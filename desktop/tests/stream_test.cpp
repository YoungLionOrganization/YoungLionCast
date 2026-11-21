// SPDX-License-Identifier: GPL-3.0-or-later
#include "document.h"
#include "media_engine.h"
#include "stream_session.h"
#include <QApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QtTest>

using namespace Cast;

class StreamTest : public QObject {
    Q_OBJECT

  private slots:
    void validatesLoopbackTargets() {
        const auto target = LoopbackTarget::parse("rtmp://127.0.0.1/live/test");
        QVERIFY(target);
        QCOMPARE(target->server.toString(), QString("rtmp://127.0.0.1:1935/live"));
        QCOMPARE(target->key, QString("test"));
        for (const auto &value :
             {"rtmp://192.0.2.1/live/test", "rtmp://localhost/live/test",
              "rtmps://127.0.0.1/live/test", "rtmp://127.0.0.1:0/live/test",
              "rtmp://name@127.0.0.1/live/test", "rtmp://127.0.0.1/live/test?x=y",
              "rtmp://127.0.0.1/live/test/extra", "rtmp://127.0.0.1/live/%2Ftest"})
            QVERIFY(!LoopbackTarget::parse(value));
    }

    void broadcastsAndSurvivesOneDisconnect() {
        if (!qEnvironmentVariableIsSet("CAST_TEST_RTMP"))
            QSKIP("Set CAST_TEST_RTMP in an X11 session to run the RTMP integration test");
        const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
        const auto ffprobe = QStandardPaths::findExecutable("ffprobe");
        QVERIFY(!ffmpeg.isEmpty() && !ffprobe.isEmpty());
        MediaEngine engine;
        QString error;
        QVERIFY2(engine.initialize(&error), qPrintable(error));
        SceneDocument doc;
        doc.setCanvas(1280, 720, 30);
        doc.addSource("text", "RTMP test",
                      {{"text", "YoungLion Cast"}, {"fontFace", "DejaVu Sans"}});
        engine.sync(doc.json());
        QVERIFY(StreamSession::available());

        QTcpServer a, b;
        QVERIFY(a.listen(QHostAddress::LocalHost));
        QVERIFY(b.listen(QHostAddress::LocalHost));
        const auto first = QString("rtmp://127.0.0.1:%1/live/cast").arg(a.serverPort());
        const auto second = QString("rtmp://127.0.0.1:%1/live/cast").arg(b.serverPort());
        a.close();
        b.close();
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto toolsEnvironment = QProcessEnvironment::systemEnvironment();
        // The desktop uses a minimal FFmpeg SDK. The receivers use the system FFmpeg build.
        // Keep the two library sets separate even when their major versions match.
        toolsEnvironment.remove("LD_LIBRARY_PATH");
        toolsEnvironment.remove("DYLD_LIBRARY_PATH");
        QProcess receiverA, receiverB;
        const auto file = temp.filePath("capture.flv");
        auto listen = [&](QProcess &proc, const QString &url, const QStringList &tail) {
            proc.setProcessEnvironment(toolsEnvironment);
            proc.start(ffmpeg, QStringList{"-nostdin", "-hide_banner", "-loglevel", "error",
                                           "-listen", "1", "-i", url, "-an", "-c:v", "copy"} +
                                   tail);
            return proc.waitForStarted();
        };
        QVERIFY(listen(receiverA, first, {"-f", "null", "-"}));
        QVERIFY(listen(receiverB, second, {"-f", "flv", file}));
        QTest::qWait(600);
        QVERIFY2(receiverA.state() == QProcess::Running,
                 receiverA.readAllStandardError().constData());
        QVERIFY2(receiverB.state() == QProcess::Running,
                 receiverB.readAllStandardError().constData());
        StreamSession session;
        QVERIFY(!session.start({first, first}, &error));
        QVERIFY2(session.start({first, second}, &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(session.status().size() == 2 && session.status()[0].bytes > 1000 &&
                                     session.status()[1].bytes > 1000,
                                 12000);
        const auto before = session.status()[1].bytes;
        receiverA.kill();
        QVERIFY(receiverA.waitForFinished());
        QTRY_VERIFY_WITH_TIMEOUT(session.status()[0].state != StreamSession::State::Live, 12000);
        QTRY_VERIFY_WITH_TIMEOUT(session.status()[1].bytes > before + 1000, 12000);
        QCOMPARE(session.status()[1].state, StreamSession::State::Live);
        session.stop();
        QVERIFY(!session.running());
        if (receiverB.state() != QProcess::NotRunning)
            QVERIFY(receiverB.waitForFinished(6000));
        QProcess probe;
        probe.setProcessEnvironment(toolsEnvironment);
        probe.start(ffprobe, {"-v", "error", "-show_entries", "stream=codec_name,width,height",
                              "-of", "csv=p=0", file});
        QVERIFY(probe.waitForFinished());
        QCOMPARE(probe.exitCode(), 0);
        QVERIFY(probe.readAllStandardOutput().contains("h264,1280,720"));
    }
};

int main(int argc, char **argv) {
#ifdef Q_OS_LINUX
    if (!qEnvironmentVariableIsSet("CAST_TEST_RTMP"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
    QApplication app(argc, argv);
    StreamTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "stream_test.moc"
