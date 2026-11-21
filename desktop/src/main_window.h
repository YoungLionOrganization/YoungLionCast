#pragma once
#include "document.h"
#include <QMainWindow>
class QListWidget;
class QFormLayout;
class QLabel;
class QCloseEvent;
namespace Cast {
class MediaEngine;
class Preview;
class Accounts;
class MainWindow final : public QMainWindow {
    Q_OBJECT
  public:
    explicit MainWindow(MediaEngine *engine, QWidget *parent = nullptr);
    ~MainWindow() override;
    SceneDocument *document() {
        return &m_document;
    }
    void setTheme(const QString &mode);
    QString theme() const {
        return m_theme;
    }
    void openProject(const QString &file);

  protected:
    void closeEvent(QCloseEvent *) override;
    void changeEvent(QEvent *) override;

  private:
    void refresh();
    void inspector();
    void addSource(const QString &kind);
    void editSource();
    void addScene();
    void renameScene();
    bool save(bool as = false);
    bool mayDiscard();
    void restoreLayout();
    QString selected() const;
    SceneDocument m_document;
    MediaEngine *m_engine;
    Preview *m_preview;
    QListWidget *m_scenes, *m_sources, *m_connections;
    QFormLayout *m_form;
    QLabel *m_sourceStatus = nullptr, *m_accountStatus = nullptr;
    Accounts *m_accounts = nullptr;
    QString m_theme = "system";
    bool m_refreshing = false;
    QByteArray m_defaultLayout;
};
} // namespace Cast
