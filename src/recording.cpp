#include "recording.hpp"
#include "displays.hpp"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <QtConcurrent>
#include <algorithm>
#include <csignal>
#include <cstdio>

Recording::Placement Recording::placeStop(const QList<Display> &displays,
                                          const QString &capturedDisplay,
                                          const QRect &capture, QSize size) {
  // Keep a gap on every side, including for rounding at fractional scales.
  constexpr int gap = 16;
  auto fits = [&](const Display &d, QPoint point) {
    QRect rect(point, size);
    return d.bounds.adjusted(gap, gap, -gap, -gap).contains(rect) &&
           !capture.adjusted(-gap, -gap, gap, gap).intersects(rect);
  };
  for (const auto &d : displays) {
    if (d.name != capturedDisplay)
      continue;
    const int x = std::clamp(
        capture.center().x() - size.width() / 2, d.bounds.x() + gap,
        std::max(d.bounds.x() + gap, d.bounds.right() - size.width() - gap));
    for (QPoint point : {QPoint(x, capture.bottom() + gap + 1),
                         QPoint(x, capture.top() - size.height() - gap - 1)})
      if (fits(d, point))
        return {d.name, QRect(point, size)};
  }
  for (const auto &d : displays) {
    if (d.name == capturedDisplay)
      continue;
    QPoint point(d.bounds.center().x() - size.width() / 2, d.bounds.y() + 40);
    if (fits(d, point))
      return {d.name, QRect(point, size)};
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
Recorder::Recorder(QObject *parent) : QObject(parent) {
  QSettings s;
  m_desktop = s.value("record/desktop", false).toBool();
  m_microphone = s.value("record/microphone", false).toBool();
  m_cursor = s.value("record/cursor", true).toBool();
  m_countdown = std::clamp(s.value("record/countdown", 3).toInt(), 0, 5);
  m_preferredMic = s.value("record/micSource").toString();
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
    if (QFileInfo(m_path).size() > 0 &&
        m_process.state() == QProcess::Running) {
      m_state = "recording";
      m_status = "Recording";
      m_clock.restart();
      m_tick.start();
      m_startupCheck.stop();
      emit changed();
    } else if (m_clock.elapsed() > 12000) {
      m_status = "Recorder is still starting. You can stop safely.";
      emit changed();
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
#define OPTION(Setter, Type, Member)                                           \
  void Recorder::Setter(Type v) {                                              \
    if (active())                                                              \
      return;                                                                  \
    Member = v;                                                                \
    emit changed();                                                            \
  }
OPTION(setDesktopAudio, bool, m_desktop)
OPTION(setMicAudio, bool, m_microphone)
OPTION(setCursor, bool, m_cursor)
void Recorder::setMicrophone(int i) {
  if (!active() && i >= 0 && i < m_mics.size()) {
    m_mic = i;
    m_preferredMic = m_mics[i].toMap().value("id").toString();
    emit changed();
  }
}
void Recorder::setCountdown(int seconds) {
  if (!active()) {
    m_countdown = std::clamp(seconds, 0, 5);
    emit changed();
  }
}
QString Recorder::targetLabel() const {
  if (m_capture.isEmpty())
    return "Select an area or display";
  if (m_full)
    for (const auto &d : m_displays)
      if (d.name == m_screen)
        return "Entire display · " + d.label;
  return QString("Region · %1 × %2 on the %3")
      .arg(m_capture.width())
      .arg(m_capture.height())
      .arg(place(m_screen));
}
bool Recorder::canStart() const {
  return !active() && m_state != "loading" && !m_capture.isEmpty() &&
         (!m_microphone || m_mic >= 0) &&
         (!m_desktop || !m_defaultSink.isEmpty());
}
QString Recorder::controlLocation() const {
  if (m_capture.isEmpty())
    return "The Stop control will stay outside the recorded area.";
  if (!safeStop())
    return "No room for a stop button outside this capture. Use Alt+Print to "
           "stop, or select a smaller area.";
  return m_control.display == m_screen
             ? "Stop button stays outside your selected area."
             : "Stop button stays on the " + place(m_control.display) +
                   ", outside the recording.";
}
void Recorder::prepare() {
  if (active() || m_state == "loading")
    return;
  const int generation = ++m_generation;
  m_state = "loading";
  m_status = "Checking displays and audio…";
  emit changed();
  emit setupRequested();
  struct Result {
    QList<Recording::Display> displays;
    QVariantList mics;
    QString sink, defaultMic;
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
            m_status =
                r.other ? "Another screen recorder is running. Stop it before "
                          "starting an Omaframe recording."
                : m_microphone && m_mic < 0
                    ? "Your selected microphone is unavailable. Choose a "
                      "microphone or turn it off."
                    : m_desktop && m_defaultSink.isEmpty()
                    ? "Desktop audio is unavailable. Turn it off or reconnect your output."
                    : "Choose what to capture.";
            emit changed();
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
        r.displays.append({o.value("name").toString(), bounds});
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
    return r;
  }));
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
  m_state = "setup";
  m_status = "Ready to record.";
  emit changed();
}
void Recorder::selectDisplay(int i) {
  if (active() || i < 0 || i >= m_displays.size())
    return;
  setTarget(m_displays[i].name, m_displays[i].bounds, true);
}
void Recorder::chooseRegion() {
  if (active() || m_state == "loading")
    return;
  m_state = "selecting";
  emit changed();
  emit selectionRequested();
}
void Recorder::regionSelected(const QString &name, const QRectF &normalized) {
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
  m_path.clear();
  QSettings s;
  s.setValue("record/desktop", m_desktop);
  s.setValue("record/microphone", m_microphone);
  s.setValue("record/cursor", m_cursor);
  s.setValue("record/countdown", m_countdown);
  if (m_mic >= 0)
    s.setValue("record/micSource", m_mics[m_mic].toMap().value("id"));
  emit changed();
  emit hideRequested();
  if (safeStop())
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
  emit changed();
  const int generation = m_generation;
  struct Check {
    QString error;
  };
  const auto target = m_capture;
  const bool hasControl = safeStop();
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
        m_process.start(
            "gpu-screen-recorder",
            Recording::arguments(m_target, m_path,
                                 m_desktop ? m_defaultSink : QString(), mic,
                                 m_cursor));
        m_startupCheck.start();
        emit changed();
      });
  watcher->setFuture(QtConcurrent::run([target, hasControl] {
    if (!command("pgrep", {"-f", "^([^ ]*/)?gpu-screen-recorder( |$)"})
             .isEmpty())
      return Check{"Another screen recorder is running. Stop it first."};
    if (hasControl) {
      bool found = false;
      auto layers =
          QJsonDocument::fromJson(command("hyprctl", {"-j", "layers"}))
              .object();
      for (auto mon : layers)
        for (auto level : mon.toObject().value("levels").toObject())
          for (auto value : level.toArray()) {
            auto layer = value.toObject();
            if (layer.value("namespace").toString() !=
                "omaframe-record-control")
              continue;
            found = true;
            QRect actual(layer.value("x").toInt(), layer.value("y").toInt(),
                         layer.value("w").toInt(), layer.value("h").toInt());
            if (actual.isEmpty() ||
                target.adjusted(-8, -8, 8, 8).intersects(actual))
              return Check{"The stop control could overlap the recording. "
                           "Select another area."};
          }
      if (!found)
        return Check{"The stop control did not appear safely. Select another "
                     "area and retry."};
    }
    return Check{};
  }));
}
void Recorder::stop() {
  if (m_state == "countdown" ||
      (m_state == "starting" && m_process.state() == QProcess::NotRunning)) {
    ++m_generation;
    m_countdownTick.stop();
    m_state = "setup";
    emit hideRequested();
    emit changed();
    emit setupRequested();
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
void Recorder::validateResult(int code, QProcess::ExitStatus exitStatus) {
  m_startupCheck.stop();
  m_tick.stop();
  emit hideRequested();
  if (code != 0 || exitStatus != QProcess::NormalExit) {
    fail("Recording failed. " + m_error.simplified().right(500) +
         (QFileInfo::exists(m_path) ? " File retained: " + m_path : QString()));
    return;
  }
  m_state = "stopping";
  m_status = "Checking the saved video…";
  emit changed();
  auto *watcher = new QFutureWatcher<QString>(this);
  const auto path = m_path;
  connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher] {
    const QString error = watcher->result();
    watcher->deleteLater();
    if (!error.isEmpty()) {
      fail(error + " File retained: " + m_path);
      return;
    }
    m_state = "saved";
    m_status = "Recording saved.";
    emit changed();
    emit completed(QUrl::fromLocalFile(m_path));
  });
  watcher->setFuture(QtConcurrent::run([path] {
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
                          .toDouble() <= 0)
      return QString("The recorder stopped without a readable video.");
    if (audio) {
      // Preserve this desktop's existing capture-open pop suppression, without
      // loudness normalization or re-encoding video frames.
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
  m_state = "idle";
  emit changed();
  emit dismissRequested();
}
void Recorder::layoutChanged() {
  if (active()) {
    emit hideRequested();
    stop();
    if (m_state == "setup") {
      m_capture = {};
      m_control = {};
      m_state = "idle";
      prepare();
    }
  } else if (m_state == "setup" || m_state == "selecting") {
    m_capture = {};
    m_control = {};
    m_state = "idle";
    prepare();
  }
}
