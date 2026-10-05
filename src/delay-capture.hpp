#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <functional>

// Owns a single delayed screenshot. Compositor acknowledgements and worker
// results carry the generation, so cancelling also cancels pending callbacks.
class DelayCapture final : public QObject {
  Q_OBJECT
public:
  explicit DelayCapture(QObject *parent = nullptr,
                        std::function<qint64()> clock = {});
  bool active() const { return m_phase != Idle; }
  int remaining() const { return m_remaining; }
  quint64 generation() const { return m_generation; }
  bool current(quint64 generation) const {
    return active() && generation == m_generation;
  }
  void begin(int seconds);
  void desktopCleared(quint64 generation, bool success);
  void badgeCleared(quint64 generation, bool success);
  void tick();
  void cancel();
  void complete(quint64 generation);
signals:
  void changed();
  void hideRequested(quint64 generation);
  void badgeRequested();
  void clearRequested(quint64 generation);
  void grabRequested(quint64 generation);
  void cancelled();
  void failed();

private:
  friend class DelayTest;
  enum Phase { Idle, Hiding, Countdown, Clearing, Capturing };
  Phase m_phase = Idle;
  QElapsedTimer m_elapsed;
  QTimer m_timer;
  std::function<qint64()> m_clock;
  qint64 m_deadline = 0;
  quint64 m_generation = 0;
  int m_seconds = 0, m_remaining = 0;
};
