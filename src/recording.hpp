#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QRect>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

namespace Recording {
struct Display {
  QString name;
  QRect bounds;
  QString label, place;
};
struct Placement {
  QString display;
  QRect bounds;
};
Placement placeStop(const QList<Display> &displays,
                    const QString &capturedDisplay, const QRect &capture,
                    QSize size = {280, 56});
QStringList arguments(const QString &target, const QString &path,
                      const QString &desktopSource, const QString &micSource,
                      bool cursor);
} // namespace Recording

class Recorder final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString state READ state NOTIFY changed)
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(bool active READ active NOTIFY changed)
  Q_PROPERTY(QString elapsed READ elapsed NOTIFY changed)
  Q_PROPERTY(int remaining READ remaining NOTIFY changed)
  Q_PROPERTY(QStringList displays READ displays NOTIFY changed)
  Q_PROPERTY(QVariantList microphones READ microphones NOTIFY changed)
  Q_PROPERTY(int microphone READ microphone WRITE setMicrophone NOTIFY changed)
  Q_PROPERTY(
      bool desktopAudio READ desktopAudio WRITE setDesktopAudio NOTIFY changed)
  Q_PROPERTY(bool micAudio READ micAudio WRITE setMicAudio NOTIFY changed)
  Q_PROPERTY(bool cursor READ cursor WRITE setCursor NOTIFY changed)
  Q_PROPERTY(int countdown READ countdown WRITE setCountdown NOTIFY changed)
  Q_PROPERTY(QString targetLabel READ targetLabel NOTIFY changed)
  Q_PROPERTY(bool canStart READ canStart NOTIFY changed)
  Q_PROPERTY(bool safeStop READ safeStop NOTIFY changed)
  Q_PROPERTY(QString controlLocation READ controlLocation NOTIFY changed)
  Q_PROPERTY(QString savedPath READ savedPath NOTIFY changed)
public:
  explicit Recorder(QObject *parent = nullptr);
  QString state() const { return m_state; }
  QString status() const { return m_status; }
  bool active() const;
  QString elapsed() const;
  int remaining() const { return m_remaining; }
  QStringList displays() const;
  QVariantList microphones() const { return m_mics; }
  int microphone() const { return m_mic; }
  bool desktopAudio() const { return m_desktop; }
  bool micAudio() const { return m_microphone; }
  bool cursor() const { return m_cursor; }
  int countdown() const { return m_countdown; }
  void setMicrophone(int);
  void setDesktopAudio(bool);
  void setMicAudio(bool);
  void setCursor(bool);
  void setCountdown(int);
  QString targetLabel() const;
  bool canStart() const;
  bool safeStop() const { return !m_control.bounds.isEmpty(); }
  QString controlLocation() const;
  QString savedPath() const { return m_path; }
  Recording::Placement control() const { return m_control; }
  Q_INVOKABLE void prepare();
  Q_INVOKABLE void selectDisplay(int index);
  Q_INVOKABLE void chooseRegion();
  void regionSelected(const QString &screen, const QRectF &normalized);
  Q_INVOKABLE void start();
  Q_INVOKABLE void stop();
  Q_INVOKABLE void cancel();
  void layoutChanged();
signals:
  void changed();
  void setupRequested();
  void selectionRequested();
  void hideRequested();
  void controlRequested();
  void dismissRequested();
  void completed(const QUrl &path);

private:
  void setTarget(const QString &, const QRect &, bool full);
  /** The display's name inside a sentence, e.g. "left display". */
  QString place(const QString &display) const;
  void launch();
  void fail(const QString &);
  void validateResult(int exitCode, QProcess::ExitStatus status);
  QString m_state = "idle", m_status, m_screen, m_target, m_path, m_error,
          m_defaultSink, m_preferredMic;
  QList<Recording::Display> m_displays;
  QVariantList m_mics;
  Recording::Placement m_control;
  QRect m_capture;
  QProcess m_process;
  QTimer m_tick, m_countdownTick, m_startupCheck;
  QElapsedTimer m_clock;
  bool m_desktop = false, m_microphone = false, m_cursor = true, m_full = false;
  int m_mic = -1, m_countdown = 3, m_remaining = 0, m_generation = 0;
};
