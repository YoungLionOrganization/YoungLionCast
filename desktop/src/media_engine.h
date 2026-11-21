#pragma once
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QPolygonF>
#include <QSize>
#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif
#include <obs.h>
namespace Cast {
struct SourceOption {
    QString label;
    QJsonValue value;
};
struct SourceProperty {
    QString key, label;
    int type;
    QList<SourceOption> options;
    int minimum = 0, maximum = 100;
};
class MediaEngine final : public QObject {
    Q_OBJECT
  public:
    explicit MediaEngine(QObject *parent = nullptr);
    ~MediaEngine() override;
    bool initialize(QString *error);
    bool ready() const {
        return m_ready;
    }
    QString platform() const;
    static QString captureId(const QString &kind, const QString &platform);
    static QJsonObject safeSettings(const QString &kind, QJsonObject settings);
    QString sourceType(const QString &kind) const;
    bool supported(const QString &kind) const;
    QJsonObject defaults(const QString &kind) const;
    QList<SourceProperty> properties(const QString &sourceId, const QString &kind = {});
    bool captureTargetSelected(const QString &kind, const QJsonObject &settings) const;
    void propertyButton(const QString &sourceId, const QString &key);
    void sync(const QJsonObject &document);
    QString status(const QString &sourceId) const;
    QSize sourceSize(const QString &sourceId) const;
    QSize canvas() const {
        return m_canvas;
    }
    QImage readback();
    void setSelection(const QJsonObject &item);
    void draw(uint32_t width, uint32_t height);
  signals:
    void error(const QString &message);

  private:
    struct Source {
        obs_source_t *handle = nullptr;
        QJsonObject spec;
        QString error;
    };
    QMap<QString, Source> m_sources;
    QHash<QString, obs_sceneitem_t *> m_items;
    QHash<QString, QJsonObject> m_itemSpecs;
    obs_scene_t *m_scene = nullptr;
    bool m_started = false, m_ready = false;
    QSize m_canvas{1920, 1080};
    int m_fps = 30;
    QMutex m_selectionMutex;
    QPolygonF m_selection;
};
} // namespace Cast
