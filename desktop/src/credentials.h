#pragma once
#include <QObject>
#include <QQueue>
#include <functional>
namespace Cast {
// Serialize OS-vault operations: logout's delete always follows an in-flight token write.
class Credentials final : public QObject {
    Q_OBJECT
  public:
    explicit Credentials(QString key, QObject *parent = nullptr)
        : QObject(parent), m_key(std::move(key)) {}
    using Done = std::function<void(bool, const QString &)>;
    void read(Done done);
    void write(const QString &value, Done done);
    void clear(Done done);

  private:
    struct Operation {
        int type;
        QString value;
        Done done;
    };
    QString m_key;
    QQueue<Operation> m_queue;
    bool m_busy = false;
    void run();
};
} // namespace Cast
