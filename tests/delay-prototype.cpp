#include "capture-dismissal.hpp"
#include "capture.hpp"
#include <LayerShellQt/Window>
#include <QDir>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <cstdio>
int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);
  QString error;
  if (!CaptureDismissal::prepare(error)) {
    fprintf(stderr, "%s\n", qPrintable(error));
    return 1;
  }
  QQuickWindow badge;
  badge.setFlags(Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
  badge.setColor(QColor("#ff00ff"));
  badge.resize(360, 64);
  auto *screen = app.primaryScreen();
  auto *layer = LayerShellQt::Window::get(&badge);
  layer->setScope(CaptureDismissal::scope);
  layer->setLayer(LayerShellQt::Window::LayerOverlay);
  layer->setExclusiveZone(-1);
  layer->setKeyboardInteractivity(
      LayerShellQt::Window::KeyboardInteractivityNone);
  layer->setAnchors(LayerShellQt::Window::AnchorTop);
  layer->setMargins(QMargins(0, 64, 0, 0));
  layer->setScreen(screen);
  badge.setScreen(screen);
  const QString folder = QDir::homePath() + "/delay-prototype";
  QDir().mkpath(folder);
  MonitorInfo monitor;
  monitor.name = screen->name();
  QImage reference;
  if (!captureOutputSurface(monitor, reference, error))
    return 2;
  reference.save(folder + "/reference.png");
  badge.show();
  QTimer::singleShot(1000, &app, [&] {
    QImage visible;
    if (!captureOutputSurface(monitor, visible, error)) {
      app.exit(3);
      return;
    }
    visible.save(folder + "/visible.png");
    CaptureDismissal::clear(&badge, &app, [&](bool gone) {
      QImage captured;
      if (!gone || !captureOutputSurface(monitor, captured, error)) {
        app.exit(4);
        return;
      }
      captured.save(folder + "/captured.png");
      const QRect region((screen->size().width() - 360) / 2, 64, 360, 64);
      const bool equal = reference.copy(region) == captured.copy(region);
      const bool shown = visible.copy(region) != reference.copy(region);
      fprintf(stderr,
              "badge shown=%d; post-dismissal badge region identical=%d; "
              "refresh=%.2f\n",
              shown, equal, screen->refreshRate());
      app.exit(equal && shown ? 0 : 5);
    });
  });
  return app.exec();
}
