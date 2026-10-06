#include "audio-levels.hpp"
#include <algorithm>
#include <cmath>

AudioLevels::AudioLevels(QObject *parent) : AudioLevels(pulseAudioBackend(), parent) {}
AudioLevels::AudioLevels(std::unique_ptr<AudioLevelBackend> backend, QObject *parent, bool automaticClock)
    : QObject(parent), m_backend(std::move(backend)), m_automaticClock(automaticClock) {
  connect(m_backend.get(), &AudioLevelBackend::samples, this, &AudioLevels::receive);
  connect(m_backend.get(), &AudioLevelBackend::unavailable, this,
          [this](int i, quint64 generation) {
            if (generation != m_generation || i < 0 || i > 1 || m_channels[i].state == "Off")
              return;
            m_channels[i].state = "Unavailable";
            m_channels[i].db = m_channels[i].held = -60;
            m_channels[i].clipUntil = 0;
            emit changed();
          });
  m_clock.start();
  m_tick.setInterval(40);
  connect(&m_tick, &QTimer::timeout, this, [this] { advance(m_clock.elapsed()); });
}
AudioLevels::~AudioLevels() { m_backend->close(); }
double AudioLevels::decibels(const QList<float> &peaks) {
  double peak = 0;
  for (float p : peaks)
    if (std::isfinite(p))
      peak = std::max(peak, double(std::abs(p)));
  return peak > 0 ? std::clamp(20 * std::log10(peak), -60.0, 0.0) : -60;
}
QVariantMap AudioLevels::value(int i) const {
  const auto &c = m_channels[i];
  return {{"state", c.state}, {"source", c.name}, {"device", c.label},
          {"db", c.db}, {"held", c.held}, {"clip", c.clipUntil > m_now}};
}
void AudioLevels::configure(const AudioSnapshot &preview, const AudioSnapshot &session,
                            const QString &state, bool micOn, bool soundOn) {
  m_preview = preview; m_session = session; m_state = state;
  m_micOn = micOn; m_soundOn = soundOn;
  reconcile();
}
void AudioLevels::setSurface(const QString &token, const QString &kind, bool visible) {
  if (visible) m_surfaces.insert(token, kind);
  else m_surfaces.remove(token);
  reconcile();
}
void AudioLevels::reconcile() {
  const bool previewState = m_state == "setup" || m_state == "selecting" || m_state == "loading";
  // The options window shows live levels whenever it is visible, including
  // after a failure or cancel, except while a recording owns the meters.
  const bool optionsState = previewState || m_state == "idle" || m_state == "failed";
  bool needed = false;
  for (const auto &kind : m_surfaces)
    needed |= (previewState && kind == "bar") ||
              (optionsState && kind == "options") ||
              (m_state == "recording" && kind == "control");
  const auto &snapshot = m_state == "recording" ? m_session : m_preview;
  AudioSnapshot wanted = snapshot;
  if (!needed || !m_micOn) wanted.mic.clear();
  if (!needed || !m_soundOn) wanted.sound.clear();
  // Labels can change without reopening a pinned stream.
  if (m_automaticClock && !m_tick.isActive()) m_previous = m_now = m_clock.elapsed();
  const bool reopen = wanted.mic != m_open.mic || wanted.sound != m_open.sound;
  if (reopen) {
    ++m_generation;
    m_backend->close();
    m_open = wanted;
    m_openedAt = m_now;
  }
  for (int i = 0; i < 2; ++i) {
    auto &c = m_channels[i];
    const bool on = i == 0 ? m_micOn : m_soundOn;
    const QString name = i == 0 ? wanted.mic : wanted.sound;
    const QString label = i == 0 ? snapshot.micLabel : snapshot.soundLabel;
    if (reopen || !needed || !on || name.isEmpty()) {
      c = {};
      c.state = !on || !needed ? "Off" : name.isEmpty() ?
                (m_state == "loading" ? "Checking" : "Unavailable") : "Checking";
    }
    c.name = i == 0 ? snapshot.mic : snapshot.sound;
    c.label = label;
  }
  if (reopen && (!wanted.mic.isEmpty() || !wanted.sound.isEmpty()))
    m_backend->open(wanted, m_generation);
  if (m_automaticClock && needed && (m_micOn || m_soundOn)) m_tick.start();
  else m_tick.stop();
  emit changed();
}
void AudioLevels::receive(int i, quint64 generation, const QList<float> &peaks) {
  if (generation != m_generation || i < 0 || i > 1 || m_channels[i].state == "Off") return;
  if (std::none_of(peaks.begin(), peaks.end(), [](float p) { return std::isfinite(p); })) return;
  if (m_automaticClock) m_now = m_clock.elapsed();
  auto &c = m_channels[i];
  const double db = decibels(peaks);
  c.sampleAt = m_now;
  c.raw = db;
  c.db = std::max(c.db, db);
  if (db >= c.held) { c.held = db; c.holdUntil = m_now + 1000; }
  if (db >= -0.5) c.clipUntil = m_now + 2000;
  c.state = c.db <= -60 ? "Quiet" : "Level";
  // QML gets at most one update per 40 ms tick.
}
void AudioLevels::advance(qint64 now) {
  const qint64 delta = std::max(qint64(0), now - m_previous);
  m_previous = m_now = now;
  for (auto &c : m_channels) {
    if (c.state == "Off") continue;
    if ((c.sampleAt < 0 && now - m_openedAt > 2000) ||
        (c.sampleAt >= 0 && now - c.sampleAt > 1000)) {
      c.state = "Unavailable"; c.db = c.held = -60; c.clipUntil = 0;
    } else if (c.state == "Level" || c.state == "Quiet") {
      // Instant attack, 300 ms release across the display's 60 dB range.
      if (c.sampleAt < now) c.db = std::max(c.raw, c.db - delta * 0.2);
      if (now > c.holdUntil) c.held = std::max(c.db, c.held - delta * 0.2);
      c.state = c.db <= -60 ? "Quiet" : "Level";
    }
  }
  emit changed();
}
