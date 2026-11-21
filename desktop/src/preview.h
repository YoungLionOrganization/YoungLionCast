#pragma once
#include <QWidget>
#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif
#include <obs.h>
namespace Cast {
class MediaEngine;
class Preview final : public QWidget {
    Q_OBJECT
  public:
    explicit Preview(MediaEngine *engine, QWidget *parent = nullptr);
    ~Preview() override;
    QPaintEngine *paintEngine() const override {
        return nullptr;
    }
    void setSuspended(bool value);
  signals:
    void moved(QPointF delta);

  protected:
    void showEvent(QShowEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void paintEvent(QPaintEvent *) override;
    bool event(QEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

  private:
    void create();
    void destroy();
    MediaEngine *m_engine;
    obs_display_t *m_display = nullptr;
    QPointF m_start;
    bool m_dragging = false, m_suspended = false;
};
} // namespace Cast
