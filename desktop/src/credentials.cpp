#include "credentials.h"
#include <keychain.h>
namespace Cast {
void Credentials::read(Done done) {
    m_queue.enqueue({0, {}, std::move(done)});
    run();
}
void Credentials::write(const QString &value, Done done) {
    m_queue.enqueue({1, value, std::move(done)});
    run();
}
void Credentials::clear(Done done) {
    m_queue.enqueue({2, {}, std::move(done)});
    run();
}
void Credentials::run() {
    if (m_busy || m_queue.isEmpty())
        return;
    m_busy = true;
    auto op = m_queue.dequeue();
    QKeychain::Job *job = nullptr;
    if (op.type == 0)
        job = new QKeychain::ReadPasswordJob("YoungLion Cast", this);
    else if (op.type == 1) {
        auto *w = new QKeychain::WritePasswordJob("YoungLion Cast", this);
        w->setTextData(op.value);
        job = w;
    } else
        job = new QKeychain::DeletePasswordJob("YoungLion Cast", this);
    job->setKey(m_key);
    job->setInsecureFallback(false);
    job->setAutoDelete(true);
    connect(job, &QKeychain::Job::finished, this, [this, op](QKeychain::Job *j) {
        const bool okay = j->error() == QKeychain::NoError ||
                          (op.type != 1 && j->error() == QKeychain::EntryNotFound);
        QString value;
        if (op.type == 0 && okay)
            value = static_cast<QKeychain::ReadPasswordJob *>(j)->textData();
        op.done(okay, value);
        m_busy = false;
        run();
    });
    job->start();
}
} // namespace Cast
