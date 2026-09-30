#include "webcam.hpp"
#include <QCamera>
#include <QCameraDevice>
#include <QFile>
#include <QFileInfo>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QPainter>
#include <QSettings>
#include <QVideoFrame>
#include <QVideoSink>
#include <algorithm>
#include <csignal>
#include <sys/prctl.h>
#include <utility>

CameraTrack::CameraTrack(QObject *parent) : QObject(parent) {
  m_process.setChildProcessModifier([] { ::prctl(PR_SET_PDEATHSIG, SIGTERM); });
  m_finishTimeout.setSingleShot(true);
  m_finishTimeout.setInterval(5000);
  connect(&m_finishTimeout, &QTimer::timeout, this, [this] {
    qWarning() << "Camera finish timed out, frames:" << m_frames
               << "queued bytes:" << m_process.bytesToWrite();
    if (m_error.isEmpty())
      m_error =
          "Camera recording could not finish. The screen video is still saved.";
    m_process.kill();
  });
  connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
    const auto error = m_process.readAllStandardError();
    if (!error.isEmpty())
      qWarning().noquote() << "Camera encoder:" << error;
  });
  connect(&m_process, &QProcess::errorOccurred, this,
          [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
              m_finishTimeout.stop();
              emit finished(false, "Couldn't start camera recording. Check "
                                   "that FFmpeg is installed.");
            }
          });
  connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
          this, [this](int code, QProcess::ExitStatus status) {
            m_finishTimeout.stop();
            const bool usable = code == 0 && status == QProcess::NormalExit &&
                                m_frames > 0 &&
                                QFileInfo(m_path).size() >= 1024;
            if (!usable) {
              QFile::remove(m_path);
              if (m_error.isEmpty())
                m_error =
                    "Camera recording failed. The screen video is still saved.";
            }
            emit finished(usable, m_error);
          });
}
CameraTrack::~CameraTrack() {
  if (busy()) {
    m_process.kill();
    m_process.waitForFinished(1000);
  }
}
void CameraTrack::begin(const QString &path) {
  if (busy())
    return;
  m_path = path;
  m_error.clear();
  m_frames = 0;
  m_ending = false;
  m_process.start("ffmpeg", {"-hide_banner",
                             "-loglevel",
                             "error",
                             "-nostdin",
                             "-n",
                             "-probesize",
                             "32",
                             "-analyzeduration",
                             "0",
                             "-fpsprobesize",
                             "0",
                             "-f",
                             "rawvideo",
                             "-pixel_format",
                             "rgba",
                             "-video_size",
                             "640x360",
                             "-framerate",
                             QString::number(Fps),
                             "-i",
                             "pipe:0",
                             "-an",
                             "-c:v",
                             "libx264",
                             "-threads",
                             "2",
                             "-preset",
                             "ultrafast",
                             "-crf",
                             "22",
                             "-pix_fmt",
                             "yuv420p",
                             "-map_metadata",
                             "-1",
                             "-movflags",
                             "+faststart",
                             path});
}
void CameraTrack::fail(const QString &message) {
  qWarning() << "Camera track stopped, frames:" << m_frames
             << "queued bytes:" << m_process.bytesToWrite();
  if (m_error.isEmpty())
    m_error = message;
  end();
}
void CameraTrack::append(const QImage &frame, qint64 recordedMs) {
  if (m_ending || frame.isNull() || recordedMs < 0 ||
      m_process.state() != QProcess::Running)
    return;
  const qint64 wanted = recordedMs * Fps / 1000 + 1;
  if (wanted <= m_frames)
    return;
  constexpr qint64 frameBytes = 640 * 360 * 4;
  if (wanted - m_frames > Fps || m_process.bytesToWrite() > frameBytes * 8) {
    fail("Camera recording stopped because it fell behind. The screen "
         "recording continues.");
    return;
  }
  // Fixed-size frames bound both CPU work and the encoder's pending queue.
  QImage image(640, 360, QImage::Format_RGBA8888);
  image.fill(Qt::black);
  const QImage fit =
      frame.scaled(640, 360, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  QPainter painter(&image);
  painter.drawImage((640 - fit.width()) / 2, (360 - fit.height()) / 2, fit);
  painter.end();
  const QByteArray bytes(reinterpret_cast<const char *>(image.constBits()),
                         frameBytes);
  while (m_frames < wanted) {
    if (m_process.bytesToWrite() > frameBytes * 8 ||
        m_process.write(bytes) != bytes.size()) {
      fail("Camera recording stopped because it fell behind. The screen "
           "recording continues.");
      return;
    }
    ++m_frames;
  }
}
void CameraTrack::end() {
  if (!busy() || m_ending)
    return;
  m_ending = true;
  m_process.closeWriteChannel();
  m_finishTimeout.start();
}

Webcam::Webcam(QObject *parent) : QObject(parent) {
  m_frameTick.setInterval(1000 / CameraTrack::Fps);
  m_frameTick.setTimerType(Qt::PreciseTimer);
  connect(&m_frameTick, &QTimer::timeout, this, [this] {
    if (!m_track.busy())
      return;
    if (!ready()) {
      m_status =
          "Camera stopped sending video. The screen recording continues.";
      m_frameTick.stop();
      m_track.end();
      emit changed();
      return;
    }
    const qint64 time = m_clock ? m_clock() : -1;
    if (time >= 0)
      m_track.append(m_latest, time);
  });
  connect(&m_track, &CameraTrack::finished, this,
          [this](bool usable, const QString &error) {
            m_frameTick.stop();
            const QString message = !error.isEmpty() ? error
                                    : m_status.startsWith("Camera stopped")
                                        ? m_status
                                        : QString();
            if (!message.isEmpty())
              m_status = message;
            const QString path = std::exchange(m_trackPath, {});
            suspend();
            emit changed();
            emit trackFinished(usable ? path : QString(), m_track.duration(),
                               message);
          });
}
Webcam::~Webcam() {
  m_frameTick.stop();
  m_track.disconnect(this);
  if (m_camera)
    m_camera->disconnect(this);
  if (m_sink)
    m_sink->disconnect(this);
  if (m_mediaDevices)
    m_mediaDevices->disconnect(this);
  suspend();
  if (m_session)
    m_session->setCamera(nullptr);
}
QVideoSink *Webcam::previewSink() const { return m_preview; }
void Webcam::setPreviewSink(QVideoSink *sink) { m_preview = sink; }
void Webcam::refresh() {
  if (m_track.busy())
    return;
  if (!m_mediaDevices) {
    m_mediaDevices = std::make_unique<QMediaDevices>();
    connect(m_mediaDevices.get(), &QMediaDevices::videoInputsChanged, this,
            &Webcam::refresh);
  }
  m_devices.clear();
  const auto cameras = QMediaDevices::videoInputs();
  const QByteArray preferred =
      QSettings().value("record/cameraDevice").toByteArray();
  m_device = cameras.isEmpty() ? -1 : 0;
  for (int i = 0; i < cameras.size(); ++i) {
    m_devices.append(QVariantMap{{"label", cameras[i].description()},
                                 {"id", cameras[i].id()}});
    if (cameras[i].id() == preferred)
      m_device = i;
  }
  if (m_enabled)
    activate();
  emit changed();
}
void Webcam::setEnabled(bool enabled) {
  if (m_track.busy() || m_enabled == enabled)
    return;
  m_enabled = enabled;
  if (enabled) {
    if (!m_mediaDevices)
      refresh();
    else
      activate();
  } else {
    suspend();
    m_status = "Camera is off.";
  }
  emit changed();
}
void Webcam::setDevice(int index) {
  if (m_track.busy() || index < 0 || index >= m_devices.size() ||
      index == m_device)
    return;
  m_device = index;
  QSettings().setValue("record/cameraDevice",
                       m_devices[index].toMap().value("id"));
  if (m_enabled)
    activate();
  emit changed();
}
void Webcam::suspend() {
  m_capturing = false;
  if (m_camera)
    m_camera->stop();
  m_latest = {};
  m_frameAge.invalidate();
  if (m_preview)
    m_preview->setVideoFrame({});
}
void Webcam::activate() {
  suspend();
  const auto cameras = QMediaDevices::videoInputs();
  if (m_device < 0 || m_device >= cameras.size()) {
    m_status = "No camera found. Connect a camera or turn Camera off.";
    emit changed();
    return;
  }
  if (!m_session) {
    m_session = std::make_unique<QMediaCaptureSession>();
    m_sink = std::make_unique<QVideoSink>();
    m_session->setVideoSink(m_sink.get());
    connect(m_sink.get(), &QVideoSink::videoFrameChanged, this,
            [this](const QVideoFrame &frame) {
              if (!m_enabled || !m_capturing || !frame.isValid())
                return;
              if (m_preview)
                m_preview->setVideoFrame(frame);
              m_latest = frame.toImage();
              const bool first = !m_frameAge.isValid();
              m_frameAge.restart();
              if (first) {
                m_status =
                    "Camera ready. You can move or hide it after recording.";
                emit changed();
              }
            });
  }
  m_session->setCamera(nullptr);
  m_camera = std::make_unique<QCamera>(cameras[m_device]);
  // Prefer a modest, wide camera stream. The overlay does not need a 4K feed.
  QCameraFormat chosen;
  for (const auto &format : cameras[m_device].videoFormats()) {
    if (format.resolution().width() > 1280 ||
        format.maxFrameRate() < CameraTrack::Fps)
      continue;
    if (chosen.isNull() ||
        qAbs(double(format.resolution().width()) /
                 format.resolution().height() -
             16.0 / 9) < qAbs(double(chosen.resolution().width()) /
                                  chosen.resolution().height() -
                              16.0 / 9))
      chosen = format;
  }
  if (!chosen.isNull())
    m_camera->setCameraFormat(chosen);
  connect(m_camera.get(), &QCamera::errorOccurred, this,
          [this](QCamera::Error, const QString &error) {
            qWarning().noquote() << "Camera:" << error;
            m_frameAge.invalidate();
            m_status = "Couldn't use that camera. Choose another camera or "
                       "turn Camera off.";
            emit changed();
          });
  m_session->setCamera(m_camera.get());
  m_status = "Starting camera preview…";
  m_capturing = true;
  m_camera->start();
  emit changed();
}
void Webcam::startTrack(const QString &path, std::function<qint64()> clock) {
  if (!m_enabled || !ready())
    return;
  m_trackPath = path;
  m_clock = std::move(clock);
  m_track.begin(path);
  m_frameTick.start();
}
void Webcam::finishTrack() {
  m_frameTick.stop();
  if (m_track.busy()) {
    const qint64 time = m_clock ? m_clock() : -1;
    if (time >= 0 && ready())
      m_track.append(m_latest, time);
    m_track.end();
  } else
    suspend();
}
