#include "recording.hpp"
#include "displays.hpp"
#include "shortcuts.hpp"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>
#include <QUuid>
#include <QVersionNumber>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <sys/prctl.h>

static QByteArray command(const QString &program, const QStringList &args,
                          int timeout = 2500) {
  QProcess p;
  p.start(program, args);
  if (!p.waitForFinished(timeout)) {
    p.kill();
    p.waitForFinished();
    return {};
  }
  return p.exitCode() == 0 ? p.readAllStandardOutput() : QByteArray();
}

Recording::Placement Recording::placeStop(const QList<Display> &displays,
                                          const QString &capturedDisplay,
                                          const QRect &capture, QSize size) {
  // Keep a gap on every side, including for rounding at fractional scales.
  constexpr int gap = 16;
  const QRect keepOut = capture.adjusted(-gap, -gap, gap, gap);
  auto usable = [&](const Display &d) {
    return d.bounds.marginsRemoved(d.reserved).adjusted(gap, gap, -gap, -gap);
  };
  auto clampX = [&](const QRect &area, int x) {
    return std::clamp(x, area.left(),
                      std::max(area.left(), area.x() + area.width() - size.width()));
  };
  auto clampY = [&](const QRect &area, int y) {
    return std::clamp(y, area.top(),
                      std::max(area.top(), area.y() + area.height() - size.height()));
  };
  // Beside a region on its own display: below, above, right, then left.
  for (const auto &d : displays) {
    if (d.name != capturedDisplay)
      continue;
    const QRect area = usable(d);
    const int x = clampX(area, capture.center().x() - size.width() / 2);
    const int y = clampY(area, capture.top());
    for (QPoint point : {QPoint(x, capture.y() + capture.height() + gap),
                         QPoint(x, capture.top() - gap - size.height()),
                         QPoint(capture.x() + capture.width() + gap, y),
                         QPoint(capture.left() - gap - size.width(), y)}) {
      const QRect rect(point, size);
      if (area.contains(rect) && !keepOut.intersects(rect))
        return {d.name, rect};
    }
  }
  // Otherwise the nearest other display, at the edge that faces the capture,
  // so the control stays next to what is being recorded.
  Placement best;
  double bestDistance = 0;
  for (const auto &d : displays) {
    if (d.name == capturedDisplay)
      continue;
    const QRect area = usable(d);
    if (area.width() < size.width() || area.height() < size.height())
      continue;
    QPoint point;
    if (d.bounds.left() >= capture.x() + capture.width())
      point = {area.left(), clampY(area, capture.top())};
    else if (d.bounds.x() + d.bounds.width() <= capture.left())
      point = {area.x() + area.width() - size.width(), clampY(area, capture.top())};
    else if (d.bounds.top() >= capture.y() + capture.height())
      point = {clampX(area, capture.center().x() - size.width() / 2), area.top()};
    else
      point = {clampX(area, capture.center().x() - size.width() / 2),
               area.y() + area.height() - size.height()};
    const QRect rect(point, size);
    if (keepOut.intersects(rect))
      continue;
    const int dx = std::max({0, rect.left() - (capture.x() + capture.width()),
                             capture.left() - (rect.x() + rect.width())});
    const int dy = std::max({0, rect.top() - (capture.y() + capture.height()),
                             capture.top() - (rect.y() + rect.height())});
    const double distance = std::hypot(dx, dy);
    if (best.bounds.isEmpty() || distance < bestDistance) {
      best = {d.name, rect};
      bestDistance = distance;
    }
  }
  return best;
}
Recording::Placement Recording::placeCountdown(const QList<Display> &displays,
                                              const QString &capturedDisplay,
                                              QSize size) {
  constexpr int gap = 16;
  for (const auto &d : displays) {
    if (d.name != capturedDisplay)
      continue;
    const QRect area =
        d.bounds.marginsRemoved(d.reserved).adjusted(gap, gap, -gap, -gap);
    if (area.width() < 160 || area.height() < size.height())
      return {};
    size.setWidth(std::min(size.width(), area.width()));
    return {d.name, QRect(QPoint(area.center().x() - size.width() / 2, area.top()),
                          size)};
  }
  return {};
}
QStringList Recording::arguments(const QString &target, const QString &path,
                                 const QString &desktop, const QString &mic,
                                 bool cursor) {
  QStringList args{"-w",
                   target,
                   "-k",
                   "h264",
                   "-f",
                   "60",
                   "-fm",
                   "cfr",
                   "-fallback-cpu-encoding",
                   "yes",
                   "-cursor",
                   cursor ? "yes" : "no",
                   "-exclude-metadata",
                   "yes",
                   "-write-first-frame-ts",
                   "yes",
                   "-o",
                   path};
  QStringList audio;
  if (!desktop.isEmpty())
    audio << desktop;
  if (!mic.isEmpty())
    audio << mic;
  if (!audio.isEmpty())
    args << "-a" << audio.join('|') << "-ac" << "aac";
  return args;
}
QPair<QString, QStringList>
Recording::recorderCommand(const QStringList &arguments) {
  // `exec` keeps one process, so its PID, the parent-death signal and SIGINT
  // all still reach the recorder itself.
  return {"bash",
          QStringList{"-c",
                      "exec -a gpu-screen-recorder gpu-screen-recorder \"$@\"",
                      "omaframe-recorder"} +
              arguments};
}
Recorder::Recorder(QObject *parent) : QObject(parent) {
  QSettings s;
  m_desktop = s.value("record/desktop", false).toBool();
  m_microphone = s.value("record/microphone", false).toBool();
  m_cursor = s.value("record/cursor", true).toBool();
  m_suppressStartupPop = s.value("record/suppressStartupPop", false).toBool();
  m_countdown = std::clamp(s.value("record/countdown", 3).toInt(), 0, 5);
  m_preferredMic = s.value("record/micSource").toString();
  // If Omaframe itself dies, ask the recorder to finish its file instead of
  // leaving an orphaned capture with an unwritten MP4 index.
  m_process.setChildProcessModifier([] { ::prctl(PR_SET_PDEATHSIG, SIGINT); });
  m_tick.setInterval(500);
  connect(&m_tick, &QTimer::timeout, this, &Recorder::changed);
  m_countdownTick.setInterval(1000);
  connect(&m_countdownTick, &QTimer::timeout, this, [this] {
    if (--m_remaining <= 0) {
      m_countdownTick.stop();
      launch();
    }
    emit changed();
  });
  m_startupCheck.setInterval(100);
  connect(&m_startupCheck, &QTimer::timeout, this, [this] {
    if (m_state != "starting") {
      m_startupCheck.stop();
      return;
    }
    // MP4 headers can be written before capture produces a frame. GSR writes
    // this sidecar only after the first frame, so file size is not readiness.
    QFile firstFrame(m_path + ".ts");
    bool frameReady = false;
    if (firstFrame.open(QIODevice::ReadOnly)) {
      // GSR 6.1.3 writes a header followed by the timestamp values. Read the
      // last line so the header cannot make a valid first frame look absent.
      const auto fields =
          firstFrame.readAll().trimmed().split('\n').last().simplified().split(' ');
      bool monotonic = false, realtime = false;
      if (fields.size() == 2) {
        frameReady = fields[0].toLongLong(&monotonic) > 0 &&
                     fields[1].toLongLong(&realtime) > 0 && monotonic && realtime;
      }
    }
    if (frameReady && m_process.state() == QProcess::Running) {
      firstFrame.close();
      firstFrame.remove();
      m_state = "recording";
      m_status = "Recording";
      m_clock.restart();
      m_tick.start();
      m_startupCheck.stop();
      refreshBar();
      emit changed();
    } else if (m_clock.elapsed() > 12000) {
      m_frameTimedOut = true;
      stop();
    }
  });
  connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
    m_error = (m_error + QString::fromUtf8(m_process.readAllStandardError()))
                  .right(4000);
  });
  connect(&m_process, &QProcess::errorOccurred, this,
          [this](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart)
              fail("Could not start GPU Screen Recorder. Install "
                   "gpu-screen-recorder and try again.");
          });
  connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
          this, &Recorder::validateResult);
}
void Recorder::refreshBar() const {
  // The bar's recording icon only rechecks when asked.
  if (m_barStop)
    QProcess::startDetached("omarchy-shell",
                            {"-q", "omarchy.indicators", "refresh"});
}
bool Recorder::active() const {
  return m_state == "countdown" || m_state == "starting" ||
         m_state == "recording" || m_state == "stopping";
}
QString Recorder::elapsed() const {
  qint64 seconds = m_clock.isValid() ? m_clock.elapsed() / 1000 : 0;
  return QString("%1:%2")
      .arg(seconds / 60, 2, 10, QChar('0'))
      .arg(seconds % 60, 2, 10, QChar('0'));
}
QStringList Recorder::displays() const {
  QStringList result;
  for (const auto &d : m_displays)
    result << d.label;
  return result;
}
QString Recorder::place(const QString &display) const {
  for (const auto &d : m_displays)
    if (d.name == display)
      return d.place;
  return display;
}
Recording::Placement Recorder::visibleControl() const {
  if (hasControl())
    return m_control;
  return m_state == "countdown" ? m_countdownControl : Recording::Placement{};
}
// Options are remembered as soon as they change, so a choice made in the
// capture bar holds even if that recording is cancelled.
#define OPTION(Setter, Type, Member, Key)                                      \
  void Recorder::Setter(Type v) {                                              \
    if (active())                                                              \
      return;                                                                  \
    Member = v;                                                                \
    QSettings().setValue(Key, v);                                              \
    updateSetupStatus();                                                       \
    emit changed();                                                            \
  }
