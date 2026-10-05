#include "delay-capture.hpp"
#include <algorithm>
DelayCapture::DelayCapture(QObject *parent, std::function<qint64()> clock)
    : QObject(parent), m_clock(std::move(clock)) {
  m_elapsed.start();
  if (!m_clock)
    m_clock = [this] { return m_elapsed.elapsed(); };
  m_timer.setInterval(50);
  connect(&m_timer, &QTimer::timeout, this, &DelayCapture::tick);
}
void DelayCapture::begin(int seconds) {
  if (active() || seconds < 1 || seconds > 30)
    return;
  ++m_generation;
  m_seconds = seconds;
  m_remaining = seconds;
  m_phase = Hiding;
  emit changed();
  emit hideRequested(m_generation);
}
void DelayCapture::desktopCleared(quint64 generation, bool success) {
  if (!current(generation) || m_phase != Hiding)
    return;
  if (!success) {
    cancel();
    emit failed();
    return;
  }
  m_deadline = m_clock() + m_seconds * 1000;
  m_phase = Countdown;
  m_timer.start();
  emit changed();
  emit badgeRequested();
}
void DelayCapture::tick() {
  if (m_phase != Countdown)
    return;
  const qint64 left = std::max(qint64(0), m_deadline - m_clock());
  const int remaining = int((left + 999) / 1000);
  if (remaining != m_remaining) {
    m_remaining = remaining;
    emit changed();
  }
  if (left == 0) {
    m_timer.stop();
    m_phase = Clearing;
    emit clearRequested(m_generation);
  }
}
void DelayCapture::badgeCleared(quint64 generation, bool success) {
  if (!current(generation) || m_phase != Clearing)
    return;
  if (!success) {
    cancel();
    emit failed();
    return;
  }
  m_phase = Capturing;
  emit grabRequested(m_generation);
}
void DelayCapture::cancel() {
  if (!active())
    return;
  ++m_generation;
  m_phase = Idle;
  m_timer.stop();
  m_remaining = 0;
  emit changed();
  emit cancelled();
}
void DelayCapture::complete(quint64 generation) {
  if (!current(generation) || m_phase != Capturing)
    return;
  m_phase = Idle;
  m_remaining = 0;
  emit changed();
}
