// SPDX-License-Identifier: GPL-3.0-or-later
#include "stream_session.h"
#include "media_engine.h"
#include <QRegularExpression>
#include <QSet>
#include <atomic>
#include <vector>

namespace Cast {
namespace {
constexpr auto serviceId = "cast_local_rtmp";
constexpr auto outputId = "cast_rtmp_video";
constexpr auto encoderId = "obs_x264";
constexpr auto maxTargets = 4;

struct Service {
    QByteArray server;
    QByteArray key;
};

struct Output {
    obs_output_t *output = nullptr;
    obs_service_t *service = nullptr;
    std::atomic<StreamSession::State> state{StreamSession::State::Connecting};

    static void started(void *instance, calldata_t *) {
        static_cast<Output *>(instance)->state = StreamSession::State::Live;
    }
    static void reconnecting(void *instance, calldata_t *) {
        static_cast<Output *>(instance)->state = StreamSession::State::Reconnecting;
    }
    static void stopped(void *instance, calldata_t *data) {
        static_cast<Output *>(instance)->state = calldata_int(data, "code") == OBS_OUTPUT_SUCCESS
                                                     ? StreamSession::State::Stopped
                                                     : StreamSession::State::Failed;
    }
    void connectSignals() {
        auto *handler = obs_output_get_signal_handler(output);
        signal_handler_connect(handler, "start", started, this);
        signal_handler_connect(handler, "reconnect", reconnecting, this);
        signal_handler_connect(handler, "reconnect_success", started, this);
        signal_handler_connect(handler, "stop", stopped, this);
    }
    ~Output() {
        if (output) {
            auto *handler = obs_output_get_signal_handler(output);
            signal_handler_disconnect(handler, "start", started, this);
            signal_handler_disconnect(handler, "reconnect", reconnecting, this);
            signal_handler_disconnect(handler, "reconnect_success", started, this);
            signal_handler_disconnect(handler, "stop", stopped, this);
            obs_output_release(output);
        }
        obs_service_release(service);
    }
};
} // namespace

struct StreamSession::Session {
    obs_encoder_t *encoder = nullptr;
    std::vector<std::unique_ptr<Output>> targets;
    ~Session() {
        // Initiate all stops before waiting for (or releasing) any one connection.
        for (auto &target : targets)
            if (target->output)
                obs_output_force_stop(target->output);
        targets.clear();
        obs_encoder_release(encoder);
    }
};

std::optional<LoopbackTarget> LoopbackTarget::parse(const QString &text) {
    const QUrl url(text, QUrl::StrictMode);
    static const QRegularExpression targetPath(R"(^/([A-Za-z0-9_-]{1,64})/([A-Za-z0-9_-]{1,64})$)");
    if (text.size() > 200 || !url.isValid() || url.scheme() != "rtmp" ||
        url.host() != "127.0.0.1" || !url.userInfo().isEmpty() || url.hasQuery() ||
        url.hasFragment() || url.port(1935) < 1)
        return std::nullopt;
    const auto match = targetPath.match(url.path(QUrl::FullyEncoded));
    if (!match.hasMatch())
        return std::nullopt;
    QUrl server;
    server.setScheme("rtmp");
    server.setHost("127.0.0.1");
    server.setPort(url.port(1935));
    server.setPath("/" + match.captured(1));
    return LoopbackTarget{server, match.captured(2)};
}

StreamSession::StreamSession(QObject *parent) : QObject(parent) {}
StreamSession::~StreamSession() = default;

void StreamSession::registerService() {
    obs_service_info info{};
    info.id = serviceId;
    info.get_name = [](void *) { return "YoungLion Cast Local RTMP"; };
    info.create = [](obs_data_t *settings, obs_service_t *) -> void * {
        return new Service{obs_data_get_string(settings, "server"),
                           obs_data_get_string(settings, "key")};
    };
    info.destroy = [](void *data) { delete static_cast<Service *>(data); };
    info.get_protocol = [](void *) { return "RTMP"; };
    info.get_connect_info = [](void *data, uint32_t type) -> const char * {
        const auto *service = static_cast<Service *>(data);
        if (type == OBS_SERVICE_CONNECT_INFO_SERVER_URL)
            return service->server.constData();
        if (type == OBS_SERVICE_CONNECT_INFO_STREAM_KEY)
            return service->key.constData();
        return "";
    };
    obs_register_service(&info);
}

bool StreamSession::available() {
    return obs_initialized() && obs_get_video() && obs_output_get_display_name(outputId) &&
           obs_encoder_get_display_name(encoderId);
}

bool StreamSession::start(const QStringList &urls, QString *error) {
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (running())
        return fail(tr("Önce mevcut yayını durdurun."));
    if (!available())
        return fail(tr("Yerel RTMP ve H.264 modülleri yüklenemedi."));
    if (urls.isEmpty() || urls.size() > maxTargets)
        return fail(tr("Bir ile dört arasında yerel yayın hedefi girin."));

    QList<LoopbackTarget> targets;
    QSet<QString> seen;
    for (const auto &url : urls) {
        auto target = LoopbackTarget::parse(url);
        if (!target)
            return fail(tr("Her hedef rtmp://127.0.0.1:1935/live/test biçiminde olmalıdır."));
        const auto identity = target->server.toString(QUrl::FullyEncoded) + "/" + target->key;
        if (seen.contains(identity))
            return fail(tr("Aynı hedefi birden fazla kez eklemeyin."));
        seen.insert(identity);
        targets.append(*target);
    }

    auto session = std::make_unique<Session>();
    auto *settings = obs_data_create();
    obs_data_set_int(settings, "bitrate", 2500);
    obs_data_set_int(settings, "keyint_sec", 2);
    obs_data_set_int(settings, "bf", 0);
    obs_data_set_string(settings, "rate_control", "CBR");
    obs_data_set_string(settings, "preset", "veryfast");
    obs_data_set_string(settings, "tune", "zerolatency");
    session->encoder = obs_video_encoder_create(encoderId, "Cast H.264", settings, nullptr);
    obs_data_release(settings);
    if (!session->encoder)
        return fail(tr("H.264 kodlayıcı oluşturulamadı."));
    obs_encoder_set_preferred_video_format(session->encoder, VIDEO_FORMAT_NV12);
    obs_encoder_set_video(session->encoder, obs_get_video());

    for (const auto &target : targets) {
        auto item = std::make_unique<Output>();
        const auto name =
            QByteArray("Local RTMP ") + QByteArray::number(session->targets.size() + 1);
        settings = obs_data_create();
        obs_data_set_string(settings, "server", target.server.toEncoded().constData());
        obs_data_set_string(settings, "key", target.key.toLatin1().constData());
        item->service = obs_service_create_private(serviceId, name.constData(), settings);
        obs_data_release(settings);
        settings = obs_data_create();
        obs_data_set_int(settings, "drop_threshold", 400);
        obs_data_set_int(settings, "pframe_drop_threshold", 650);
        item->output = obs_output_create(outputId, name.constData(), settings, nullptr);
        obs_data_release(settings);
        if (!item->service || !item->output)
            return fail(tr("RTMP çıkışı oluşturulamadı."));
        item->connectSignals();
        obs_output_set_service(item->output, item->service);
        obs_output_set_video_encoder(item->output, session->encoder);
        obs_output_set_reconnect_settings(item->output, 5, 2);
        session->targets.push_back(std::move(item));
    }

    bool started = false;
    for (auto &target : session->targets) {
        if (obs_output_start(target->output))
            started = true;
        else
            target->state = State::Failed;
    }
    if (!started)
        return fail(tr("Yerel RTMP çıkışları başlatılamadı."));
    m_session = std::move(session);
    return true;
}

bool StreamSession::running() const {
    return bool(m_session);
}
void StreamSession::stop() {
    m_session.reset();
}
QList<StreamSession::TargetStatus> StreamSession::status() const {
    QList<TargetStatus> rows;
    if (m_session)
        for (const auto &target : m_session->targets)
            rows.append({target->state.load(), obs_output_get_total_bytes(target->output),
                         obs_output_get_frames_dropped(target->output)});
    return rows;
}
} // namespace Cast
