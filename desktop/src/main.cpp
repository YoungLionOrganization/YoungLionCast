#include "main_window.h"
#include "media_engine.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QImage>
#include <QMessageBox>
#include <QScreen>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <cstdio>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("YoungLion");
    app.setApplicationName("YoungLion Cast");
    app.setApplicationVersion("0.2.0");
    app.setStyle("Fusion");
    QTranslator translator;
    if (translator.load(":/i18n/younglion_tr_TR.qm"))
        app.installTranslator(&translator);
    QCommandLineParser parser;
    parser.setApplicationDescription(QObject::tr("YoungLion Cast sahne editörü"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"project", QObject::tr("Proje aç"), "file"});
    parser.addOption({"smoke", QObject::tr("Görüntü motorunu dene ve çık")});
    parser.addOption({"screenshot", QObject::tr("Smoke ekran görüntüsü"), "file"});
    parser.process(app);
    Cast::MediaEngine engine;
    QString error;
    const bool ready = engine.initialize(&error);
    if (!ready && parser.isSet("smoke")) {
        fprintf(stderr, "Media initialization failed: %s\n", qPrintable(error));
        return 2;
    }
    Cast::MainWindow window(&engine);
    window.show();
    if (!ready)
        QTimer::singleShot(0, &window, [&] {
            QMessageBox::warning(&window, QObject::tr("Yakalama motoru"), error);
        });
    if (parser.isSet("project"))
        window.openProject(parser.value("project"));
    QTemporaryDir samples;
    if (parser.isSet("smoke")) {
        QImage image(640, 360, QImage::Format_ARGB32);
        image.fill(QColor("#536dce"));
        auto file = samples.filePath("preview.png");
        image.save(file);
        auto *doc = window.document();
        const auto imageId = doc->addSource("image", "Demo görsel", {{"file", file}});
        doc->setItem(imageId, {{"x", 100}, {"y", 100}});
        const auto textId = doc->addSource(
            "text", "Başlık",
            {{"text", QString::fromUtf8("YoungLion Cast\nÖzgürce üret. İstediğin yerde paylaş.")},
             {"fontFace", "DejaVu Sans"},
             {"fontSize", 42}});
        doc->setItem(textId, {{"x", 140}, {"y", 150}});
        doc->undoStack()->clear();
        QTimer::singleShot(4000, &window, [&] {
            if (parser.isSet("screenshot"))
                QGuiApplication::primaryScreen()->grabWindow(0).save(parser.value("screenshot"));
            app.quit();
        });
    }
    return app.exec();
}
