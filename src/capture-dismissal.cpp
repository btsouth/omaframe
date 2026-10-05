#include "capture-dismissal.hpp"
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QScreen>
#include <QTimer>
#include <cmath>
#include <memory>

bool CaptureDismissal::prepare(QString &error) {
  QProcess p;
  p.start("hyprctl", {"eval", "hl.layer_rule({ match = { namespace = \"^" + scope +
      "$\" }, no_anim = true, animation = \"none\" })"});
  if (!p.waitForFinished(800)) {
    p.kill();
    p.waitForFinished();
  }
  if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0 ||
      p.readAllStandardOutput().trimmed() != "ok") {
    error = "Could not disable screenshot countdown animations.";
    return false;
  }
  return true;
}
void CaptureDismissal::clear(QQuickWindow *badge, QObject *owner,
                            std::function<void(bool)> done) {
  const double refresh = badge->screen() ? badge->screen()->refreshRate() : 60;
  const int frames = int(std::ceil(2000 / std::max(10.0, refresh)));
  badge->hide();
  badge->destroy();
  // Return to Qt first so the Wayland unmap/destroy is flushed.
  QTimer::singleShot(0, owner, [owner, frames, done = std::move(done)] {
    auto *p = new QProcess(owner);
    auto *timeout = new QTimer(p);
    timeout->setSingleShot(true);
    QObject::connect(timeout, &QTimer::timeout, p, [p] { p->kill(); });
    QObject::connect(p, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                     owner, [owner, p, frames, done](int code, QProcess::ExitStatus status) {
      const auto document = QJsonDocument::fromJson(p->readAllStandardOutput());
      const bool gone = status == QProcess::NormalExit && code == 0 &&
          document.isObject() && !document.toJson().contains(scope.toUtf8());
      p->deleteLater();
      if (!gone) {
        done(false);
        return;
      }
      QTimer::singleShot(frames, owner, [done] { done(true); });
    });
    QObject::connect(p, &QProcess::errorOccurred, owner,
                     [p, done](QProcess::ProcessError error) {
      if (error == QProcess::FailedToStart) {
        p->deleteLater();
        done(false);
      }
    });
    p->start("hyprctl", {"-j", "layers"});
    timeout->start(1000);
  });
}
