#include "media_engine.h"
#include "stream_session.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QStandardPaths>
#include <QtGui/qguiapplication_platform.h>
#include <cmath>
#include <graphics/vec2.h>
#ifdef Q_OS_LINUX
#include <obs-nix-platform.h>
#endif
namespace Cast {
static obs_data_t *data(const QJsonObject &json) {
    return obs_data_create_from_json(
        QJsonDocument(json).toJson(QJsonDocument::Compact).constData());
}
MediaEngine::MediaEngine(QObject *p) : QObject(p) {}
MediaEngine::~MediaEngine() {
    if (m_scene) {
        obs_set_output_source(0, nullptr);
        obs_scene_release(m_scene);
    }
    for (auto &s : m_sources)
        if (s.handle)
            obs_source_release(s.handle);
    if (m_started)
        obs_shutdown();
}
QString MediaEngine::platform() const {
#ifdef Q_OS_WIN
    return "windows";
#elif defined(Q_OS_MACOS)
    return "macos";
#else
    return QGuiApplication::platformName() == "wayland" ? "wayland"
           : QGuiApplication::platformName() == "xcb"   ? "x11"
                                                        : "unknown";
#endif
}
QString MediaEngine::captureId(const QString &kind, const QString &p) {
    if (kind == "text")
        return "text_ft2_source";
    if (kind == "image")
        return "image_source";
    if (p == "windows") {
        if (kind == "game")
            return "game_capture";
        if (kind == "window")
            return "window_capture";
        if (kind == "display")
            return "monitor_capture";
    }
    if (p == "macos" && (kind == "window" || kind == "display" || kind == "application"))
        return "screen_capture";
    if (p == "x11") {
        if (kind == "window")
            return "xcomposite_input";
        if (kind == "display")
            return "xshm_input_v2";
    }
    if (p == "wayland") {
        if (kind == "window")
            return "pipewire-window-capture-source";
        if (kind == "display")
            return "pipewire-desktop-capture-source";
    }
    return {};
}
QJsonObject MediaEngine::safeSettings(const QString &kind, QJsonObject s) {
    // A window/game source must never silently widen to all fullscreen applications.
    if (kind == "game")
        s["capture_mode"] = "window";
    if (kind == "window" || kind == "game") {
        s["fallback"] = false;
        s["priority"] = 1;
        s["capture_audio"] = false;
    }
    s["capture_audio"] = false;
    return s;
}
QString MediaEngine::sourceType(const QString &kind) const {
    return captureId(kind, platform());
}
bool MediaEngine::supported(const QString &kind) const {
    const auto id = sourceType(kind).toUtf8();
    return m_ready && !id.isEmpty() && obs_source_get_display_name(id.constData());
}
bool MediaEngine::initialize(QString *failure) {
    auto fail = [&](const QString &s) {
        if (failure)
            *failure = s;
        return false;
    };
    if (m_started)
        return m_ready;
#ifdef Q_OS_LINUX
    if (platform() == "unknown")
        return fail(tr("GPU önizlemesi için X11 veya Wayland oturumu gerekli."));
    obs_set_nix_platform(platform() == "wayland" ? OBS_NIX_PLATFORM_WAYLAND
                                                 : OBS_NIX_PLATFORM_X11_EGL);
    if (auto *x = qGuiApp->nativeInterface<QNativeInterface::QX11Application>())
        obs_set_nix_platform_display(x->display());
    if (auto *w = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>())
        obs_set_nix_platform_display(w->display());
#endif
    const auto config =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/obs";
    QDir().mkpath(config);
    if (!obs_startup("tr-TR", config.toUtf8().constData(), nullptr))
        return fail(tr("libobs başlatılamadı."));
    m_started = true;
    const auto dir = QCoreApplication::applicationDirPath();
    obs_add_data_path((dir + "/../share/obs/libobs").toUtf8().constData());
    obs_add_data_path((dir + "/../Frameworks/libobs.framework/Resources").toUtf8().constData());
    obs_add_data_path((dir + "/../data/libobs").toUtf8().constData());
    obs_add_data_path(CAST_OBS_DATA_DIR);
    obs_video_info v{};
#ifdef Q_OS_WIN
    v.graphics_module = "libobs-d3d11";
#else
    v.graphics_module = "libobs-opengl";
#endif
    v.fps_num = m_fps;
    v.fps_den = 1;
    v.base_width = v.output_width = m_canvas.width();
    v.base_height = v.output_height = m_canvas.height();
    v.output_format = VIDEO_FORMAT_RGBA;
    v.colorspace = VIDEO_CS_709;
    v.range = VIDEO_RANGE_FULL;
    v.scale_type = OBS_SCALE_BILINEAR;
    if (obs_reset_video(&v) != OBS_VIDEO_SUCCESS)
        return fail(tr("GPU görüntü motoru başlatılamadı. Ekran sürücüsünü kontrol edin."));
    // Load just the editor and local streaming modules.
    for (const auto name :
         {"image-source", "text-freetype2", "obs-filters", "linux-capture", "linux-pipewire",
          "win-capture", "mac-capture", "obs-outputs", "obs-x264"}) {
#ifdef Q_OS_WIN
        const auto filename = QString(name) + "/" + name + ".dll";
#elif defined(Q_OS_MACOS)
        const auto filename = QString(name) + ".plugin/Contents/MacOS/" + name;
#else
        const auto filename = QString(name) + ".so";
#endif
        const auto packagedPlugins = QString(
#if defined(Q_OS_MACOS)
            dir + "/../PlugIns"
#elif defined(Q_OS_WIN)
            dir + "/../core"
#else
            dir + "/../lib/obs-modules/core"
#endif
        );
        for (const auto &base : {packagedPlugins, QString(CAST_OBS_PLUGIN_DIR)}) {
            const auto path = base + "/" + filename;
            if (!QFileInfo::exists(path))
                continue;
            obs_module_t *module = nullptr;
#ifdef Q_OS_MACOS
            const auto assets = base + "/" + name + ".plugin/Contents/Resources";
#else
            const auto assets = base == packagedPlugins
                                    ? dir + "/../share/obs/obs-modules/core/" + name
                                    : QString(CAST_OBS_PLUGIN_DATA_DIR).replace("%module%", name);
#ifdef Q_OS_WIN
            const auto winAssets = base + "/" + name + "/data";
#else
            const auto &winAssets = assets;
#endif
#endif
#ifdef Q_OS_WIN
            const auto &dataPath = winAssets;
#else
            const auto &dataPath = assets;
#endif
            if (obs_open_module(&module, path.toUtf8().constData(),
                                dataPath.toUtf8().constData()) == MODULE_SUCCESS)
                obs_init_module(module);
            break;
        }
    }
    obs_post_load_modules();
    StreamSession::registerService();
    m_scene = obs_scene_create_private("YoungLion preview");
    if (!m_scene)
        return fail(tr("Sahne oluşturulamadı."));
    obs_set_output_source(0, obs_scene_get_source(m_scene));
    m_ready = true;
    return true;
}
QJsonObject MediaEngine::defaults(const QString &kind) const {
    auto *d = obs_get_source_defaults(sourceType(kind).toUtf8().constData());
    auto json = d ? QJsonDocument::fromJson(obs_data_get_json(d)).object() : QJsonObject{};
    if (d)
        obs_data_release(d);
    if (platform() == "macos")
        json["type"] = kind == "window" ? 1 : kind == "application" ? 2 : 0;
    return safeSettings(kind, json);
}
#ifdef Q_OS_MACOS
QList<SourceProperty> macCaptureProperties(const QString &kind);
#endif
bool MediaEngine::captureTargetSelected(const QString &kind, const QJsonObject &settings) const {
    if (kind == "image" || kind == "text" || platform() == "wayland")
        return true;
    const auto p = platform();
    if (p == "macos")
        return kind == "window"
                   ? settings["window"].toInt() > 0
                   : !settings["display_uuid"].toString().isEmpty() &&
                         (kind != "application" || !settings["application"].toString().isEmpty());
    if (p == "x11")
        return kind == "window" ? !settings["capture_window"].toString().isEmpty()
                                : settings["screen"].toInt(-1) >= 0;
    if (p == "windows")
        return kind == "display" ? (!settings["monitor_id"].toString().isEmpty() ||
                                    settings["monitor"].toInt(-1) >= 0)
                                 : !settings["window"].toString().isEmpty();
    return false;
}
QList<SourceProperty> MediaEngine::properties(const QString &id, const QString &kind) {
    QList<SourceProperty> result;
    auto it = m_sources.find(id);
    auto *handle = it == m_sources.end() ? nullptr : it->handle;
#ifdef Q_OS_MACOS
    if (!handle && !kind.isEmpty())
        return macCaptureProperties(kind);
#endif
    auto *props = handle ? obs_source_properties(handle)
                         : (supported(kind) && platform() != "wayland"
                                ? obs_get_source_properties(sourceType(kind).toUtf8().constData())
                                : nullptr);
    if (!props)
        return result;
    for (auto *p = obs_properties_first(props); p; obs_property_next(&p)) {
        if (!obs_property_visible(p) &&
            !(kind == "game" && qstrcmp(obs_property_name(p), "window") == 0))
            continue;
        SourceProperty field{QString::fromUtf8(obs_property_name(p)),
                             QString::fromUtf8(obs_property_description(p)),
                             int(obs_property_get_type(p))};
        // Capture mode and match policy are fixed by the source kind; never widen it in the
        // property UI.
        if (QStringList{"capture_mode", "priority", "type", "capture_audio", "fallback"}.contains(
                field.key))
            continue;
        if (field.type == OBS_PROPERTY_LIST) {
            for (size_t i = 0; i < obs_property_list_item_count(p); ++i) {
                if (obs_property_list_item_disabled(p, i))
                    continue;
                QJsonValue v;
                switch (obs_property_list_format(p)) {
                case OBS_COMBO_FORMAT_STRING:
                    v = QString::fromUtf8(obs_property_list_item_string(p, i));
                    break;
                case OBS_COMBO_FORMAT_INT:
                    v = double(obs_property_list_item_int(p, i));
                    break;
                case OBS_COMBO_FORMAT_FLOAT:
                    v = obs_property_list_item_float(p, i);
                    break;
                default:
                    continue;
                }
                field.options.append({QString::fromUtf8(obs_property_list_item_name(p, i)), v});
            }
        }
        if (field.type == OBS_PROPERTY_INT) {
            field.minimum = obs_property_int_min(p);
            field.maximum = obs_property_int_max(p);
        }
        if (field.type == OBS_PROPERTY_BOOL || field.type == OBS_PROPERTY_INT ||
            field.type == OBS_PROPERTY_LIST || field.type == OBS_PROPERTY_BUTTON)
            result.append(field);
    }
    obs_properties_destroy(props);
    return result;
}
void MediaEngine::propertyButton(const QString &id, const QString &key) {
    auto it = m_sources.find(id);
    if (it == m_sources.end() || !it->handle)
        return;
    auto *props = obs_source_properties(it->handle);
    if (props) {
        auto *p = obs_properties_get(props, key.toUtf8().constData());
        if (p && obs_property_get_type(p) == OBS_PROPERTY_BUTTON)
            obs_property_button_clicked(p, it->handle);
        obs_properties_destroy(props);
    }
}
void MediaEngine::sync(const QJsonObject &doc) {
    if (!m_ready)
        return;
    const auto canvas = doc["canvas"].toObject();
    const QSize size(canvas["width"].toInt(), canvas["height"].toInt());
    const int fps = canvas["fps"].toInt();
    if (size != m_canvas || fps != m_fps) {
        obs_video_info v{};
        obs_get_video_info(&v);
        v.base_width = v.output_width = size.width();
        v.base_height = v.output_height = size.height();
        v.fps_num = fps;
        if (obs_reset_video(&v) == OBS_VIDEO_SUCCESS) {
            m_canvas = size;
            m_fps = fps;
        } else
            emit error(tr("Tuval boyutu değiştirilemedi."));
    }
    QSet<QString> present, active;
    for (const auto &scene : doc["scenes"].toArray())
        if (scene.toObject()["id"] == doc["activeScene"])
            for (const auto &item : scene.toObject()["items"].toArray())
                active.insert(item.toObject()["sourceId"].toString());
    for (const auto &value : doc["sources"].toArray()) {
        const auto spec = value.toObject();
        const auto id = spec["id"].toString(), kind = spec["kind"].toString();
        if (!active.contains(id))
            continue;
        present.insert(id);
        auto &s = m_sources[id];
        if (s.spec == spec)
            continue;
        const auto settings = spec["settings"].toObject();
        if (s.handle && s.spec["kind"] != spec["kind"]) {
            obs_source_release(s.handle);
            s.handle = nullptr;
        }
        if (settings["needsSelection"].toBool() ||
            (supported(kind) && !captureTargetSelected(kind, settings))) {
            s.error = tr("Yeniden yakalama hedefi seçin.");
            if (s.handle) {
                obs_source_release(s.handle);
                s.handle = nullptr;
            }
            s.spec = spec;
            continue;
        }
        if (!supported(kind)) {
            s.error = tr("Bu kaynak türü bu oturumda kullanılamıyor.");
            s.spec = spec;
            continue;
        }
        if (kind == "image" && !QFileInfo::exists(settings["file"].toString())) {
            s.error = tr("Görsel bulunamadı. Dosyayı yeniden seçin.");
            if (s.handle) {
                obs_source_release(s.handle);
                s.handle = nullptr;
            }
            s.spec = spec;
            continue;
        }
        auto safe = safeSettings(kind, settings);
        if (kind == "image")
            safe = QJsonObject{{"file", settings["file"]}};
        if (kind == "text")
            safe = QJsonObject{{"text", settings["text"]}};
        if (platform() == "wayland")
            safe.remove("RestoreToken");
        if (platform() == "macos" && kind != "text" && kind != "image")
            safe["type"] = kind == "window" ? 1 : kind == "application" ? 2 : 0;
        if (kind == "text") {
            QJsonObject font{{"face", settings["fontFace"].toString("Sans Serif")},
                             {"size", settings["fontSize"].toInt(48)},
                             {"flags", 0}};
            safe["font"] = font;
            safe["from_file"] = false;
            safe["color1"] = settings["color"].toDouble(0xffffffff);
            safe["color2"] = safe["color1"];
        }
        auto *d = data(safe);
        if (s.handle)
            obs_source_update(s.handle, d);
        else
            s.handle = obs_source_create_private(sourceType(kind).toUtf8().constData(),
                                                 id.toUtf8().constData(), d);
        obs_data_release(d);
        s.error = s.handle ? QString{} : tr("Kaynak oluşturulamadı.");
        s.spec = spec;
    }
    std::pair<MediaEngine *, QJsonObject> context(this, doc);
    obs_scene_atomic_update(
        m_scene,
        [](void *raw, obs_scene_t *scene) {
            auto *self = static_cast<std::pair<MediaEngine *, QJsonObject> *>(raw);
            auto *engine = self->first;
            const auto doc = self->second;
            QSet<QString> keep;
            for (const auto &sceneSpec : doc["scenes"].toArray()) {
                if (sceneSpec.toObject()["id"] != doc["activeScene"])
                    continue;
                for (const auto &value : sceneSpec.toObject()["items"].toArray()) {
                    const auto item = value.toObject();
                    const auto id = item["id"].toString();
                    const auto *existing = engine->m_items.value(id);
                    const auto source = engine->m_sources.constFind(item["sourceId"].toString());
                    if (existing && source != engine->m_sources.cend() &&
                        obs_sceneitem_get_source(existing) == source->handle)
                        keep.insert(id);
                }
            }
            for (auto it = engine->m_items.begin(); it != engine->m_items.end();) {
                if (keep.contains(it.key())) {
                    ++it;
                    continue;
                }
                obs_sceneitem_remove(it.value());
                engine->m_itemSpecs.remove(it.key());
                it = engine->m_items.erase(it);
            }
            int order = 0;
            for (const auto &sv : doc["scenes"].toArray()) {
                const auto s = sv.toObject();
                if (s["id"] != doc["activeScene"])
                    continue;
                for (const auto &iv : s["items"].toArray()) {
                    const auto item = iv.toObject();
                    auto &source = engine->m_sources[item["sourceId"].toString()];
                    if (!source.handle)
                        continue;
                    const auto id = item["id"].toString();
                    auto *si = engine->m_items.value(id);
                    if (!si) {
                        si = obs_scene_add(scene, source.handle);
                        if (!si)
                            continue;
                        engine->m_items.insert(id, si);
                    }
                    if (obs_sceneitem_get_order_position(si) != order)
                        obs_sceneitem_set_order_position(si, order);
                    ++order;
                    if (engine->m_itemSpecs.value(id) == item)
                        continue;
                    const auto old = engine->m_itemSpecs.value(id);
                    engine->m_itemSpecs.insert(id, item);
                    vec2 pos{float(item["x"].toDouble()), float(item["y"].toDouble())},
                        scale{float(item["scaleX"].toDouble()), float(item["scaleY"].toDouble())};
                    obs_sceneitem_set_pos(si, &pos);
                    obs_sceneitem_set_scale(si, &scale);
                    obs_sceneitem_set_rot(si, float(item["rotation"].toDouble()));
                    obs_sceneitem_set_visible(si, item["visible"].toBool());
                    obs_sceneitem_set_locked(si, item["locked"].toBool());
                    obs_sceneitem_crop crop{item["cropLeft"].toInt(), item["cropTop"].toInt(),
                                            item["cropRight"].toInt(), item["cropBottom"].toInt()};
                    obs_sceneitem_set_crop(si, &crop);
                    if (old["opacity"] == item["opacity"])
                        continue;
                    auto *filter =
                        obs_source_get_filter_by_name(source.handle, "YoungLion opacity");
                    if (!filter) {
                        filter = obs_source_create_private("color_filter_v2", "YoungLion opacity",
                                                           nullptr);
                        if (filter)
                            obs_source_filter_add(source.handle, filter);
                    }
                    if (filter) {
                        auto *d = obs_data_create();
                        obs_data_set_double(d, "opacity", item["opacity"].toDouble());
                        obs_source_update(filter, d);
                        obs_data_release(d);
                        obs_source_release(filter);
                    }
                }
            }
        },
        &context);
    // Removed sources are released only after their scene references have been removed.
    for (auto it = m_sources.begin(); it != m_sources.end();)
        if (!present.contains(it.key())) {
            if (it->handle)
                obs_source_release(it->handle);
            it = m_sources.erase(it);
        } else
            ++it;
}
QString MediaEngine::status(const QString &id) const {
    auto it = m_sources.constFind(id);
    if (it == m_sources.end())
        return tr("Kaynak bekleniyor.");
    if (!it->error.isEmpty())
        return it->error;
    if (it->handle && (!obs_source_get_width(it->handle) || !obs_source_get_height(it->handle)))
        return tr("Hedef bekleniyor; izin, pencere veya oyun durumunu kontrol edin.");
    return tr("Hazır");
}
QSize MediaEngine::sourceSize(const QString &id) const {
    auto it = m_sources.constFind(id);
    return it != m_sources.end() && it->handle
               ? QSize(obs_source_get_width(it->handle), obs_source_get_height(it->handle))
               : QSize{};
}
QImage MediaEngine::readback() {
    if (!m_ready)
        return {};
    obs_enter_graphics();
    auto *texture = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    QImage image;
    if (gs_texrender_begin(texture, m_canvas.width(), m_canvas.height())) {
        vec4 clear{};
        gs_clear(GS_CLEAR_COLOR, &clear, 0, 0);
        gs_ortho(0, float(m_canvas.width()), 0, float(m_canvas.height()), -100, 100);
        obs_source_video_render(obs_scene_get_source(m_scene));
        gs_texrender_end(texture);
        auto *surface = gs_stagesurface_create(m_canvas.width(), m_canvas.height(), GS_RGBA);
        if (surface) {
            gs_stage_texture(surface, gs_texrender_get_texture(texture));
            uint8_t *bytes = nullptr;
            uint32_t stride = 0;
            if (gs_stagesurface_map(surface, &bytes, &stride)) {
                image = QImage(bytes, m_canvas.width(), m_canvas.height(), int(stride),
                               QImage::Format_RGBA8888)
                            .copy();
                gs_stagesurface_unmap(surface);
            }
            gs_stagesurface_destroy(surface);
        }
    }
    gs_texrender_destroy(texture);
    obs_leave_graphics();
    return image;
}
void MediaEngine::setSelection(const QJsonObject &item) {
    QPolygonF polygon;
    const auto size = sourceSize(item["sourceId"].toString());
    if (!item.isEmpty() && item["visible"].toBool() && !size.isEmpty()) {
        const double w =
            qMax(0, size.width() - item["cropLeft"].toInt() - item["cropRight"].toInt()) *
            item["scaleX"].toDouble();
        const double h =
            qMax(0, size.height() - item["cropTop"].toInt() - item["cropBottom"].toInt()) *
            item["scaleY"].toDouble();
        const double angle = item["rotation"].toDouble() * 3.141592653589793 / 180;
        for (const auto &p :
             {QPointF(0, 0), QPointF(w, 0), QPointF(w, h), QPointF(0, h), QPointF(0, 0)})
            polygon.append(
                QPointF(item["x"].toDouble() + p.x() * std::cos(angle) - p.y() * std::sin(angle),
                        item["y"].toDouble() + p.x() * std::sin(angle) + p.y() * std::cos(angle)));
    }
    QMutexLocker locker(&m_selectionMutex);
    m_selection = polygon;
}
void MediaEngine::draw(uint32_t w, uint32_t h) {
    obs_video_info v{};
    if (!obs_get_video_info(&v) || !v.base_width || !v.base_height)
        return;
    const double scale = qMin(double(w) / v.base_width, double(h) / v.base_height);
    const int cx = int(v.base_width * scale), cy = int(v.base_height * scale);
    gs_viewport_push();
    gs_projection_push();
    gs_set_viewport((int(w) - cx) / 2, (int(h) - cy) / 2, cx, cy);
    gs_ortho(0, float(v.base_width), 0, float(v.base_height), -100, 100);
    obs_render_main_texture();
    QPolygonF selection;
    {
        QMutexLocker locker(&m_selectionMutex);
        selection = m_selection;
    }
    if (!selection.isEmpty()) {
        auto *effect = obs_get_base_effect(OBS_EFFECT_SOLID);
        vec4 color{0.45f, 0.67f, 1.0f, 1.0f};
        gs_effect_set_vec4(gs_effect_get_param_by_name(effect, "color"), &color);
        while (gs_effect_loop(effect, "Solid")) {
            gs_render_start(true);
            for (const auto &point : selection)
                gs_vertex2f(float(point.x()), float(point.y()));
            gs_render_stop(GS_LINESTRIP);
        }
    }
    gs_projection_pop();
    gs_viewport_pop();
}
} // namespace Cast
