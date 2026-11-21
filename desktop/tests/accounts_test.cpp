#include "accounts.h"
#include <QDesktopServices>
#include <QUrlQuery>
#include <QtTest>
using namespace Cast;
class AccountsTest : public QObject {
    Q_OBJECT
    static QJsonObject sample() {
        return QJsonObject{{"clientId", "public-client"},
                           {"authorizationUrl", "https://account.example/auth/v1/oauth/authorize"},
                           {"tokenUrl", "https://account.example/auth/v1/oauth/token"},
                           {"apiBase", "https://api.example/functions/v1/platform-connections/v1"},
                           {"accountWebUrl", "https://account.example/connections"},
                           {"logoutUrl", "https://account.example/auth/v1/logout"},
                           {"publishableKey", "public-key"}};
    }
    QUrl opened;
  private slots:
    void captureUrl(const QUrl &url) {
        opened = url;
    }
    void pkceBrowserUsesRegisteredLoopback() {
        Accounts a(AccountConfig::parse(sample()));
        QDesktopServices::setUrlHandler("https", this, "captureUrl");
        a.login();
        QDesktopServices::unsetUrlHandler("https");
        QCOMPARE(opened.host(), QString("account.example"));
        const QUrlQuery query(opened);
        QCOMPARE(query.queryItemValue("redirect_uri"),
                 QString("http://127.0.0.1:49173/oauth/callback"));
        QCOMPARE(query.queryItemValue("code_challenge_method"), QString("S256"));
        QCOMPARE(query.queryItemValue("code_challenge").size(), 43);
        QVERIFY(!query.queryItemValue("state").isEmpty());
        QVERIFY(!query.hasQueryItem("client_secret"));
    }
    void configurationFailsClosed() {
        QString error;
        QVERIFY(!AccountConfig::parse({}, &error).valid());
        auto c = sample();
        QVERIFY(AccountConfig::parse(c, &error).valid());
        c["tokenUrl"] = "http://account.example/token";
        QVERIFY(!AccountConfig::parse(c, &error).valid());
        c["tokenUrl"] = "https://attacker.example/token";
        QVERIFY(!AccountConfig::parse(c, &error).valid());
    }
    void logoutCancelsAndClears() {
        Accounts a(AccountConfig{});
        QSignalSpy changes(&a, &Accounts::changed);
        a.logout();
        QVERIFY(!a.authenticated());
        QVERIFY(!changes.isEmpty());
        QVERIFY(a.connections().isEmpty());
    }
};
QTEST_MAIN(AccountsTest)
#include "accounts_test.moc"