OPTION(setDesktopAudio, bool, m_desktop, "record/desktop")
OPTION(setMicAudio, bool, m_microphone, "record/microphone")
OPTION(setCursor, bool, m_cursor, "record/cursor")
OPTION(setSuppressStartupPop, bool, m_suppressStartupPop,
       "record/suppressStartupPop")
void Recorder::setMicrophone(int i) {
  if (!active() && i >= 0 && i < m_mics.size()) {
    m_mic = i;
    m_preferredMic = m_mics[i].toMap().value("id").toString();
    QSettings().setValue("record/micSource", m_preferredMic);
    updateSetupStatus();
    emit changed();
  }
}
void Recorder::setCountdown(int seconds) {
  if (!active()) {
    m_countdown = std::clamp(seconds, 0, 5);
    QSettings().setValue("record/countdown", m_countdown);
    emit changed();
  }
}
QString Recorder::targetLabel() const {
  if (m_capture.isEmpty())
    return "Choose an area, a window or a display";
  if (m_full)
    for (const auto &d : m_displays)
      if (d.name == m_screen)
        return "Entire display · " + d.label;
  return QString("Area · %1 × %2 on the %3")
      .arg(m_capture.width())
      .arg(m_capture.height())
      .arg(place(m_screen));
}
bool Recorder::canStart() const {
  return !active() && m_state != "loading" && hasTarget() && !m_otherRecorder &&
         (hasControl() || stopShortcut()) && (!m_microphone || m_mic >= 0) &&
         (!m_desktop || !m_defaultSink.isEmpty());
}
QString Recorder::controlLocation() const {
  const QString also =
      stopShortcut() ? " " + m_stopKey + " also stops it." : QString();
  if (!hasTarget())
    return "A Stop button appears outside the recorded area when there is "
           "room." + also;
  if (hasControl())
    return (m_control.display != m_screen
                ? "Stop stays on the " + place(m_control.display) +
                      ", outside the video."
                : QString("Stop stays just outside the recorded area.")) +
           also;
  if (stopShortcut())
    return "No Stop button is shown, so nothing from Omaframe is in the "
           "video. Stop with " + m_stopKey +
           (m_barStop ? " or the recording icon in the Omarchy bar." : ".");
  if (m_full)
    return "With the whole display recorded, no Stop button is shown so none "
           "ends up in the video. Set up a stop shortcut to record it.";
  return "There is no room for a Stop button outside this area. Set up a stop "
         "shortcut, or choose a smaller area.";
}
void Recorder::updateSetupStatus() {
  if (m_state != "setup")
    return;
  m_status = m_otherRecorder
                 ? "Another screen recorder is running. Stop it before "
                   "starting an Omaframe recording."
             : m_microphone && m_mic < 0
                 ? "Your selected microphone is unavailable. Choose a "
                   "microphone or turn it off."
             : m_desktop && m_defaultSink.isEmpty()
                 ? "Desktop audio is unavailable. Turn it off or reconnect "
                   "your output."
             : !hasTarget() ? "Choose what to record."
             : needsStopShortcut()
                 ? "Set up a stop shortcut to record this area."
                 : "Ready to record.";
}
void Recorder::setStopKey(const QString &key) {
  if (m_stopKey == key)
    return;
  m_stopKey = key;
  updateSetupStatus();
  emit changed();
}
void Recorder::prepare(bool showSetup) {
  if (active())
    return;
  if (m_state == "loading") {
    if (showSetup && !m_showSetupWhenLoaded) {
      m_showSetupWhenLoaded = true;
      emit setupRequested();
    }
    return;
  }
  const int generation = ++m_generation;
  m_state = "loading";
  m_status = "Checking displays and audio…";
  m_showSetupWhenLoaded = showSetup;
  m_screen.clear();
  m_capture = {};
  m_target.clear();
  m_control = m_countdownControl = {};
  m_pending = {};
  emit changed();
  if (showSetup)
    emit setupRequested();
  struct Result {
    QList<Recording::Display> displays;
    QVariantList mics;
    QString sink, defaultMic, stopKey;
    bool other = false;
  };
  auto *watcher = new QFutureWatcher<Result>(this);
  connect(watcher, &QFutureWatcher<Result>::finished, this,
          [this, watcher, generation] {
            auto r = watcher->result();
            watcher->deleteLater();
            if (generation != m_generation || m_state != "loading")
              return;
            m_displays = r.displays;
            m_mics = r.mics;
            m_defaultSink = r.sink;
            m_otherRecorder = r.other;
            m_stopKey = r.stopKey;
            m_mic = -1;
            QString wanted = m_preferredMic;
            if (wanted.isEmpty()) {
              wanted = r.defaultMic;
              for (const auto &mic : m_mics)
                if (mic.toMap().value("id").toString() == "clean_desktop_microphone")
                  wanted = "clean_desktop_microphone";
            }
            for (int i = 0; i < m_mics.size(); ++i)
              if (m_mics[i].toMap().value("id").toString() == wanted)
                m_mic = i;
            m_state = "setup";
            updateSetupStatus();
            emit changed();
            applyPending();
          });
  watcher->setFuture(QtConcurrent::run([] {
    Result r;
    auto monitors =
        QJsonDocument::fromJson(command("hyprctl", {"-j", "monitors"})).array();
    QList<Displays::Info> infos;
    for (auto v : monitors) {
      auto o = v.toObject();
      double scale = o.value("scale").toDouble(1);
      int w = o.value("width").toInt(), h = o.value("height").toInt();
      if (o.value("transform").toInt() % 2)
        std::swap(w, h);
      if (scale > 0 && w > 0 && h > 0) {
        const QRect bounds(o.value("x").toInt(), o.value("y").toInt(),
                           qRound(w / scale), qRound(h / scale));
        // Hyprland lists reserved space as left, top, right, bottom.
        const auto reserved = o.value("reserved").toArray();
        QMargins margins;
        if (reserved.size() == 4)
          margins = QMargins(reserved[0].toInt(), reserved[1].toInt(),
                             reserved[2].toInt(), reserved[3].toInt());
        r.displays.append({o.value("name").toString(), bounds, {}, {}, margins});
        infos.append({o.value("name").toString(), o.value("make").toString(),
                      o.value("model").toString(), bounds, QSize(w, h)});
      }
    }
    const auto names = Displays::describe(infos);
    for (int i = 0; i < names.size(); ++i) {
      r.displays[i].label = names[i].label;
      r.displays[i].place = names[i].place;
    }
    auto sources = QJsonDocument::fromJson(
                       command("pactl", {"-f", "json", "list", "sources"}))
                       .array();
    for (auto v : sources) {
      auto o = v.toObject();
      auto name = o.value("name").toString();
      if (!name.endsWith(".monitor"))
        r.mics.append(QVariantMap{
            {"id", name}, {"label", o.value("description").toString(name)}});
    }
    r.defaultMic =
        QString::fromUtf8(command("pactl", {"get-default-source"})).trimmed();
    auto sink =
        QString::fromUtf8(command("pactl", {"get-default-sink"})).trimmed();
    if (!sink.isEmpty())
      r.sink = sink + ".monitor";
    r.other = !command("pgrep", {"-f", "^([^ ]*/)?gpu-screen-recorder( |$)"})
                   .isEmpty();
    r.stopKey = Shortcuts::omaframeKey(
        QJsonDocument::fromJson(command("hyprctl", {"-j", "binds"})).array(),
        Shortcuts::Action::Record);
    return r;
  }));
}
void Recorder::showSetup() {
  if (active())
    return;
  if (m_state == "idle" || m_state == "saved" || m_state == "selecting") {
    if (m_state == "selecting" && !m_displays.isEmpty()) {
      m_state = "setup";
      updateSetupStatus();
      emit changed();
      emit setupRequested();
      return;
    }
    prepare(true);
    return;
  }
  if (m_state == "loading")
    m_showSetupWhenLoaded = true;
  emit setupRequested();
}
void Recorder::setTarget(const QString &screen, const QRect &rect, bool full) {
  m_screen = screen;
  m_capture = rect;
  m_full = full;
  m_target = full ? screen
                  : QString("%1x%2+%3+%4")
                        .arg(rect.width())
                        .arg(rect.height())
                        .arg(rect.x())
                        .arg(rect.y());
  m_control = Recording::placeStop(m_displays, screen, rect);
  m_countdownControl = hasControl()
                           ? Recording::Placement{}
                           : Recording::placeCountdown(m_displays, screen);
  m_state = "setup";
  updateSetupStatus();
  emit changed();
}
void Recorder::applyPending() {
  if (!m_pending.valid)
    return;
  const Pending pending = m_pending;
  m_pending = {};
  if (pending.full)
    selectDisplayNamed(pending.display, pending.start);
  else
    regionSelected(pending.display, pending.area, pending.start);
}
void Recorder::selectDisplay(int i) {
  if (active() || i < 0 || i >= m_displays.size())
    return;
  setTarget(m_displays[i].name, m_displays[i].bounds, true);
}
void Recorder::selectDisplayNamed(const QString &name, bool startAfterSelection) {
  if (active())
    return;
  if (m_state == "loading") {
    m_pending = {name, {}, true, startAfterSelection, true};
    return;
  }
  for (const auto &d : m_displays)
    if (d.name == name) {
      setTarget(name, d.bounds, true);
      if (startAfterSelection && canStart())
        start();
      else
        emit setupRequested();
      return;
    }
  fail("That display is no longer available. Choose what to record again.");
}
void Recorder::chooseRegion() {
  if (active() || m_state == "loading")
    return;
  m_state = "selecting";
  emit changed();
  emit hideRequested();
  QTimer::singleShot(120, this, [this] {
    if (m_state == "selecting")
      emit selectionRequested();
  });
}
void Recorder::regionSelected(const QString &name, const QRectF &normalized,
                              bool startAfterSelection) {
  if (active())
    return;
  if (m_state == "loading") {
    m_pending = {name, normalized, false, startAfterSelection, true};
    return;
  }
  // A selection that covers the display records the display itself, at its
  // native resolution, rather than a region of the same size.
  if (normalized.left() <= 0.001 && normalized.top() <= 0.001 &&
      normalized.right() >= 0.999 && normalized.bottom() >= 0.999) {
    selectDisplayNamed(name, startAfterSelection);
    return;
  }
  for (const auto &d : m_displays)
    if (d.name == name) {
      QRect r(qRound(d.bounds.x() + normalized.x() * d.bounds.width()),
              qRound(d.bounds.y() + normalized.y() * d.bounds.height()),
              qRound(normalized.width() * d.bounds.width()),
              qRound(normalized.height() * d.bounds.height()));
      r = r.intersected(d.bounds);
      if (r.width() < 16 || r.height() < 16) {
        fail("Select a larger recording area.");
        return;
      }
      setTarget(name, r, false);
      if (startAfterSelection && canStart())
        start();
      else
        emit setupRequested();
      return;
    }
  fail("That display is no longer available. Select the recording area again.");
}
void Recorder::start() {
  if (!canStart())
    return;
  const int generation = ++m_generation;
  m_state = "countdown";
  m_remaining = m_countdown;
  m_error.clear();
  m_frameTimedOut = false;
  m_path.clear();
  QSettings s;
  s.setValue("record/desktop", m_desktop);
  s.setValue("record/microphone", m_microphone);
  s.setValue("record/cursor", m_cursor);
  s.setValue("record/suppressStartupPop", m_suppressStartupPop);
  s.setValue("record/countdown", m_countdown);
  if (m_mic >= 0)
    s.setValue("record/micSource", m_mics[m_mic].toMap().value("id"));
  emit changed();
  emit hideRequested();
  if (!visibleControl().bounds.isEmpty() && (hasControl() || m_remaining > 0))
    emit controlRequested();
  // Let the setup surface disappear before the first recorded frame.
  QTimer::singleShot(250, this, [this, generation] {
    if (generation != m_generation || m_state != "countdown")
      return;
    if (m_remaining > 0)
      m_countdownTick.start();
    else
      launch();
  });
}
void Recorder::launch() {
  if (m_state != "countdown")
    return;
  m_state = "starting";
  m_status = "Starting recorder…";
  // The countdown sits on the recorded display. Take it down before capture.
  if (!hasControl())
    emit hideRequested();
  emit changed();
  const int generation = m_generation;
  struct Check {
    QString error;
  };
  const auto target = m_capture;
  const bool hasControl = this->hasControl();
  const QString controlDisplay = m_control.display;
  const QString capturedDisplay = m_screen;
  QHash<QString, QRect> bounds;
  for (const auto &display : m_displays)
    bounds.insert(display.name, display.bounds);
  auto *watcher = new QFutureWatcher<Check>(this);
  connect(
      watcher, &QFutureWatcher<Check>::finished, this,
      [this, watcher, generation] {
        auto r = watcher->result();
        watcher->deleteLater();
        if (generation != m_generation || m_state != "starting")
          return;
        if (!r.error.isEmpty()) {
          fail(r.error);
          return;
        }
        const QString dir =
            QSettings()
                .value("videoDirectory", QStandardPaths::writableLocation(
                                             QStandardPaths::MoviesLocation) +
                                             "/Omaframe")
                .toString();
        if (!QDir().mkpath(dir)) {
          fail("Could not create the recording folder.");
          return;
        }
        m_path = dir + "/Recording-" +
                 QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss") +
                 "-" + QUuid::createUuid().toString(QUuid::Id128).left(6) +
                 ".mp4";
        const QString mic =
            m_microphone ? m_mics.value(m_mic).toMap().value("id").toString()
                         : QString();
        m_clock.start();
        const auto [program, arguments] =
            Recording::recorderCommand(Recording::arguments(
                m_target, m_path, m_desktop ? m_defaultSink : QString(), mic,
                m_cursor));
        m_process.start(program, arguments);
        m_startupCheck.start();
        emit changed();
      });
  watcher->setFuture(QtConcurrent::run([target, hasControl, controlDisplay,
                                       capturedDisplay, bounds] {
    if (!command("pgrep", {"-f", "^([^ ]*/)?gpu-screen-recorder( |$)"})
             .isEmpty())
      return Check{"Another screen recorder is running. Stop it first."};
    const QString recorderVersion =
        QString::fromUtf8(command("gpu-screen-recorder", {"--version"})).trimmed();
    const QVersionNumber parsed = QVersionNumber::fromString(recorderVersion);
    if (parsed.isNull() ||
        QVersionNumber::compare(parsed, QVersionNumber(6, 1, 3)) < 0)
      return Check{recorderVersion.isEmpty()
                       ? "Install GPU Screen Recorder 6.1.3 or newer to record."
                       : "GPU Screen Recorder " + recorderVersion +
                             " is too old. Version 6.1.3 or newer is required."};
    // Nothing of Omaframe's may be inside the capture when it starts. Hidden
    // surfaces can take a frame or two to leave, so check a few times.
    const QRect keepOut = target.adjusted(-8, -8, 8, 8);
    QString problem;
    for (int attempt = 0; attempt < 12; ++attempt) {
      problem.clear();
      bool found = false;
      const auto layers =
          QJsonDocument::fromJson(command("hyprctl", {"-j", "layers"})).object();
      for (auto mon = layers.begin(); mon != layers.end(); ++mon) {
        const QRect display = bounds.value(mon.key());
        for (auto level : mon.value().toObject().value("levels").toObject())
          for (auto value : level.toArray()) {
            const auto layer = value.toObject();
            const QString name = layer.value("namespace").toString();
            if (!name.startsWith("omaframe-"))
              continue;
            QRect actual(layer.value("x").toInt(), layer.value("y").toInt(),
                         layer.value("w").toInt(), layer.value("h").toInt());
            // Layer positions are global; accept display-local ones too.
            if (!display.isEmpty() && !display.contains(actual) &&
                display.contains(actual.translated(display.topLeft())))
              actual.translate(display.topLeft());
            if (name == "omaframe-record-control" && hasControl &&
                mon.key() == controlDisplay) {
              found = !actual.isEmpty();
              if (keepOut.intersects(actual))
                return Check{"The Stop button would be in the recording. "
                             "Choose another area."};
              continue;
            }
            if (mon.key() == capturedDisplay && keepOut.intersects(actual))
              problem = "An Omaframe window is still over the recording "
                        "area. Try again.";
          }
      }
      if (hasControl && !found)
        problem = "The Stop button did not appear. Choose the area again.";
      if (problem.isEmpty())
        break;
      QThread::msleep(60);
    }
    if (!problem.isEmpty())
      return Check{problem};
    if (!hasControl &&
        Shortcuts::omaframeKey(
            QJsonDocument::fromJson(command("hyprctl", {"-j", "binds"})).array(),
            Shortcuts::Action::Record)
            .isEmpty())
      return Check{"The stop shortcut is no longer active. Set it up again "
                   "before recording this area."};
    return Check{};
  }));
}
void Recorder::stop() {
  if (m_state == "countdown" ||
      (m_state == "starting" && m_process.state() == QProcess::NotRunning)) {
    // Nothing was captured yet. Cancel and go back to what the user was doing.
    ++m_generation;
    m_countdownTick.stop();
    m_state = "idle";
    emit hideRequested();
    emit changed();
    emit dismissRequested();
    return;
  }
  if (m_state != "recording" && m_state != "starting")
    return;
  m_state = "stopping";
  m_status = "Finishing and checking your recording…";
  m_startupCheck.stop();
  m_tick.stop();
  emit changed();
  // Stop only our child, and let it finish writing the container. Never pkill
  // other recorders or force-kill a recording after an arbitrary deadline.
  if (m_process.processId() > 0)
    ::kill(static_cast<pid_t>(m_process.processId()), SIGINT);
}
/** Removes a recording that holds no usable video, so a failed start does
 *  not leave an unplayable file behind. */
