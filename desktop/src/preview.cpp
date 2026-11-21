#include "preview.h"
#include "media_engine.h"
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPlatformSurfaceEvent>
#include <QWindow>
#ifdef Q_OS_LINUX
#include <obs-nix-platform.h>
#endif
namespace Cast {
Preview::Preview(MediaEngine *engine, QWidget *parent) : QWidget(parent), m_engine(engine) {
    setObjectName("gpuPreview");
    setMinimumSize(480, 270);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NativeWindow);
    setFocusPolicy(Qt::StrongFocus);
}
Preview::~Preview() {
    destroy();
}
void Preview::destroy() {
    if (m_display) {
        obs_display_destroy(m_display);
        m_display = nullptr;
    }
}
void Preview::create() {
    if (m_display || !m_engine->ready() || !isVisible() || !windowHandle() ||
        !windowHandle()->isExposed())
        return;
    gs_init_data info{};
    info.cx = uint32_t(width() * devicePixelRatioF());
    info.cy = uint32_t(height() * devicePixelRatioF());
    info.format = GS_BGRA;
    info.zsformat = GS_ZS_NONE;
#ifdef Q_OS_WIN
    info.window.hwnd = reinterpret_cast<void *>(winId());
#elif defined(Q_OS_MACOS)
    info.window.view = reinterpret_cast<id>(winId());
#else
    if (obs_get_nix_platform() == OBS_NIX_PLATFORM_WAYLAND)
        info.window.display = reinterpret_cast<void *>(winId());
    else {
        info.window.id = uint32_t(winId());
        info.window.display = obs_get_nix_platform_display();
    }
#endif
    m_display = obs_display_create(&info, 0xFF151210);
    if (m_display) {
        obs_display_add_draw_callback(
            m_display,
            [](void *p, uint32_t w, uint32_t h) { static_cast<MediaEngine *>(p)->draw(w, h); },
            m_engine);
        obs_display_set_enabled(m_display, !m_suspended);
    }
}
void Preview::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
    create();
}
void Preview::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    if (m_display)
        obs_display_resize(m_display, uint32_t(width() * devicePixelRatioF()),
                           uint32_t(height() * devicePixelRatioF()));
    else
        create();
}
void Preview::paintEvent(QPaintEvent *) {
    create();
}
bool Preview::event(QEvent *e) {
    if (e->type() == QEvent::PlatformSurface &&
        static_cast<QPlatformSurfaceEvent *>(e)->surfaceEventType() ==
            QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
        destroy();
    return QWidget::event(e);
}
void Preview::setSuspended(bool s) {
    m_suspended = s;
    if (m_display)
        obs_display_set_enabled(m_display, !s);
}
void Preview::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton) {
        m_start = e->position();
        m_dragging = true;
    }
}
void Preview::mouseReleaseEvent(QMouseEvent *e) {
    if (m_dragging && e->button() == Qt::LeftButton) {
        m_dragging = false;
        const auto canvas = m_engine->canvas();
        const double scale =
            qMin(double(width()) / canvas.width(), double(height()) / canvas.height());
        if (scale > 0)
            emit moved((e->position() - m_start) / scale);
    }
}
} // namespace Cast
