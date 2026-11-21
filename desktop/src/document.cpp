#include "document.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <cmath>

namespace Cast {
static constexpr qint64 maxProjectBytes = 8 * 1024 * 1024;
static QString uid() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
static QJsonObject initial() {
    const auto id = uid();
    return {
        {"version", 1},
        {"canvas", QJsonObject{{"width", 1920}, {"height", 1080}, {"fps", 30}}},
        {"activeScene", id},
        {"sources", QJsonArray{}},
        {"scenes", QJsonArray{QJsonObject{
                       {"id", id}, {"name", QObject::tr("Sahne 1")}, {"items", QJsonArray{}}}}}};
}
static int index(const QJsonArray &array, const QString &id) {
    for (qsizetype i = 0; i < array.size(); ++i)
        if (array[i].toObject()["id"].toString() == id)
            return int(i);
    return -1;
}
class Snapshot final : public QUndoCommand {
  public:
    Snapshot(SceneDocument *d, QJsonObject before, QJsonObject after, const QString &label)
        : QUndoCommand(label), doc(d), oldData(std::move(before)), newData(std::move(after)) {}
    void undo() override {
        doc->apply(oldData);
    }
    void redo() override {
        doc->apply(newData);
    }

  private:
    SceneDocument *doc;
    QJsonObject oldData, newData;
};
SceneDocument::SceneDocument(QObject *parent) : QObject(parent), m_data(initial()), m_undo(this) {
    m_undo.setUndoLimit(100);
}
void SceneDocument::apply(const QJsonObject &data) {
    m_data = data;
    emit changed();
}
void SceneDocument::reset() {
    m_path.clear();
    m_undo.clear();
    apply(initial());
    m_undo.setClean();
}
QJsonArray SceneDocument::items() const {
    for (const auto &v : scenes())
        if (v.toObject()["id"].toString() == activeScene())
            return v.toObject()["items"].toArray();
    return {};
}
void SceneDocument::edit(const QString &label, const std::function<void(QJsonObject &)> &fn) {
    auto next = m_data;
    fn(next);
    QString error;
    if (!validate(next, &error)) {
        emit rejected(error);
        return;
    }
    if (next != m_data)
        m_undo.push(new Snapshot(this, m_data, next, label));
}
QString SceneDocument::addScene(const QString &name) {
    const auto id = uid();
    edit(tr("Sahne ekle"), [&](auto &d) {
        auto a = d["scenes"].toArray();
        a.append(QJsonObject{{"id", id}, {"name", name}, {"items", QJsonArray{}}});
        d["scenes"] = a;
        d["activeScene"] = id;
    });
    return index(scenes(), id) >= 0 ? id : QString{};
}
void SceneDocument::renameScene(const QString &id, const QString &name) {
    edit(tr("Sahne adı"), [&](auto &d) {
        auto a = d["scenes"].toArray();
        int i = index(a, id);
        if (i < 0)
            return;
        auto s = a[i].toObject();
        s["name"] = name;
        a[i] = s;
        d["scenes"] = a;
    });
}
void SceneDocument::selectScene(const QString &id) {
    if (index(scenes(), id) < 0)
        return;
    m_data["activeScene"] = id;
    emit changed();
}
void SceneDocument::removeScene(const QString &id) {
    if (scenes().size() <= 1)
        return;
    edit(tr("Sahne sil"), [&](auto &d) {
        auto a = d["scenes"].toArray();
        const int i = index(a, id);
        if (i < 0)
            return;
        a.removeAt(i);
        d["scenes"] = a;
        QSet<QString> referenced;
        for (const auto &scene : a)
            for (const auto &item : scene.toObject()["items"].toArray())
                referenced.insert(item.toObject()["sourceId"].toString());
        QJsonArray sources;
        for (const auto &source : d["sources"].toArray())
            if (referenced.contains(source.toObject()["id"].toString()))
                sources.append(source);
        d["sources"] = sources;
        if (d["activeScene"].toString() == id)
            d["activeScene"] = a[0].toObject()["id"];
    });
}
QString SceneDocument::addSource(const QString &kind, const QString &name,
                                 const QJsonObject &settings) {
    const auto id = uid();
    edit(tr("Kaynak ekle"), [&](auto &d) {
        auto s = d["sources"].toArray();
        s.append(QJsonObject{{"id", id}, {"kind", kind}, {"name", name}, {"settings", settings}});
        d["sources"] = s;
        auto scenes = d["scenes"].toArray();
        int i = index(scenes, activeScene());
        auto scene = scenes[i].toObject();
        auto items = scene["items"].toArray();
        items.append(QJsonObject{{"id", id},
                                 {"sourceId", id},
                                 {"x", 0.0},
                                 {"y", 0.0},
                                 {"scaleX", 1.0},
                                 {"scaleY", 1.0},
                                 {"rotation", 0.0},
                                 {"opacity", 1.0},
                                 {"visible", true},
                                 {"locked", false},
                                 {"cropLeft", 0},
                                 {"cropTop", 0},
                                 {"cropRight", 0},
                                 {"cropBottom", 0}});
        scene["items"] = items;
        scenes[i] = scene;
        d["scenes"] = scenes;
    });
    return index(sources(), id) >= 0 ? id : QString{};
}
void SceneDocument::removeSource(const QString &id) {
    edit(tr("Kaynak sil"), [&](auto &d) {
        auto scenes = d["scenes"].toArray();
        int si = index(scenes, activeScene());
        auto scene = scenes[si].toObject();
        auto a = scene["items"].toArray();
        int ii = index(a, id);
        if (ii < 0)
            return;
        const auto sourceId = a[ii].toObject()["sourceId"].toString();
        a.removeAt(ii);
        scene["items"] = a;
        scenes[si] = scene;
        d["scenes"] = scenes;
        bool used = false;
        for (const auto &v : scenes)
            for (const auto &item : v.toObject()["items"].toArray())
                if (item.toObject()["sourceId"].toString() == sourceId)
                    used = true;
        if (!used) {
            auto sources = d["sources"].toArray();
            const int i = index(sources, sourceId);
            if (i >= 0)
                sources.removeAt(i);
            d["sources"] = sources;
        }
    });
}
void SceneDocument::moveItem(const QString &id, int direction) {
    edit(tr("Katman sırası"), [&](auto &d) {
        auto s = d["scenes"].toArray();
        int i = index(s, activeScene());
        auto scene = s[i].toObject();
        auto a = scene["items"].toArray();
        int at = index(a, id);
        const int next = at + direction;
        if (at < 0 || next < 0 || next >= a.size())
            return;
        const auto value = a.takeAt(at);
        a.insert(next, value);
        scene["items"] = a;
        s[i] = scene;
        d["scenes"] = s;
    });
}
void SceneDocument::setItem(const QString &id, const QJsonObject &changes) {
    edit(tr("Kaynak dönüşümü"), [&](auto &d) {
        auto s = d["scenes"].toArray();
        int i = index(s, activeScene());
        auto scene = s[i].toObject();
        auto a = scene["items"].toArray();
        int at = index(a, id);
        if (at < 0)
            return;
        auto item = a[at].toObject();
        if (item["locked"].toBool() && changes["locked"] != QJsonValue(false)) {
            for (auto it = changes.begin(); it != changes.end(); ++it)
                if (it.key() != "locked" && it.key() != "visible")
                    return;
        }
        for (auto it = changes.begin(); it != changes.end(); ++it)
            if (it.key() != "id" && it.key() != "sourceId")
                item[it.key()] = it.value();
        a[at] = item;
        scene["items"] = a;
        s[i] = scene;
        d["scenes"] = s;
    });
}
void SceneDocument::setSource(const QString &id, const QString &name, const QJsonObject &settings) {
    edit(tr("Kaynak özellikleri"), [&](auto &d) {
        auto a = d["sources"].toArray();
        int at = index(a, id);
        if (at < 0)
            return;
        auto source = a[at].toObject();
        source["name"] = name;
        source["settings"] = settings;
        a[at] = source;
        d["sources"] = a;
    });
}
void SceneDocument::setCanvas(int w, int h, int fps) {
    edit(tr("Tuval ayarları"),
         [&](auto &d) { d["canvas"] = QJsonObject{{"width", w}, {"height", h}, {"fps", fps}}; });
}
bool SceneDocument::validate(const QJsonObject &d, QString *error) {
    auto fail = [&](const QString &s) {
        if (error)
            *error = s;
        return false;
    };
    if (d["version"].toInt() != 1)
        return fail(tr("Desteklenmeyen proje sürümü."));
    const auto c = d["canvas"].toObject();
    if (!((c["width"].toInt() == 1920 && c["height"].toInt() == 1080) ||
          (c["width"].toInt() == 1280 && c["height"].toInt() == 720)) ||
        (c["fps"].toInt() != 30 && c["fps"].toInt() != 60))
        return fail(tr("Geçersiz tuval ayarı."));
    const auto sources = d["sources"].toArray(), scenes = d["scenes"].toArray();
    if (!d["sources"].isArray() || !d["scenes"].isArray() || sources.size() > 1024 ||
        scenes.isEmpty() || scenes.size() > 128)
        return fail(tr("Geçersiz sahne veya kaynak sayısı."));
    QSet<QString> sourceIds, sceneIds, itemIds;
    auto validName = [](const QJsonValue &v) {
        return v.isString() && !v.toString().trimmed().isEmpty() && v.toString().size() <= 160;
    };
    for (const auto &v : sources) {
        const auto s = v.toObject();
        const auto id = s["id"].toString(), kind = s["kind"].toString();
        if (QUuid(id).isNull() || sourceIds.contains(id) || !validName(s["name"]) ||
            !QStringList{"text", "image", "display", "window", "application", "game"}.contains(
                kind) ||
            !s["settings"].isObject())
            return fail(tr("Geçersiz kaynak."));
        const auto settings = s["settings"].toObject();
        if (kind == "text" &&
            (!settings["text"].isString() || settings["text"].toString().size() > 100000))
            return fail(tr("Geçersiz yazı."));
        if (kind == "text") {
            if (settings.contains("fontSize") &&
                (settings["fontSize"].toInt() < 8 || settings["fontSize"].toInt() > 512))
                return fail(tr("Geçersiz yazı boyutu."));
            if (settings.contains("fontFace") &&
                (!settings["fontFace"].isString() || settings["fontFace"].toString().size() > 128))
                return fail(tr("Geçersiz yazı tipi."));
            const double color = settings["color"].toDouble(-1);
            if (settings.contains("color") && (!std::isfinite(color) || color < 0 ||
                                               color > 0xffffffffu || std::floor(color) != color))
                return fail(tr("Geçersiz yazı rengi."));
        }
        if (kind == "image" &&
            (!settings["file"].isString() || settings["file"].toString().size() > 4096 ||
             settings["file"].toString().contains("://") ||
             QDir::fromNativeSeparators(settings["file"].toString()).startsWith("//")))
            return fail(tr("Görsel yerel bir dosya olmalıdır."));
        sourceIds.insert(id);
    }
    for (const auto &v : scenes) {
        const auto s = v.toObject();
        const auto id = s["id"].toString();
        if (QUuid(id).isNull() || sceneIds.contains(id) || !validName(s["name"]) ||
            !s["items"].isArray() || s["items"].toArray().size() > 1024)
            return fail(tr("Geçersiz sahne."));
        sceneIds.insert(id);
        for (const auto &iv : s["items"].toArray()) {
            auto item = iv.toObject();
            const auto iid = item["id"].toString();
            if (QUuid(iid).isNull() || itemIds.contains(iid) ||
                !sourceIds.contains(item["sourceId"].toString()))
                return fail(tr("Geçersiz kaynak başvurusu."));
            itemIds.insert(iid);
            for (const auto &key : QStringList{"x", "y", "rotation", "scaleX", "scaleY", "opacity",
                                               "cropLeft", "cropTop", "cropRight", "cropBottom"}) {
                const auto v = item[key];
                if (!v.isDouble() || !std::isfinite(v.toDouble()) ||
                    std::abs(v.toDouble()) > 100000)
                    return fail(tr("Geçersiz dönüşüm."));
            }
            if (item["scaleX"].toDouble() <= 0 || item["scaleY"].toDouble() <= 0 ||
                item["opacity"].toDouble() < 0 || item["opacity"].toDouble() > 1 ||
                !item["visible"].isBool() || !item["locked"].isBool())
                return fail(tr("Geçersiz kaynak durumu."));
            for (const auto &key : QStringList{"cropLeft", "cropTop", "cropRight", "cropBottom"})
                if (item[key].toDouble() < 0 ||
                    std::floor(item[key].toDouble()) != item[key].toDouble())
                    return fail(tr("Geçersiz kırpma."));
        }
    }
    if (!sceneIds.contains(d["activeScene"].toString()))
        return fail(tr("Etkin sahne bulunamadı."));
    return true;
}
bool SceneDocument::replace(const QJsonObject &data, QString *error) {
    if (!validate(data, error))
        return false;
    m_undo.clear();
    apply(data);
    m_undo.setClean();
    return true;
}
bool SceneDocument::load(const QString &file, QString *error) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly) || f.size() > maxProjectBytes) {
        if (error)
            *error = tr("Proje okunamadı veya çok büyük.");
        return false;
    }
    QJsonParseError pe;
    const auto bytes = f.read(maxProjectBytes + 1);
    if (bytes.size() > maxProjectBytes) {
        if (error)
            *error = tr("Proje çok büyük.");
        return false;
    }
    auto data = QJsonDocument::fromJson(bytes, &pe);
    if (pe.error != QJsonParseError::NoError || !data.isObject()) {
        if (error)
            *error = tr("Proje JSON biçimi geçersiz.");
        return false;
    }
    auto root = data.object();
    if (!validate(root, error))
        return false;
    auto sources = root["sources"].toArray();
    for (qsizetype i = 0; i < sources.size(); ++i) {
        auto s = sources[i].toObject();
        auto settings = s["settings"].toObject();
        if (s["kind"].toString() == "image")
            settings["file"] =
                QDir(QFileInfo(file).absolutePath()).absoluteFilePath(settings["file"].toString());
        else if (s["kind"].toString() != "text")
            settings["needsSelection"] = true;
        s["settings"] = settings;
        sources[i] = s;
    }
    root["sources"] = sources;
    if (!replace(root, error))
        return false;
    m_path = QFileInfo(file).absoluteFilePath();
    return true;
}
bool SceneDocument::save(const QString &file, QString *error) {
    auto root = m_data;
    auto sources = root["sources"].toArray();
    for (qsizetype i = 0; i < sources.size(); ++i) {
        auto s = sources[i].toObject();
        if (s["kind"].toString() == "image") {
            auto settings = s["settings"].toObject();
            settings["file"] =
                QDir(QFileInfo(file).absolutePath()).relativeFilePath(settings["file"].toString());
            s["settings"] = settings;
            sources[i] = s;
        }
    }
    root["sources"] = sources;
    const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (bytes.size() > maxProjectBytes) {
        if (error)
            *error = tr("Proje çok büyük.");
        return false;
    }
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    if (f.write(bytes) != bytes.size() || !f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    m_path = QFileInfo(file).absoluteFilePath();
    m_undo.setClean();
    return true;
}
} // namespace Cast
