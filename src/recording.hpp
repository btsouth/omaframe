#pragma once
#include "webcam.hpp"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QLocalSocket>
#include <QMargins>
#include <QObject>
#include <QProcess>
#include <QRect>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <memory>

namespace Recording {
struct Display {
  QString name;
  QRect bounds;
  QString label, place;
  /** Space reserved by bars and panels, in logical pixels. */
  QMargins reserved;
};
struct Placement {
  QString display;
  QRect bounds;
};
/** Where the timer and Stop control can sit during a recording without
 *  appearing in it. Beside a region on its own display when there is room,
 *  otherwise on the nearest other display, at the edge that faces the
 *  recording. Returns an empty placement when no position is outside the
 *  capture: Omaframe never puts its control inside the video. */
Placement placeStop(const QList<Display> &displays,
                    const QString &capturedDisplay, const QRect &capture,
                    QSize size = {232, 48});
/** A countdown shown before capture starts, when the control has nowhere
 *  to go during the recording. It is hidden before the first frame. */
Placement placeCountdown(const QList<Display> &displays,
                         const QString &capturedDisplay,
                         QSize size = {420, 48});
QStringList arguments(const QString &target, const QString &path,
                      const QString &desktopSource, const QString &micSource,
                      bool cursor);
/** The program and arguments that start GPU Screen Recorder. It runs with
 *  the plain name `gpu-screen-recorder` as its command, which is what the
 *  Omarchy bar's recording icon looks for. QProcess alone would start it as
 *  `/usr/bin/gpu-screen-recorder`, which the bar does not see or stop. */
QPair<QString, QStringList> recorderCommand(const QStringList &arguments);
} // namespace Recording

