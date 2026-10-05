#pragma once
#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <QVariantList>
#include <QVideoSink>
#include <functional>
#include <memory>

namespace CameraDevices {
int preferredIndex(const QVariantList &devices, const QByteArray &preferred);
}

class QCamera;
class QMediaCaptureSession;
class QMediaDevices;
class QVideoSink;

// A bounded, constant-rate track. Its timestamps come from the screen's
// recorded-time clock, so paused intervals never become camera frames.
class CameraTrack : public QObject {
  Q_OBJECT
public:
  static constexpr int Fps = 24;
  explicit CameraTrack(QObject *parent = nullptr);
  ~CameraTrack() override;
  void begin(const QString &path);
  void append(const QImage &frame, qint64 recordedMs);
  void end();
  bool busy() const { return m_process.state() != QProcess::NotRunning; }
  double duration() const { return double(m_frames) / Fps; }
signals:
  void finished(bool usable, const QString &error);

private:
  void fail(const QString &message);
  QProcess m_process;
  QTimer m_finishTimeout;
  QString m_path, m_error;
  qint64 m_frames = 0;
  bool m_ending = false;
};

class Webcam : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
  Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)
  Q_PROPERTY(int device READ device WRITE setDevice NOTIFY changed)
  Q_PROPERTY(bool ready READ ready NOTIFY changed)
  Q_PROPERTY(bool unavailable READ unavailable NOTIFY changed)
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(QVideoSink *previewSink READ previewSink WRITE setPreviewSink
                 NOTIFY changed)
public:
  explicit Webcam(QObject *parent = nullptr);
  ~Webcam() override;
  bool enabled() const { return m_enabled; }
  bool ready() const {
    return !m_enabled || (!m_latest.isNull() && m_frameAge.isValid() &&
                          m_frameAge.elapsed() < 1000);
  }
  bool unavailable() const { return m_unavailable; }
  QVariantList devices() const { return m_devices; }
  int device() const { return m_device; }
  QString status() const { return m_status; }
  QVideoSink *previewSink() const;
  void setPreviewSink(QVideoSink *sink);
  void setEnabled(bool enabled);
  void setDevice(int index);
  Q_INVOKABLE void refresh();
  void suspend();
  bool startTrack(const QString &path, std::function<qint64()> clock);
  void finishTrack();
  bool finishing() const { return m_track.busy(); }
signals:
  void changed();
  void trackFinished(const QString &path, double duration,
                     const QString &error);

private:
  void activate();
  std::unique_ptr<QCamera> m_camera;
  std::unique_ptr<QMediaCaptureSession> m_session;
  std::unique_ptr<QMediaDevices> m_mediaDevices;
  std::unique_ptr<QVideoSink> m_sink;
  QPointer<QVideoSink> m_preview;
  CameraTrack m_track;
  QTimer m_frameTick;
  QElapsedTimer m_frameAge;
  QImage m_latest;
  QVariantList m_devices;
  QString m_status = "Camera is off.", m_trackPath;
  std::function<qint64()> m_clock;
  int m_device = -1;
  bool m_enabled = false, m_unavailable = false;
  bool m_capturing = false;
};
