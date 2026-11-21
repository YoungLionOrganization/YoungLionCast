#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <functional>
class QOAuth2AuthorizationCodeFlow;
class QOAuthHttpServerReplyHandler;
class QNetworkReply;
namespace Cast {
class Credentials;
struct AccountConfig {
    QString clientId, publishableKey;
    QUrl authorizationUrl, tokenUrl, apiBase, accountWebUrl, logoutUrl;
    bool valid() const {
        return !clientId.isEmpty();
    }
    static AccountConfig parse(const QJsonObject &, QString *error = nullptr);
    static AccountConfig load(QString *error = nullptr);
};
class Accounts final : public QObject {
    Q_OBJECT
  public:
    explicit Accounts(AccountConfig config, QObject *parent = nullptr);
    ~Accounts() override;
    bool configured() const {
        return m_config.valid();
    }
    bool authenticated() const {
        return !m_access.isEmpty();
    }
    QString status() const {
        return m_status;
    }
    QJsonArray connections() const {
        return m_connections;
    }
    void restore();
    void login();
    void logout();
    void reload();
    void cancelLink();
    void link(const QString &provider, const QString &connectionId = {});
    void confirmLink();
    void disconnectAccount(const QString &id);
  signals:
    void changed();
    void pending(const QJsonObject &account);

  private:
    using Result = std::function<void(const QJsonObject &)>;
    void configureFlow();
    void accepted();
    void fail(const QString &);
    void request(const QByteArray &method, const QString &path, const QJsonObject &body,
                 Result done);
    void poll();
    AccountConfig m_config;
    Credentials *m_credentials;
    QNetworkAccessManager m_network;
    QPointer<QOAuth2AuthorizationCodeFlow> m_flow;
    QPointer<QOAuthHttpServerReplyHandler> m_reply;
    QList<QPointer<QNetworkReply>> m_requests;
    QTimer m_poll, m_timeout, m_linkTimeout;
    bool m_linkRequest = false;
    quint64 m_generation = 0, m_listGeneration = 0;
    QString m_access, m_status, m_nativeId, m_proof, m_transaction;
    QJsonArray m_connections;
};
} // namespace Cast
