#include "capture-dismissal.hpp"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <QUuid>
#include <cmath>
#include <memory>
#include <utility>

namespace {
CaptureDismissal::Boundary::Stop
process(QObject *owner, const QStringList &args,
        std::function<void(CaptureDismissal::Boundary::Reply)> done) {
  auto *p = new QProcess(owner);
  auto completed = std::make_shared<bool>(false);
  auto finish = [p, done, completed](bool success) {
    if (std::exchange(*completed, true))
      return;
    done({success, p->readAllStandardOutput()});
    p->deleteLater();
  };
  QObject::connect(p, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                   p, [finish](int code, QProcess::ExitStatus status) {
                     finish(code == 0 && status == QProcess::NormalExit);
                   });
  QObject::connect(p, &QProcess::errorOccurred, p,
                   [finish](QProcess::ProcessError error) {
                     if (error == QProcess::FailedToStart)
                       finish(false);
                   });
  p->start("hyprctl", args);
  return [p = QPointer<QProcess>(p)] {
    // SIGKILL is immediate; finished reaps/deletes asynchronously. Never wait
    // for a child on the UI thread, including after cancellation or timeout.
    if (p)
      p->kill();
  };
}
bool layersGone(const QJsonDocument &document, bool desktop) {
  if (!document.isObject())
    return false;
  for (const auto &monitor : document.object()) {
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
bool accepted(const CaptureDismissal::Boundary::Reply &reply) {
  return reply.success && reply.output.trimmed() == "ok";
}
} // namespace

int CaptureDismissal::frameWait(QScreen *screen) {
  const double refresh = screen ? screen->refreshRate() : 60;
  return int(std::ceil(2000 / std::max(10.0, refresh)));
}
using CaptureDismissal::Boundary;
Boundary::Boundary(QObject *parent, Transport transport)
    : QObject(parent), m_transport(std::move(transport)) {
  if (!m_transport)
    m_transport = [this](const QStringList &args, auto done) {
      return process(this, args, done);
    };
  QSettings settings;
  m_ruleId = settings.value("screenshot/dismissalRuleId").toString();
  if (QUuid(m_ruleId).isNull()) {
    m_ruleId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    settings.setValue("screenshot/dismissalRuleId", m_ruleId);
    // Persist ownership before installing rules, including for SIGKILL
    // recovery.
    settings.sync();
  }
}
QString Boundary::rule(const QString &suffix) const {
  return "omaframe-delay-" + m_ruleId + '-' + suffix;
}
void Boundary::command(Command command) {
  m_commands.enqueue(std::move(command));
  next();
}
void Boundary::next() {
  if (m_running || m_commands.isEmpty())
    return;
  auto job = m_commands.dequeue();
  if (job.current && !job.current()) {
    next();
    return;
  }
  m_running = true;
  auto *timer = new QTimer(this);
  timer->setSingleShot(true);
  auto completed = std::make_shared<bool>(false);
  auto stop = std::make_shared<Stop>();
  auto finish = [this, timer, completed, job](Reply reply) {
    if (std::exchange(*completed, true))
      return;
    timer->stop();
    timer->deleteLater();
    m_running = false;
    m_abort = {};
    if (!job.current || job.current())
      job.done(reply);
    next();
  };
  m_abort = [stop, finish] {
    if (*stop)
      (*stop)();
    finish({});
  };
  connect(timer, &QTimer::timeout, this, m_abort);
  timer->start(job.deadline);
  // A late reply after timeout/cancel is ignored, even if another job started.
  *stop =
      m_transport(job.args, [self = QPointer<Boundary>(this), finish](Reply r) {
        if (self)
          finish(r);
      });
}
void Boundary::restore(Done done) {
  command(
      {{"eval", QString("hl.window_rule({ name = \"%1\", enabled = false }); "
                        "hl.layer_rule({ name = \"%2\", enabled = false })")
                    .arg(rule("window"), rule("selection"))},
       500,
       [done](Reply r) { done(accepted(r)); },
       {}});
}
void Boundary::recover(Done done) { restore(std::move(done)); }
void Boundary::prepare(quint64 epoch, Done done) {
  command(
      {{"eval",
        QString(
            "hl.layer_rule({ name = \"%1\", enabled = true, match = { "
            "namespace = "
            "\"^omaframe-capture-countdown$\" }, no_anim = true, "
            "animation = \"none\" }); "
            "hl.layer_rule({ name = \"%2\", enabled = true, match = { "
            "namespace = "
            "\"^omaframe-(selection|finishes)$\" }, no_anim = true, "
            "animation = \"none\" }); "
            "hl.window_rule({ name = \"%3\", enabled = true, match = { class = "
            "\"^io.github.btsouth.omaframe$\", title = \"^Omaframe$\" "
            "}, no_anim = true })")
            .arg(rule("countdown"), rule("selection"), rule("window"))},
       800,
       [done](Reply r) { done(accepted(r)); },
       [this, epoch] { return current(epoch); }});
  // The countdown-only rule stays installed: it matches only our badge, which
  // must never fade. Reusing the saved UUID avoids accumulating crash
  // leftovers.
}
void Boundary::afterFrames(quint64 epoch, int frames,
                           std::function<void()> done) {
  QTimer::singleShot(frames, this, [this, epoch, done] {
    if (current(epoch))
      done();
  });
}
void Boundary::finish(quint64 epoch, bool success, Done done) {
  restore([this, epoch, success, done](bool restored) {
    if (current(epoch))
      done(success && restored);
  });
}
void Boundary::desktopGone(quint64 epoch, int frames, Done done) {
  command({{"-j", "layers"},
           1000,
           [this, epoch, frames, done](Reply layers) {
             if (!layers.success ||
                 !layersGone(QJsonDocument::fromJson(layers.output), true)) {
               finish(epoch, false, done);
               return;
             }
             command({{"-j", "clients"},
                      1000,
                      [this, epoch, frames, done](Reply reply) {
                        const auto clients =
                            QJsonDocument::fromJson(reply.output);
                        bool gone = reply.success && clients.isArray();
                        for (const auto &value : clients.array()) {
                          const auto c = value.toObject();
                          if (c.value("pid").toInteger() ==
                                  QCoreApplication::applicationPid() &&
                              c.value("title").toString() == "Omaframe")
                            gone = false;
                        }
                        afterFrames(epoch, frames, [this, epoch, gone, done] {
                          finish(epoch, gone, done);
                        });
                      },
                      [this, epoch] { return current(epoch); }});
           },
           [this, epoch] { return current(epoch); }});
}
void Boundary::begin(quint64 generation, QScreen *screen,
                     std::function<void()> hide, Done done) {
  const auto epoch = ++m_epoch;
  m_generation = generation;
  const int frames = frameWait(screen);
  // Also retry startup recovery before each timer. No failed restore can fall
  // through to capture, and a cancelled prepare is restored before a new one.
  restore([this, epoch, frames, hide, done](bool restored) {
    if (!current(epoch))
      return;
    if (!restored) {
      done(false);
      return;
    }
    prepare(epoch, [this, epoch, frames, hide, done](bool success) {
      if (!success) {
        finish(epoch, false, done);
        return;
      }
      afterFrames(epoch, frames, [this, epoch, frames, hide, done] {
        hide();
        // Return to Qt so Wayland unmap/destroy is flushed before querying.
        afterFrames(epoch, 0, [this, epoch, frames, done] {
          desktopGone(epoch, frames, done);
        });
      });
    });
  });
}
void Boundary::placeBadge(quint64 generation,
                          std::function<void(QScreen *)> done) {
  if (generation != m_generation)
    return;
  const auto epoch = m_epoch;
  command({{"-j", "cursorpos"},
           500,
           [done](Reply reply) {
             QScreen *screen = nullptr;
             const auto at = QJsonDocument::fromJson(reply.output);
             if (reply.success && at.isObject()) {
               const QPoint point(at.object().value("x").toInt(),
                                  at.object().value("y").toInt());
               for (auto *candidate : QGuiApplication::screens())
                 if (candidate->geometry().contains(point))
                   screen = candidate;
             }
             done(screen ? screen : QGuiApplication::primaryScreen());
           },
           [this, epoch] { return current(epoch); }});
}
void Boundary::clear(quint64 generation, QQuickWindow *badge, Done done) {
  if (generation != m_generation)
    return;
  const auto epoch = ++m_epoch;
  const int frames = frameWait(badge->screen());
  // A compositor reload may have removed the countdown rule. Preserve the
  // proven sequence: rule acknowledgement, frames, destroy, Qt flush, layer
  // acknowledgement, two compositor frames, checked restore, then acquisition.
  prepare(epoch, [this, epoch, badge = QPointer<QQuickWindow>(badge), frames,
                  done](bool success) {
    if (!success || !badge) {
      finish(epoch, false, done);
      return;
    }
    afterFrames(epoch, frames, [this, epoch, badge, frames, done] {
      if (!badge) {
        finish(epoch, false, done);
        return;
      }
      badge->hide();
      badge->destroy();
      afterFrames(epoch, 0, [this, epoch, frames, done] {
        command({{"-j", "layers"},
                 1000,
                 [this, epoch, frames, done](Reply reply) {
                   const bool gone =
                       reply.success &&
                       layersGone(QJsonDocument::fromJson(reply.output), false);
                   afterFrames(epoch, frames, [this, epoch, gone, done] {
                     finish(epoch, gone, done);
                   });
                 },
                 [this, epoch] { return current(epoch); }});
      });
    });
  });
}
void Boundary::cancel(Done done) {
  const auto epoch = ++m_epoch;
  m_generation = 0;
  // Invalidate before terminating. The cancelled command cannot hide a newly
  // restored editor or map a late badge. Restore after its bounded termination.
  if (m_abort) {
    auto abort = m_abort;
    abort();
  }
  restore([this, epoch, done](bool success) {
    if (current(epoch))
      done(success);
  });
}