static bool discardEmpty(const QString &path) {
  const QFileInfo info(path);
  return info.exists() && info.size() < 64 * 1024 && QFile::remove(path);
}
void Recorder::validateResult(int code, QProcess::ExitStatus exitStatus) {
  m_startupCheck.stop();
  m_tick.stop();
  QFile::remove(m_path + ".ts");
  refreshBar();
  emit hideRequested();
  if (code != 0 || exitStatus != QProcess::NormalExit) {
    const bool discarded = discardEmpty(m_path);
    fail("Recording failed. " + m_error.simplified().right(500) +
         (discarded ? " No file was kept."
          : QFileInfo::exists(m_path) ? " The partial file is " + m_path
                                      : QString()));
    return;
  }
  m_state = "stopping";
  m_status = "Checking the saved video…";
  emit changed();
  auto *watcher = new QFutureWatcher<QString>(this);
  const auto path = m_path;
  const bool suppressStartupPop = m_suppressStartupPop;
  const bool frameTimedOut = m_frameTimedOut;
  connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher] {
    const QString error = watcher->result();
    watcher->deleteLater();
    if (!error.isEmpty()) {
      fail(error + (QFileInfo::exists(m_path) ? " The file is " + m_path : QString()));
      return;
    }
    m_state = "saved";
    m_status = "Recording saved.";
    emit changed();
    emit completed(QUrl::fromLocalFile(m_path));
  });
  watcher->setFuture(QtConcurrent::run([path, suppressStartupPop, frameTimedOut] {
    const auto data =
        QJsonDocument::fromJson(
            command("ffprobe",
                    {"-v", "error", "-show_entries", "stream=codec_type",
                     "-show_entries", "format=duration", "-of", "json", path},
                    10000))
            .object();
    bool video = false, audio = false;
    for (auto stream : data.value("streams").toArray()) {
      const auto type = stream.toObject().value("codec_type").toString();
      video |= type == "video";
      audio |= type == "audio";
    }
    if (!video || data.value("format")
                          .toObject()
                          .value("duration")
                          .toString()
                          .toDouble() <= 0) {
      const bool discarded = discardEmpty(path);
      return (frameTimedOut
                  ? QString("No video frame arrived within 12 seconds.")
                  : QString("The recorder stopped without a readable video.")) +
             (discarded ? " No file was kept." : QString());
    }
    QProcess decode;
    decode.start("ffmpeg", {"-hide_banner", "-loglevel", "error", "-xerror",
                             "-i", path, "-map", "0:v:0", "-frames:v", "1",
                             "-f", "framecrc", "pipe:1"});
    if (!decode.waitForFinished(20000)) {
      decode.kill();
      decode.waitForFinished();
      return QString("Could not verify the first recorded video frame.");
    }
    bool decoded = false;
    for (const auto &line : decode.readAllStandardOutput().split('\n'))
      decoded |= !line.trimmed().isEmpty() && !line.startsWith('#');
    if (decode.exitCode() != 0 || !decoded)
      return QString("The saved video has no decodable frame.") +
             (discardEmpty(path) ? " No file was kept." : QString());
    if (audio && suppressStartupPop) {
      // Optional cleanup: it removes a capture-open pop but also removes the
      // start of speech. Leave recorded audio untouched by default.
      const QString processed = path + ".cleaning.mp4";
      QProcess cleanup;
      cleanup.start(
          "ffmpeg",
          {"-hide_banner", "-loglevel", "error", "-y", "-i", path, "-map", "0",
           "-c:v", "copy", "-af",
           "volume=enable='lt(t,0.4)':volume=0,afade=t=in:st=0.4:d=0.05",
           "-c:a", "aac", "-map_metadata", "-1", "-movflags", "+faststart",
           processed});
      if (!cleanup.waitForFinished(120000) || cleanup.exitCode() != 0) {
        cleanup.kill();
        cleanup.waitForFinished();
        QFile::remove(processed);
        return QString("The video is saved, but startup audio cleanup failed.");
      }
      if (std::rename(QFile::encodeName(processed).constData(),
                      QFile::encodeName(path).constData()) != 0)
        return QString(
            "Could not finish the audio cleanup. Both files were retained.");
    }
    return QString();
  }));
}

void Recorder::fail(const QString &message) {
  ++m_generation;
  m_tick.stop();
  m_startupCheck.stop();
  m_countdownTick.stop();
  m_pending = {};
  m_state = "failed";
  m_status = message;
  emit hideRequested();
  emit changed();
  emit setupRequested();
}
void Recorder::cancel() {
  if (active()) {
    stop();
    return;
  }
  ++m_generation;
  m_pending = {};
  m_state = "idle";
  emit changed();
  emit dismissRequested();
}
void Recorder::reset() {
  if (active() || m_state == "idle")
    return;
  ++m_generation;
  m_pending = {};
  m_capture = {};
  m_control = m_countdownControl = {};
  m_state = "idle";
  emit changed();
}
void Recorder::layoutChanged() {
  if (active()) {
    emit hideRequested();
    stop();
  } else if (m_state == "setup" || m_state == "selecting" ||
             m_state == "loading") {
    const bool show = m_showSetupWhenLoaded;
    m_capture = {};
    m_control = m_countdownControl = {};
    m_state = "idle";
    prepare(show);
  }
}
