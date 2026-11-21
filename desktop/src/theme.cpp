#include "theme.h"
#include <QApplication>
#include <QFile>
#include <QGuiApplication>
#include <QStyleHints>
namespace Cast {
void applyTheme(const QString &mode) {
    const bool dark = mode == "dark" || (mode == "system" && qApp->styleHints()->colorScheme() ==
                                                                 Qt::ColorScheme::Dark);
    QString css;
    for (const auto &name :
         {QString("base.qss"), dark ? QString("dark.qss") : QString("light.qss")}) {
#ifdef Q_OS_MACOS
        QFile f(QCoreApplication::applicationDirPath() + "/../Resources/styles/" + name);
#else
        QFile f(QCoreApplication::applicationDirPath() + "/../share/younglion-cast/styles/" + name);
#endif
        if (!f.exists())
            f.setFileName(QString(CAST_STYLE_DIR) + "/" + name);
        if (f.open(QIODevice::ReadOnly))
            css += QString::fromUtf8(f.readAll());
    }
    qApp->setStyleSheet(css);
}
} // namespace Cast