class Recorder final : public QObject {
  Q_OBJECT
  Q_PROPERTY(Webcam *camera READ camera CONSTANT)
  Q_PROPERTY(QString state READ state NOTIFY changed)
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(bool active READ active NOTIFY changed)
  Q_PROPERTY(QString elapsed READ elapsed NOTIFY changed)
  Q_PROPERTY(bool pausePending READ pausePending NOTIFY changed)
  Q_PROPERTY(int remaining READ remaining NOTIFY changed)
  Q_PROPERTY(QStringList displays READ displays NOTIFY changed)
  Q_PROPERTY(QVariantList microphones READ microphones NOTIFY changed)
  Q_PROPERTY(int microphone READ microphone WRITE setMicrophone NOTIFY changed)
  Q_PROPERTY(
      bool desktopAudio READ desktopAudio WRITE setDesktopAudio NOTIFY changed)
  Q_PROPERTY(bool micAudio READ micAudio WRITE setMicAudio NOTIFY changed)
  Q_PROPERTY(bool cursor READ cursor WRITE setCursor NOTIFY changed)
  Q_PROPERTY(bool suppressStartupPop READ suppressStartupPop WRITE setSuppressStartupPop NOTIFY changed)
  Q_PROPERTY(int countdown READ countdown WRITE setCountdown NOTIFY changed)
  Q_PROPERTY(QString targetLabel READ targetLabel NOTIFY changed)
  Q_PROPERTY(bool hasTarget READ hasTarget NOTIFY changed)
  Q_PROPERTY(bool canStart READ canStart NOTIFY changed)
  Q_PROPERTY(bool safeStop READ safeStop NOTIFY changed)
  Q_PROPERTY(bool hasControl READ hasControl NOTIFY changed)
  Q_PROPERTY(bool countdownOnly READ countdownOnly NOTIFY changed)
  Q_PROPERTY(bool stopShortcut READ stopShortcut NOTIFY changed)
  Q_PROPERTY(QString stopKey READ stopKey NOTIFY changed)
  Q_PROPERTY(QString pauseKey READ pauseKey NOTIFY changed)
  Q_PROPERTY(bool needsStopShortcut READ needsStopShortcut NOTIFY changed)
  Q_PROPERTY(QString controlLocation READ controlLocation NOTIFY changed)
  Q_PROPERTY(QString savedPath READ savedPath NOTIFY changed)
public:
  explicit Recorder(QObject *parent = nullptr);
  Webcam *camera() { return &m_webcam; }
  ~Recorder() override;
  QString state() const { return m_state; }
  QString status() const { return m_status; }
  bool active() const;
  QString elapsed() const;
  bool pausePending() const { return m_pausePending; }
  int remaining() const { return m_remaining; }
  QStringList displays() const;
  QVariantList microphones() const { return m_mics; }
  int microphone() const { return m_mic; }
  bool desktopAudio() const { return m_desktop; }
  bool micAudio() const { return m_microphone; }
  bool cursor() const { return m_cursor; }
  bool suppressStartupPop() const { return m_suppressStartupPop; }
  int countdown() const { return m_countdown; }
  void setMicrophone(int);
  void setDesktopAudio(bool);
  void setMicAudio(bool);
  void setCursor(bool);
  void setSuppressStartupPop(bool);
  void setCountdown(int);
  QString targetLabel() const;
  bool hasTarget() const { return !m_capture.isEmpty(); }
  bool canStart() const;
  /** Every control Omaframe shows during a recording is outside it. */
  bool safeStop() const { return hasControl(); }
  bool hasControl() const { return !m_control.bounds.isEmpty(); }
  bool countdownOnly() const {
    return !hasControl() && m_state == "countdown";
  }
  bool stopShortcut() const { return !m_stopKey.isEmpty(); }
  QString stopKey() const { return m_stopKey; }
  QString pauseKey() const { return m_pauseKey; }
  /** The chosen area leaves no room for a Stop button and no shortcut stops
   *  Omaframe yet. */
  bool needsStopShortcut() const {
    return hasTarget() && !hasControl() && !stopShortcut();
  }
  QString controlLocation() const;
  QString savedPath() const { return m_path; }
  Recording::Placement control() const { return m_control; }
  /** The surface to show now: the recording control, or the countdown. */
  Recording::Placement visibleControl() const;
  /** Loads displays, audio sources and shortcut state. Setup is shown only
   *  when `showSetup` is true; the capture bar loads it in the background. */
  Q_INVOKABLE void prepare(bool showSetup = true);
  Q_INVOKABLE void selectDisplay(int index);
  /** Records the whole display named `name` once options are loaded. */
  void selectDisplayNamed(const QString &name, bool startAfterSelection);
  Q_INVOKABLE void chooseRegion();
  void regionSelected(const QString &screen, const QRectF &normalized,
                      bool startAfterSelection = false);
  Q_INVOKABLE void start();
  Q_INVOKABLE void stop();
  /** Returns false when recording is not ready or another request is pending.
   *  pauseFinished reports the backend's reply, including idempotent requests.
   */
  Q_INVOKABLE bool setPaused(bool paused);
  Q_INVOKABLE void togglePause() { setPaused(m_state != "paused"); }
  Q_INVOKABLE void cancel();
  /** Shows setup for the current target, e.g. from the capture bar. */
  Q_INVOKABLE void showSetup();
  void setStopKey(const QString &key);
  void setPauseKey(const QString &key);
  /** Forgets an unstarted setup, e.g. when the selector is cancelled. */
  void reset();
  void layoutChanged();
signals:
  void changed();
  void setupRequested();
  void selectionRequested();
  void hideRequested();
  void controlRequested();
  void dismissRequested();
  void completed(const QUrl &path);
  void pauseFinished(bool success);

private:
  void setTarget(const QString &, const QRect &, bool full);
  void applyPending();
  void updateSetupStatus();
  /** The display's name inside a sentence, e.g. "left display". */
  QString place(const QString &display) const;
  void launch();
  void refreshBar() const;
  void fail(const QString &);
  void validateResult(int exitCode, QProcess::ExitStatus status);
  void finishPause(bool success, const QString &error = {},
                   bool uncertain = false);
  void freezeClock();
  void completeWhenCameraReady();
  Webcam m_webcam;
  bool m_screenReady = false;
  QString m_cameraWarning;
  QString m_state = "idle", m_status, m_screen, m_target, m_path, m_error,
          m_defaultSink, m_preferredMic, m_stopKey, m_pauseKey;
  QList<Recording::Display> m_displays;
  QVariantList m_mics;
  Recording::Placement m_control, m_countdownControl;
  QRect m_capture;
  QProcess m_process;
  QTimer m_tick, m_countdownTick, m_startupCheck;
  QElapsedTimer m_clock;
  QLocalSocket m_pauseSocket;
  QTimer m_pauseTimeout;
  std::unique_ptr<QTemporaryDir> m_controlDir;
  qint64 m_recordedMs = 0;
  bool m_pausePending = false, m_pauseTarget = false, m_pauseSent = false;
  const bool m_barStop =
      !QStandardPaths::findExecutable("omarchy-shell").isEmpty();
  bool m_desktop = false, m_microphone = false, m_cursor = true, m_full = false;
  bool m_suppressStartupPop = false;
  bool m_frameTimedOut = false;
  bool m_otherRecorder = false;
  bool m_showSetupWhenLoaded = true;
  struct Pending {
    QString display;
    QRectF area;
    bool full = false, start = false, valid = false;
  } m_pending;
  int m_mic = -1, m_countdown = 3, m_remaining = 0, m_generation = 0;
};
