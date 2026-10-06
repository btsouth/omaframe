#include "window-export.hpp"

#include "xdg-foreign-unstable-v2-client-protocol.h"

#include <QGuiApplication>
#include <QObject>
#include <QTimer>
#include <QWindow>
#include <qpa/qplatformnativeinterface.h>

#include <wayland-client.h>

namespace FilePicks {

QString portalParent(const QString &handle) {
  return handle.isEmpty() ? QString() : "wayland:" + handle;
}

namespace {
// The chooser should appear well within this; past it, an unparented dialog is
// better than a button that seems dead.
constexpr int kHandleWaitMs = 200;
constexpr int kPollMs = 4;

// Talks on Qt's connection (a surface can only be exported by the client that
// owns it) but on a queue of its own, which a short timer dispatches while a
// request is waiting: Qt reads the socket and sorts events into the queues.
class WaylandExporter : public QObject, public WindowExporter {
public:
  WaylandExporter() {
    m_poll.setInterval(kPollMs);
    m_poll.setTimerType(Qt::PreciseTimer);
    m_deadline.setSingleShot(true);
    m_deadline.setInterval(kHandleWaitMs);
    connect(&m_poll, &QTimer::timeout, this, [this] { step(); });
    connect(&m_deadline, &QTimer::timeout, this, [this] { finish({}); });
  }
  ~WaylandExporter() override {
    release();
    if (m_sync)
      wl_callback_destroy(m_sync);
    if (m_exporter)
      zxdg_exporter_v2_destroy(m_exporter);
    if (m_registry)
      wl_registry_destroy(m_registry);
    if (m_wrapper)
      wl_proxy_wrapper_destroy(m_wrapper);
    if (m_queue)
      wl_event_queue_destroy(m_queue);
  }

  void exportWindow(QWindow *window, Ready ready) override {
    release();
    finish({});
    m_ready = std::move(ready);
    m_surface = surfaceOf(window);
    auto *app = qGuiApp
        ? qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>()
        : nullptr;
    wl_display *display = app ? app->display() : nullptr;
    if (!m_surface || !display || !connectTo(display)) {
      finish({});
      return;
    }
    m_waiting = true;
    m_deadline.start();
    m_poll.start();
    step();
  }

  void release() override {
    if (m_exported) {
      zxdg_exported_v2_destroy(m_exported);
      m_exported = nullptr;
      if (m_display)
        wl_display_flush(m_display);
    }
  }

private:
  static wl_surface *surfaceOf(QWindow *window) {
    if (!window || !window->isVisible() || !QGuiApplication::platformNativeInterface())
      return nullptr;
    return static_cast<wl_surface *>(
        QGuiApplication::platformNativeInterface()->nativeResourceForWindow(
            "surface", window));
  }

  bool connectTo(wl_display *display) {
    if (m_display == display)
      return true;
    if (m_display)
      return false;
    m_queue = wl_display_create_queue(display);
    m_wrapper = static_cast<wl_proxy *>(wl_proxy_create_wrapper(display));
    if (!m_queue || !m_wrapper)
      return false;
    wl_proxy_set_queue(m_wrapper, m_queue);
    m_display = display;
    m_registry = wl_display_get_registry(reinterpret_cast<wl_display *>(m_wrapper));
    static const wl_registry_listener registryListener{
        [](void *data, wl_registry *registry, uint32_t name,
           const char *interface, uint32_t) {
          auto *self = static_cast<WaylandExporter *>(data);
          if (!self->m_exporter &&
              qstrcmp(interface, zxdg_exporter_v2_interface.name) == 0)
            self->m_exporter = static_cast<zxdg_exporter_v2 *>(wl_registry_bind(
                registry, name, &zxdg_exporter_v2_interface, 1));
        },
        [](void *, wl_registry *, uint32_t) {}};
    wl_registry_add_listener(m_registry, &registryListener, this);
    // Every global is announced before this callback, so a missing exporter is
    // known for good once it fires.
    m_sync = wl_display_sync(reinterpret_cast<wl_display *>(m_wrapper));
    static const wl_callback_listener syncListener{
        [](void *data, wl_callback *callback, uint32_t) {
          auto *self = static_cast<WaylandExporter *>(data);
          self->m_globalsKnown = true;
          self->m_sync = nullptr;
          wl_callback_destroy(callback);
        }};
    wl_callback_add_listener(m_sync, &syncListener, this);
    return true;
  }

  void step() {
    if (!m_waiting)
      return;
    wl_display_dispatch_queue_pending(m_display, m_queue);
    if (m_waiting && m_exporter && !m_exported) {
      m_exported = zxdg_exporter_v2_export_toplevel(m_exporter, m_surface);
      static const zxdg_exported_v2_listener exportedListener{
          [](void *data, zxdg_exported_v2 *, const char *handle) {
            auto *self = static_cast<WaylandExporter *>(data);
            QMetaObject::invokeMethod(
                self, [self, handle = QString::fromUtf8(handle)] {
                  if (self->m_waiting)
                    self->finish(handle);
                }, Qt::QueuedConnection);
          }};
      zxdg_exported_v2_add_listener(m_exported, &exportedListener, this);
    }
    wl_display_flush(m_display);
    if (m_waiting && m_globalsKnown && !m_exporter)
      finish({});
  }

  void finish(const QString &handle) {
    m_poll.stop();
    m_deadline.stop();
    m_waiting = false;
    auto ready = std::move(m_ready);
    m_ready = nullptr;
    // An export that did not deliver in time is not wanted any more.
    if (handle.isEmpty())
      release();
    if (ready)
      ready(handle);
  }

  QTimer m_poll, m_deadline;
  Ready m_ready;
  wl_display *m_display = nullptr;
  wl_event_queue *m_queue = nullptr;
  wl_proxy *m_wrapper = nullptr;
  wl_registry *m_registry = nullptr;
  wl_callback *m_sync = nullptr;
  zxdg_exporter_v2 *m_exporter = nullptr;
  zxdg_exported_v2 *m_exported = nullptr;
  wl_surface *m_surface = nullptr;
  bool m_waiting = false, m_globalsKnown = false;
};
} // namespace

std::unique_ptr<WindowExporter> waylandWindowExporter() {
  return std::make_unique<WaylandExporter>();
}

} // namespace FilePicks
