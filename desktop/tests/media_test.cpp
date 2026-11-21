#include "media_engine.h"
#include <QtTest>
using namespace Cast;
class MediaTest : public QObject {
    Q_OBJECT
  private slots:
    void platformCapabilities() {
        QCOMPARE(MediaEngine::captureId("game", "windows"), QString("game_capture"));
        QVERIFY(MediaEngine::captureId("game", "x11").isEmpty());
        QCOMPARE(MediaEngine::captureId("window", "wayland"),
                 QString("pipewire-window-capture-source"));
        QVERIFY(MediaEngine::captureId("window", "unknown").isEmpty());
    }
    void settingsDoNotEnableFallback() {
        const auto s = MediaEngine::safeSettings(
            "game", {{"capture_mode", "any_fullscreen"}, {"window", "chosen"}});
        QCOMPARE(s["capture_mode"].toString(), QString("window"));
        const auto x =
            MediaEngine::safeSettings("window", {{"capture_window", 42}, {"fallback", true}});
        QVERIFY(!x["fallback"].toBool());
    }
};
QTEST_GUILESS_MAIN(MediaTest)
#include "media_test.moc"
