// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QStringList>
#include <QUrl>
#include <memory>
#include <optional>

namespace Cast {
struct LoopbackTarget {
    QUrl server;
    QString key;
    static std::optional<LoopbackTarget> parse(const QString &text);
};

class StreamSession final : public QObject {
    Q_OBJECT
  public:
    enum class State { Connecting, Live, Reconnecting, Stopped, Failed };
    struct TargetStatus {
        State state;
        quint64 bytes;
        int droppedFrames;
    };
    explicit StreamSession(QObject *parent = nullptr);
    ~StreamSession() override;
    static void registerService();
    static bool available();
    bool start(const QStringList &urls, QString *error = nullptr);
    void stop();
    bool running() const;
    QList<TargetStatus> status() const;

  private:
    struct Session;
    std::unique_ptr<Session> m_session;
};
} // namespace Cast
