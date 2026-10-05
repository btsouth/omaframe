#include "capture-dismissal.hpp"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QScreen>
#include <QTimer>
#include <cmath>

namespace {
void query(QObject *owner, const QString &what,
           std::function<void(QJsonDocument)> done) {
  auto *p = new QProcess(owner);
  auto *timeout = new QTimer(p);
  timeout->setSingleShot(true);
  QObject::connect(timeout, &QTimer::timeout, p, [p] { p->kill(); });
  QObject::connect(p, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                   owner, [p, done](int code, QProcess::ExitStatus status) {
                     const auto document =
                         code == 0 && status == QProcess::NormalExit
                             ? QJsonDocument::fromJson(
                                   p->readAllStandardOutput())
                             : QJsonDocument();
                     p->deleteLater();
                     done(document);
                   });
  QObject::connect(p, &QProcess::errorOccurred, owner,
                   [p, done](QProcess::ProcessError error) {
                     if (error == QProcess::FailedToStart) {
                       p->deleteLater();
                       done({});
                     }
                   });
  p->start("hyprctl", {"-j", what});
  timeout->start(1000);
}
bool layersGone(const QJsonDocument &document, bool desktop) {
  if (!document.isObject())
    return false;
  const auto monitors = document.object();
  for (const auto &monitor : monitors) {
    const auto levels = monitor.toObject().value("levels").toObject();
    for (const auto &level : levels)
      for (const auto &value : level.toArray()) {
        const auto layer = value.toObject();
        if (layer.value("pid").toInteger() !=
            QCoreApplication::applicationPid())
          continue;
        const auto name = layer.value("namespace").toString();
        if (name == CaptureDismissal::scope ||
            (desktop &&
             (name == "omaframe-selection" || name == "omaframe-finishes")))
          return false;
      }
  }
  return true;
}
} // namespace
int CaptureDismissal::frameWait(QScreen *screen) {
  const double refresh = screen ? screen->refreshRate() : 60;
  return int(std::ceil(2000 / std::max(10.0, refresh)));
}
bool CaptureDismissal::prepare(QString &error) {
  QProcess p;
  p.start("hyprctl",
          {"eval", "hl.layer_rule({ name = \"omaframe-capture-countdown\", "
                   "match = { namespace = "
                   "\"^omaframe-capture-countdown$\" }, no_anim = true, "
                   "animation = \"none\" }); "
                   "hl.layer_rule({ name = \"omaframe-capture-dismissal\", "
                   "match = { namespace = "
                   "\"^omaframe-(selection|finishes)$\" }, no_anim = true, "
                   "animation = \"none\" }); "
                   "hl.window_rule({ name = \"omaframe-delay-dismissal\", "
                   "match = { class = "
                   "\"^io.github.btsouth.omaframe$\", title = \"^Omaframe$\" "
                   "}, no_anim = true })"});
  if (!p.waitForFinished(800)) {
    p.kill();
    p.waitForFinished();
  }
  if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0 ||
      p.readAllStandardOutput().trimmed() != "ok") {
    error = "Could not disable screenshot dismissal animations.";
    return false;
  }
  return true;
}
void CaptureDismissal::restoreAnimations() {
  QProcess p;
  p.start("hyprctl",
          {"eval", "hl.window_rule({ name = \"omaframe-delay-dismissal\", "
                   "enabled = false }); "
                   "hl.layer_rule({ name = \"omaframe-capture-dismissal\", "
                   "enabled = false })"});
  if (!p.waitForFinished(500)) {
    p.kill();
    p.waitForFinished();
  }
}
void CaptureDismissal::clearDesktop(QObject *owner, QScreen *screen,
                                    std::function<void(bool)> done) {
  const int frames = frameWait(screen);
  QTimer::singleShot(0, owner, [owner, frames, done] {
    query(owner, "layers", [owner, frames, done](const QJsonDocument &layers) {
      if (!layersGone(layers, true)) {
        done(false);
        return;
      }
      query(owner, "clients",
            [owner, frames, done](const QJsonDocument &clients) {
              if (!clients.isArray()) {
                done(false);
                return;
              }
              for (const auto &value : clients.array()) {
                const auto client = value.toObject();
                if (client.value("pid").toInteger() ==
                        QCoreApplication::applicationPid() &&
                    client.value("title").toString() == "Omaframe") {
                  done(false);
                  return;
                }
              }
              QTimer::singleShot(frames, owner, [done] { done(true); });
            });
    });
  });
}
void CaptureDismissal::clear(QQuickWindow *badge, QObject *owner,
                             std::function<void(bool)> done,
                             std::function<bool()> current) {
  const int frames = frameWait(badge->screen());
  QString error;
  // A compositor reload during the countdown may have removed runtime rules.
  if (!prepare(error)) {
    done(false);
    return;
  }
  QTimer::singleShot(frames, owner, [badge, owner, frames, done, current] {
    if (!current())
      return;
    badge->hide();
    badge->destroy();
    // Return to Qt so the Wayland unmap/destroy is flushed before the query.
    QTimer::singleShot(0, owner, [owner, frames, done] {
      query(owner, "layers",
            [owner, frames, done](const QJsonDocument &layers) {
              if (!layersGone(layers, false)) {
                done(false);
                return;
              }
              QTimer::singleShot(frames, owner, [done] { done(true); });
            });
    });
  });
}
