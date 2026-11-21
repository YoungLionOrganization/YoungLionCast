#include "document.h"
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class DocumentTest : public QObject {
    Q_OBJECT
  private slots:
    void editsUndoAndRoundTrip() {
        Cast::SceneDocument document;
        QCOMPARE(document.scenes().size(), 1);
        const auto source =
            document.addSource("text", "Başlık", {{"text", "Əla yayın — İstanbul"}});
        QVERIFY(!source.isEmpty());
        QCOMPARE(document.items().size(), 1);
        document.setItem(source, {{"x", 140.0}, {"y", 90.0}, {"scaleX", 0.5}, {"opacity", 0.7}});
        QCOMPARE(document.items().at(0).toObject()["x"].toDouble(), 140.0);
        document.undoStack()->undo();
        QCOMPARE(document.items().at(0).toObject()["x"].toDouble(), 0.0);
        document.undoStack()->redo();
        QTemporaryDir dir;
        const auto file = dir.filePath("scene.cast.json");
        QString error;
        QVERIFY2(document.save(file, &error), qPrintable(error));
        Cast::SceneDocument loaded;
        QVERIFY2(loaded.load(file, &error), qPrintable(error));
        QCOMPARE(loaded.sources().size(), 1);
        QCOMPARE(loaded.sources().at(0).toObject()["settings"].toObject()["text"].toString(),
                 QString("Əla yayın — İstanbul"));
        QCOMPARE(loaded.items().at(0).toObject()["x"].toDouble(), 140.0);
    }
    void invalidLoadIsAtomic() {
        Cast::SceneDocument d;
        d.addSource("text", "Keep", {{"text", "Keep"}});
        const auto before = d.json();
        auto invalid = before;
        invalid["version"] = 99;
        QString error;
        QVERIFY(!d.replace(invalid, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(d.json(), before);
        invalid = before;
        auto scenes = invalid["scenes"].toArray();
        auto scene = scenes[0].toObject();
        auto items = scene["items"].toArray();
        auto item = items[0].toObject();
        item["sourceId"] = "missing";
        items[0] = item;
        scene["items"] = items;
        scenes[0] = scene;
        invalid["scenes"] = scenes;
        QVERIFY(!d.replace(invalid, &error));
        QCOMPARE(d.json(), before);
    }
    void removingSceneDropsUnusedSources() {
        Cast::SceneDocument d;
        const auto first = d.activeScene();
        d.addSource("text", "first", {{"text", "one"}});
        const auto other = d.addScene("other");
        d.addSource("text", "other", {{"text", "two"}});
        d.removeScene(first);
        QCOMPARE(d.sources().size(), 1);
        QCOMPARE(d.activeScene(), other);
        d.undoStack()->undo();
        QCOMPARE(d.sources().size(), 2);
    }
    void locksAndSceneReferences() {
        Cast::SceneDocument d;
        const auto id = d.addSource("image", "Logo", {{"file", "logo.png"}});
        d.setItem(id, {{"locked", true}});
        d.setItem(id, {{"x", 500.0}});
        QCOMPARE(d.items()[0].toObject()["x"].toDouble(), 0.0);
        d.setItem(id, {{"visible", false}, {"x", 700.0}});
        QCOMPARE(d.items()[0].toObject()["x"].toDouble(), 0.0);
        d.setItem(id, {{"locked", false}});
        d.setItem(id, {{"x", 50.0}});
        QCOMPARE(d.items()[0].toObject()["x"].toDouble(), 50.0);
        const auto next = d.addScene("Other");
        QCOMPARE(d.activeScene(), next);
        QCOMPARE(d.items().size(), 0);
        d.undoStack()->undo();
        QCOMPARE(d.scenes().size(), 1);
        QCOMPARE(d.items().size(), 1);
        d.removeSource(id);
        QCOMPARE(d.items().size(), 0);
        QCOMPARE(d.sources().size(), 0);
    }
};
QTEST_GUILESS_MAIN(DocumentTest)
#include "document_test.moc"
