#include "accounts.h"
#include "credentials.h"
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QOAuth2AuthorizationCodeFlow>
#include <QOAuthHttpServerReplyHandler>
#include <QOAuthOobReplyHandler>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QUrlQuery>
#include <QUuid>
#include <memory>
#include <utility>
namespace Cast {
static bool secure(const QUrl &u) {
    return u.isValid() && u.scheme() == "https" && !u.host().isEmpty() && u.userInfo().isEmpty() &&
           !u.hasFragment() && !u.hasQuery();
}
static QString digest(const QString &s) {
    return QString::fromLatin1(
        QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha256).toHex());
}
AccountConfig AccountConfig::parse(const QJsonObject &j, QString *error) {
    AccountConfig c;
    c.clientId = j["clientId"].toString();
    c.publishableKey = j["publishableKey"].toString();
    c.authorizationUrl = QUrl(j["authorizationUrl"].toString());
    c.tokenUrl = QUrl(j["tokenUrl"].toString());
    c.apiBase = QUrl(j["apiBase"].toString());
    c.accountWebUrl = QUrl(j["accountWebUrl"].toString());
    c.logoutUrl = QUrl(j["logoutUrl"].toString());
    if (c.clientId.isEmpty() || c.clientId.size() > 512 || c.publishableKey.isEmpty() ||
        !secure(c.authorizationUrl) || !secure(c.tokenUrl) || !secure(c.apiBase) ||
        !secure(c.accountWebUrl) || !secure(c.logoutUrl) ||
        c.authorizationUrl.host() != c.tokenUrl.host() ||
        c.authorizationUrl.port() != c.tokenUrl.port() || c.logoutUrl.host() != c.tokenUrl.host() ||
        c.logoutUrl.port() != c.tokenUrl.port()) {
        if (error)
            *error = QObject::tr(
                "Hesap ayarlarında HTTPS adresleri ve aynı kimlik sağlayıcısı gerekli.");
        return {};
    }
    return c;
}
AccountConfig AccountConfig::load(QString *error) {
    const auto path = qEnvironmentVariable(
        "CAST_ACCOUNT_CONFIG",
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/account.json");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() > 16384) {
        if (error)
            *error =
                QObject::tr("Hesap servisi ayarlanmadı. account.json yapılandırmasını ekleyin.");
        return {};
    }
    QJsonParseError pe;
    const auto json = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError) {
        if (error)
            *error = QObject::tr("Hesap yapılandırması okunamadı.");
        return {};
    }
    return parse(json.object(), error);
}
Accounts::Accounts(AccountConfig c, QObject *parent)
    : QObject(parent), m_config(std::move(c)),
      m_credentials(
          new Credentials(digest(m_config.tokenUrl.toString() + "/" + m_config.clientId), this)),
      m_network(this) {
    m_status = configured() ? tr("Giriş yapılmadı.") : tr("YoungLion Account yapılandırılmadı.");
    m_poll.setInterval(3000);
    connect(&m_poll, &QTimer::timeout, this, &Accounts::poll);
    m_linkTimeout.setSingleShot(true);
    connect(&m_linkTimeout, &QTimer::timeout, this, [this] {
        cancelLink();
        fail(tr("Hesap bağlantısı zaman aşımına uğradı."));
    });
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (!authenticated()) {
            ++m_generation;
            delete m_flow;
            delete m_reply;
        }
        cancelLink();
        fail(tr("İşlem zaman aşımına uğradı. Yeniden deneyin."));
    });
}
Accounts::~Accounts() {
    ++m_generation;
    delete m_flow;
    delete m_reply;
    const auto requests = std::exchange(m_requests, {});
    for (auto r : requests)
        if (r) {
            QObject::disconnect(r, nullptr, this, nullptr);
            r->abort();
            r->deleteLater();
        }
}
void Accounts::fail(const QString &message) {
    m_status = message;
    emit changed();
}
void Accounts::configureFlow() {
    delete m_flow;
    delete m_reply;
    m_flow = new QOAuth2AuthorizationCodeFlow(&m_network, this);
    m_flow->setReplyHandler(new QOAuthOobReplyHandler(m_flow));
    m_flow->setClientIdentifier(m_config.clientId);
    m_flow->setAuthorizationUrl(m_config.authorizationUrl);
    m_flow->setTokenUrl(m_config.tokenUrl);
    m_flow->setRequestedScopeTokens({"openid", "profile", "email"});
    m_flow->setPkceMethod(QOAuth2AuthorizationCodeFlow::PkceMethod::S256, 64);
    m_flow->setAutoRefresh(true);
    m_flow->setNetworkRequestModifier(this, [this](QNetworkRequest &r, QAbstractOAuth::Stage) {
        r.setRawHeader("apikey", m_config.publishableKey.toUtf8());
        r.setTransferTimeout(15000);
        r.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
    });
    const auto generation = m_generation;
    connect(m_flow, &QAbstractOAuth::authorizeWithBrowser, this,
            [this, generation](const QUrl &url) {
                if (generation == m_generation && !QDesktopServices::openUrl(url))
                    fail(tr("Sistem tarayıcısı açılamadı."));
            });
    connect(
        m_flow, &QAbstractOAuth::tokenChanged, this,
        [this, generation](const QString &token) {
            if (generation == m_generation && !token.isEmpty())
                accepted();
        },
        Qt::QueuedConnection);
    connect(m_flow, &QAbstractOAuth::requestFailed, this,
            [this, generation](QAbstractOAuth::Error) {
                if (generation == m_generation) {
                    ++m_generation;
                    if (m_flow) {
                        m_flow->setAutoRefresh(false);
                        m_flow->deleteLater();
                        m_flow = nullptr;
                    }
                    cancelLink();
                    m_access.clear();
                    m_connections = {};
                    m_poll.stop();
                    m_timeout.stop();
                    if (m_reply)
                        m_reply->close();
                    fail(tr("Hesap yetkilendirmesi başarısız. Yeniden giriş yapın."));
                }
            });
}
void Accounts::restore() {
    if (!configured())
        return;
    const auto gen = m_generation;
    m_credentials->read([this, gen](bool ok, const QString &token) {
        if (gen != m_generation || !ok || token.isEmpty())
            return;
        configureFlow();
        m_flow->setRefreshToken(token);
        m_timeout.start(120000);
        m_status = tr("Oturum yenileniyor…");
        emit changed();
        m_flow->refreshTokens();
    });
}
void Accounts::login() {
    if (!configured())
        return;
    logout();
    configureFlow();
    m_reply = new QOAuthHttpServerReplyHandler(QHostAddress::LocalHost, 49173, this);
    m_reply->setCallbackHost("127.0.0.1");
    m_reply->setCallbackPath("/oauth/callback");
    m_reply->setCallbackText(tr("YoungLion Cast'a dönebilirsiniz."));
    if (!m_reply->isListening()) {
        delete m_reply;
        delete m_flow;
        fail(tr("49173 yerel bağlantı noktası kullanımda. Kapatıp yeniden deneyin."));
        return;
    }
    m_flow->setReplyHandler(m_reply);
    m_timeout.start(120000);
    m_status = tr("Tarayıcıda YoungLion girişi bekleniyor…");
    emit changed();
    m_flow->grant();
}
void Accounts::accepted() {
    if (!m_flow)
        return;
    m_timeout.stop();
    if (m_reply)
        m_reply->close();
    const auto token = m_flow->token(), refresh = m_flow->refreshToken();
    const auto generation = m_generation;
    if (token.isEmpty() || refresh.isEmpty()) {
        fail(tr("Sunucu oturum yenileme belirteci vermedi."));
        return;
    }
    m_credentials->write(refresh, [this, generation, token](bool ok, const QString &) {
        if (generation != m_generation)
            return;
        if (!ok) {
            delete m_flow;
            m_access.clear();
            fail(tr("Sistem anahtarlığına erişilemiyor. Güvenli oturum kaydedilemedi."));
            return;
        }
        const bool initial = m_access.isEmpty();
        m_access = token;
        if (initial) {
            m_status = tr("YoungLion oturumu açık.");
            emit changed();
            reload();
        }
    });
}
void Accounts::logout() {
    ++m_generation;
    m_poll.stop();
    m_timeout.stop();
    m_linkTimeout.stop();
    m_nativeId.clear();
    m_proof.clear();
    m_transaction.clear();
    m_linkRequest = false;
    const auto requests = std::exchange(m_requests, {});
    for (auto r : requests)
        if (r) {
            QObject::disconnect(r, nullptr, this, nullptr);
            r->abort();
            r->deleteLater();
        }
    m_requests.clear();
    const auto old = m_access;
    m_access.clear();
    m_connections = {};
    delete m_flow;
    delete m_reply;
    if (configured())
        m_credentials->clear([](bool, const QString &) {});
    if (!old.isEmpty() && configured()) {
        auto url = m_config.logoutUrl;
        QUrlQuery query;
        query.addQueryItem("scope", "local");
        url.setQuery(query);
        QNetworkRequest r(url);
        r.setRawHeader("Authorization", "Bearer " + old.toUtf8());
        r.setRawHeader("apikey", m_config.publishableKey.toUtf8());
        r.setTransferTimeout(8000);
        r.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
        auto *reply = m_network.post(r, QByteArray{});
        connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    }
    m_status = tr("Çıkış yapıldı.");
    emit changed();
}
void Accounts::request(const QByteArray &method, const QString &path, const QJsonObject &body,
                       Result done) {
    if (!authenticated())
        return;
    const auto gen = m_generation;
    QNetworkRequest r(QUrl(m_config.apiBase.toString() + path));
    r.setRawHeader("Authorization", "Bearer " + m_access.toUtf8());
    r.setRawHeader("apikey", m_config.publishableKey.toUtf8());
    r.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    r.setTransferTimeout(15000);
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    auto *reply = m_network.sendCustomRequest(
        r, method,
        method == "GET" ? QByteArray{} : QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_requests.append(reply);
    connect(reply, &QNetworkReply::readyRead, this, [reply] {
        if (reply->bytesAvailable() > 262144)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen, done = std::move(done)] {
        m_requests.removeAll(reply);
        reply->deleteLater();
        if (gen != m_generation)
            return;
        const auto bytes = reply->readAll();
        const auto code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (code == 401) {
            logout();
            fail(tr("Oturum sona erdi. Yeniden giriş yapın."));
            return;
        }
        if (reply->error() != QNetworkReply::NoError || code < 200 || code >= 300 ||
            bytes.size() > 262144) {
            m_poll.stop();
            m_linkRequest = false;
            fail(tr("Hesap isteği tamamlanamadı (%1). Yeniden deneyin.").arg(code));
            return;
        }
        QJsonParseError pe;
        auto json = QJsonDocument::fromJson(bytes, &pe);
        if (pe.error != QJsonParseError::NoError || !json.isObject()) {
            fail(tr("Sunucu yanıtı geçersiz."));
            return;
        }
        done(json.object());
    });
}
void Accounts::reload() {
    const auto revision = ++m_listGeneration;
    m_connections = {};
    auto fetch = std::make_shared<std::function<void(QString, int)>>();
    std::weak_ptr<std::function<void(QString, int)>> weak = fetch;
    *fetch = [this, weak, revision](QString after, int page) {
        const auto keep = weak.lock();
        if (!keep)
            return;
        request("GET", "/connections" + (after.isEmpty() ? QString{} : "?after=" + after), {},
                [this, keep, page, revision](const QJsonObject &j) {
                    if (revision != m_listGeneration)
                        return;
                    for (const auto &v : j["connections"].toArray())
                        m_connections.append(v);
                    emit changed();
                    const auto next = j["next_cursor"].toString();
                    if (!QUuid(next).isNull() && page < 100)
                        (*keep)(next, page + 1);
                });
    };
    (*fetch)({}, 0);
}
void Accounts::link(const QString &provider, const QString &id) {
    if (!QStringList{"twitch", "kick"}.contains(provider) || !authenticated())
        return;
    cancelLink();
    QByteArray random(32, Qt::Uninitialized);
    for (auto &b : random)
        b = char(QRandomGenerator::system()->generate() & 255);
    m_proof = QString::fromLatin1(
        random.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    QJsonObject b{{"provider", provider}, {"proof_challenge", digest(m_proof)}};
    if (!id.isEmpty())
        b["connection_id"] = id;
    const auto proof = m_proof;
    request("POST", "/native/start", b, [this, proof](const QJsonObject &j) {
        if (proof != m_proof)
            return;
        m_nativeId = j["request_id"].toString();
        auto url = m_config.accountWebUrl;
        QUrlQuery query;
        query.addQueryItem("native_ticket", j["ticket"].toString());
        query.addQueryItem("native_request", m_nativeId);
        url.setQuery(query);
        if (!QDesktopServices::openUrl(url)) {
            cancelLink();
            fail(tr("Sistem tarayıcısı açılamadı."));
            return;
        }
        m_status = tr("Tarayıcıda aynı YoungLion hesabıyla bağlantıyı onaylayın…");
        m_poll.start();
        m_linkTimeout.start(600000);
        emit changed();
    });
}
void Accounts::poll() {
    if (m_nativeId.isEmpty() || m_linkRequest)
        return;
    const auto id = m_nativeId;
    m_linkRequest = true;
    request("POST", "/native/status", {{"request_id", id}, {"proof", m_proof}},
            [this, id](const QJsonObject &j) {
                if (id != m_nativeId)
                    return;
                m_linkRequest = false;
                const auto state = j["status"].toString();
                if (state == "pending") {
                    m_poll.stop();
                    m_transaction = j["transaction_id"].toString();
                    emit pending(j["account"].toObject());
                } else if (state == "failed" || state == "expired" || state == "cancelled") {
                    cancelLink();
                    fail(tr("Hesap bağlantısı tamamlanmadı."));
                } else if (state == "confirmed") {
                    cancelLink();
                    reload();
                }
            });
}
void Accounts::confirmLink() {
    if (m_transaction.isEmpty())
        return;
    const auto id = m_nativeId;
    request("POST", "/native/confirm", {{"request_id", id}, {"proof", m_proof}},
            [this, id](const QJsonObject &) {
                if (id != m_nativeId)
                    return;
                m_nativeId.clear();
                cancelLink();
                m_status = tr("Yayın hesabı bağlandı.");
                reload();
                emit changed();
            });
}
void Accounts::cancelLink() {
    const auto id = m_nativeId, proof = m_proof;
    m_poll.stop();
    m_linkTimeout.stop();
    m_nativeId.clear();
    m_proof.clear();
    m_transaction.clear();
    m_linkRequest = false;
    if (!id.isEmpty())
        request("POST", "/native/cancel", {{"request_id", id}, {"proof", proof}},
                [](const QJsonObject &) {});
}
void Accounts::disconnectAccount(const QString &id) {
    if (QUuid(id).isNull())
        return;
    request("DELETE", "/connections/" + id, {}, [this](const QJsonObject &) { reload(); });
}
} // namespace Cast
