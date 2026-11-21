#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QUndoStack>
#include <functional>

namespace Cast {
class SceneDocument final : public QObject {
    Q_OBJECT
  public:
    explicit SceneDocument(QObject *parent = nullptr);
    QJsonObject json() const {
        return m_data;
    }
    QJsonArray scenes() const {
        return m_data["scenes"].toArray();
    }
    QJsonArray sources() const {
        return m_data["sources"].toArray();
    }
    QJsonArray items() const;
    QString activeScene() const {
        return m_data["activeScene"].toString();
    }
    QString path() const {
        return m_path;
    }
    QUndoStack *undoStack() {
        return &m_undo;
    }
    QString addScene(const QString &name);
    void renameScene(const QString &id, const QString &name);
    void selectScene(const QString &id);
    void removeScene(const QString &id);
    QString addSource(const QString &kind, const QString &name, const QJsonObject &settings);
    void removeSource(const QString &itemId);
    void moveItem(const QString &id, int direction);
    void setItem(const QString &id, const QJsonObject &changes);
    void setSource(const QString &id, const QString &name, const QJsonObject &settings);
    void setCanvas(int width, int height, int fps);
    bool replace(const QJsonObject &data, QString *error = nullptr);
    bool load(const QString &file, QString *error = nullptr);
    bool save(const QString &file, QString *error = nullptr);
    static bool validate(const QJsonObject &data, QString *error = nullptr);
    void reset();
  signals:
    void changed();
    void rejected(const QString &message);

  private:
    friend class Snapshot;
    void apply(const QJsonObject &data);
    void edit(const QString &label, const std::function<void(QJsonObject &)> &operation);
    QJsonObject m_data;
    QUndoStack m_undo;
    QString m_path;
};
} // namespace Cast
