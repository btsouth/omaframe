#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <QTimer>
#include <QVariantMap>
#include <memory>

struct AudioSnapshot {
  QString mic, sound, micLabel, soundLabel;
  bool operator==(const AudioSnapshot &) const = default;
};

// Commands are asynchronous. Backends report immutable, generation-tagged data.
class AudioLevelBackend : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;
  virtual void open(const AudioSnapshot &, quint64 generation) = 0;
  virtual void close() = 0;
signals:
  void samples(int channel, quint64 generation, const QList<float> &peaks);
  void unavailable(int channel, quint64 generation);
};
std::unique_ptr<AudioLevelBackend> pulseAudioBackend();

class AudioLevels final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantMap microphone READ microphone NOTIFY changed)
  Q_PROPERTY(QVariantMap sound READ sound NOTIFY changed)
public:
  explicit AudioLevels(QObject *parent = nullptr);
  explicit AudioLevels(std::unique_ptr<AudioLevelBackend>, QObject *parent = nullptr, bool automaticClock = true);
  ~AudioLevels() override;
  QVariantMap microphone() const { return value(0); }
  QVariantMap sound() const { return value(1); }
  void configure(const AudioSnapshot &preview, const AudioSnapshot &session,
                 const QString &state, bool micOn, bool soundOn);
  // Each selector has its own token, so several displays cannot hide each other.
  Q_INVOKABLE void setSurface(const QString &token, const QString &kind, bool visible);
  static double decibels(const QList<float> &peaks);
  // Also used with a deterministic clock by the headless tests.
  void advance(qint64 now);
signals:
  void changed();
private:
  struct Channel {
    QString state = "Off", name, label;
    double db = -60, raw = -60, held = -60;
    qint64 sampleAt = -1, holdUntil = 0, clipUntil = 0;
  } m_channels[2];
  QVariantMap value(int) const;
  void reconcile();
  void receive(int, quint64, const QList<float> &);
  std::unique_ptr<AudioLevelBackend> m_backend;
  QHash<QString, QString> m_surfaces;
  AudioSnapshot m_preview, m_session, m_open;
  QString m_state = "idle";
  bool m_micOn = false, m_soundOn = false;
  const bool m_automaticClock;
  quint64 m_generation = 0;
  qint64 m_now = 0, m_previous = 0, m_openedAt = 0;
  QElapsedTimer m_clock;
  QTimer m_tick;
};
