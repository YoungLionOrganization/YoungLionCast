#include "document.h"
#include "media_engine.h"
#include <QApplication>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>
using namespace Cast;
class CaptureTest : public QObject {
    Q_OBJECT
  private slots:
    void realCompositionAndCapture() {
        MediaEngine engine;
        QString error;
        QVERIFY2(engine.initialize(&error), qPrintable(error));
        QVERIFY(engine.supported("image"));
        QVERIFY(engine.supported("text"));
        QTemporaryDir dir;
        QImage image(200, 100, QImage::Format_ARGB32);
        image.fill(Qt::red);
        auto file = dir.filePath("red.png");
        QVERIFY(image.save(file));
        SceneDocument doc;
        connect(&doc, &SceneDocument::changed, &engine, [&] { engine.sync(doc.json()); });
        auto id = doc.addSource("image", "red", {{"file", file}});
        QTRY_VERIFY(engine.sourceSize(id) == QSize(200, 100));
        QTRY_VERIFY(engine.readback().pixelColor(20, 20).red() > 200);
        doc.setItem(id, {{"visible", false}});
        QVERIFY(engine.readback().pixelColor(20, 20).red() < 20);
        doc.undoStack()->undo();
        QVERIFY(engine.readback().pixelColor(20, 20).red() > 200);
        doc.setItem(id, {{"opacity", 0.0}});
        QTRY_VERIFY(engine.readback().pixelColor(20, 20).alpha() < 20);
        const auto scene = doc.activeScene();
        doc.addScene("inactive");
        QVERIFY(engine.sourceSize(id).isEmpty());
        doc.selectScene(scene);
        QTRY_VERIFY(engine.sourceSize(id) == QSize(200, 100));
        doc.removeSource(id);
        QVERIFY(engine.sourceSize(id).isEmpty());
        if (engine.platform() == "x11") {
            QVERIFY(engine.supported("display"));
            auto settings = engine.defaults("display");
            settings["screen"] = 0;
            auto capture = doc.addSource("display", "screen", settings);
            QTRY_VERIFY_WITH_TIMEOUT(engine.sourceSize(capture).width() > 0, 3000);
            QVERIFY(!engine.properties(capture).isEmpty());
            doc.removeSource(capture);
        }
    }
};
int main(int argc, char **argv) {
    if (!qEnvironmentVariableIsSet("CAST_TEST_GPU"))
        return 77;
    QApplication app(argc, argv);
    CaptureTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "capture_test.moc"
