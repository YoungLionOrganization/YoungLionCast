#include "main_window.h"
#include "media_engine.h"
#include <QDockWidget>
#include <QListWidget>
#include <QTemporaryDir>
#include <QtTest>
using namespace Cast;
class WindowTest : public QObject {
    Q_OBJECT
  private slots:
    void editorTracksUndoAndLayout() {
        MediaEngine engine;
        MainWindow window(&engine);
        window.show();
        QVERIFY(window.findChild<QDockWidget *>("scenesDock"));
        QVERIFY(window.findChild<QDockWidget *>("accountsDock"));
        auto *doc = window.document();
        doc->addSource("text", "Başlık", {{"text", "Merhaba"}});
        QCOMPARE(window.findChild<QListWidget *>("sourcesList")->count(), 1);
        doc->undoStack()->undo();
        QCOMPARE(window.findChild<QListWidget *>("sourcesList")->count(), 0);
        window.setTheme("light");
        QCOMPARE(window.theme(), QString("light"));
        QVERIFY(qApp->styleSheet().contains("#f5f6f8"));
        doc->undoStack()->clear();
    }
};
QTEST_MAIN(WindowTest)
#include "window_test.moc"
