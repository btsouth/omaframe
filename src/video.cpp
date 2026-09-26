#include "video.hpp"
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>
#include <QtConcurrent>
#include <cmath>

Video::Video(QObject *parent) : QObject(parent) {
  m_directory =
      QSettings()
          .value("videoDirectory", QStandardPaths::writableLocation(
                                       QStandardPaths::MoviesLocation) +
                                       "/Omaframe")
          .toString();
  connect(&m_encoder, &QProcess::readyReadStandardError, this, [this] {
    m_error = (m_error + QString::fromUtf8(m_encoder.readAllStandardError()))
                  .right(2400);
  });
  connect(&m_encoder, &QProcess::readyReadStandardOutput, this, [this] {
    m_progressBuffer += m_encoder.readAllStandardOutput();
    while (m_progressBuffer.contains('\n')) {
      int pos = m_progressBuffer.indexOf('\n');
      const QByteArray line = m_progressBuffer.left(pos);
      m_progressBuffer.remove(0, pos + 1);
      if (line.startsWith("out_time_us=")) {
        m_progress = std::clamp(
            line.mid(12).toDouble() / 1000000. / m_exportDuration, 0., 0.99);
        emit changed();
      }
    }
  });
  connect(&m_encoder, &QProcess::errorOccurred, this,
          [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
              m_busy = false;
              m_status =
                  "Could not start FFmpeg. Install ffmpeg and try again.";
              QFile::remove(m_temporary);
              emit changed();
            }
          });
  connect(&m_encoder, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
          this, [this](int code, QProcess::ExitStatus status) {
            m_busy = false;
            if (m_cancelled) {
              QFile::remove(m_temporary);
              m_status = "Export cancelled. Your recording is unchanged.";
            } else if (code != 0 || status != QProcess::NormalExit) {
              QFile::remove(m_temporary);
              m_status =
                  "Video export failed: " + m_error.simplified().right(300);
            } else if (!QFile::rename(m_temporary, m_final)) {
              QFile::remove(m_temporary);
              m_status =
                  "Could not finish saving the clip. Check the save folder.";
            } else {
              m_saved = m_final;
              m_progress = 1;
              m_status = "Clip saved. Your original recording is unchanged.";
            }
            emit changed();
          });
}
Video::~Video() {
  if (m_encoder.state() != QProcess::NotRunning) {
    m_encoder.kill();
    m_encoder.waitForFinished(3000);
  }
  if (!m_temporary.isEmpty())
    QFile::remove(m_temporary);
}
void Video::open(const QUrl &url) {
  if (m_busy || !url.isLocalFile())
    return;
  m_busy = true;
  m_exporting = false;
  m_status = "Reading recording…";
  emit opening();
  emit changed();
  struct Result {
    double duration = 0;
    QString dimensions, error;
    int audioTracks = 0;
  };
  auto *watcher = new QFutureWatcher<Result>(this);
  connect(watcher, &QFutureWatcher<Result>::finished, this,
          [this, watcher, url] {
            const auto r = watcher->result();
            watcher->deleteLater();
            m_busy = false;
            if (!r.error.isEmpty()) {
              m_status = r.error;
              emit changed();
              return;
            }
            m_source = url;
            m_name = QFileInfo(url.toLocalFile()).fileName();
            m_duration = r.duration;
            m_dimensions = r.dimensions;
            m_audioTracks = r.audioTracks;
            m_saved.clear();
            m_progress = 0;
            m_status = "Trim the beginning and end, then export your clip.";
            makeThumbnails();
            emit changed();
            emit loaded();
          });
  watcher->setFuture(QtConcurrent::run([url] {
    Result r;
    QProcess probe;
    probe.start("ffprobe", {"-v", "error", "-show_format", "-show_streams",
                            "-of", "json", url.toLocalFile()});
    if (!probe.waitForFinished(10000)) {
      probe.kill();
      probe.waitForFinished();
      r.error = "Could not read the recording. Check that ffmpeg is installed.";
      return r;
    }
    const auto doc =
        QJsonDocument::fromJson(probe.readAllStandardOutput()).object();
    bool hasVideo = false;
    for (const auto &value : doc.value("streams").toArray()) {
      const auto stream = value.toObject();
      const QString type = stream.value("codec_type").toString();
      if (type == "audio")
        ++r.audioTracks;
      if (type == "video" && !hasVideo) {
        hasVideo = true;
        r.dimensions = QString("%1 × %2")
                           .arg(stream.value("width").toInt())
                           .arg(stream.value("height").toInt());
        r.duration = stream.value("duration").toString().toDouble();
      }
    }
    const double formatDuration =
        doc.value("format").toObject().value("duration").toString().toDouble();
    if (formatDuration > 0)
      r.duration = formatDuration;
    if (probe.exitCode() != 0 || !hasVideo || !std::isfinite(r.duration) ||
        r.duration < 0.1)
      r.error =
          "This file does not contain a readable video with a known duration.";
    return r;
  }));
}
void Video::makeThumbnails() {
  const int generation = ++m_generation;
  m_thumbnails.clear();
  m_thumbnailDir = std::make_unique<QTemporaryDir>();
  if (!m_thumbnailDir->isValid())
    return;
  auto *watcher = new QFutureWatcher<QStringList>(this);
  connect(watcher, &QFutureWatcher<QStringList>::finished, this,
          [this, watcher, generation] {
            watcher->deleteLater();
            if (generation != m_generation)
              return;
            m_thumbnails = watcher->result();
            emit changed();
          });
  watcher->setFuture(QtConcurrent::run([dir = m_thumbnailDir->path(),
                                        file = m_source.toLocalFile(),
                                        duration = m_duration] {
    QStringList result;
    for (int i = 0; i < ThumbnailCount; ++i) {
      const QString path =
          QString("%1/%2.jpg").arg(dir).arg(i, 2, 10, QChar('0'));
      QProcess ffmpeg;
      ffmpeg.start(
          "ffmpeg",
          {"-hide_banner", "-loglevel", "error", "-nostdin", "-y", "-ss",
           QString::number(duration * (i + 0.5) / ThumbnailCount, 'f', 3),
           "-i", file, "-frames:v", "1", "-an", "-vf", "scale=-2:120", "-q:v",
           "5", path});
      if (!ffmpeg.waitForFinished(5000)) {
        ffmpeg.kill();
        ffmpeg.waitForFinished();
        return QStringList();
      }
      // A frame past the last keyframe can come back empty; repeat the last.
      if (ffmpeg.exitCode() == 0 && QFileInfo::exists(path))
        result << QUrl::fromLocalFile(path).toString();
      else if (!result.isEmpty())
        result << result.last();
      else
        return QStringList();
    }
    return result;
  }));
}
void Video::exportClip(double start, double end, bool mute) {
  if (m_busy || m_source.isEmpty())
    return;
  if (!std::isfinite(start) || !std::isfinite(end) || start < 0 ||
      end > m_duration + 0.05 || end - start < 0.1) {
    m_status = "Choose a clip at least a tenth of a second long.";
    emit changed();
    return;
  }
  if (!QDir().mkpath(m_directory)) {
    m_status = "Could not create the save folder.";
    emit changed();
    return;
  }
  QString id =
      QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss-zzz") + "-" +
      QUuid::createUuid().toString(QUuid::Id128).left(6);
  m_final = m_directory + "/Omaframe-" + id + ".mp4";
  m_temporary = m_directory + "/.Omaframe-" + id + ".part.mp4";
  m_exportDuration = end - start;
  m_cancelled = false;
  m_progress = 0;
  m_error.clear();
  m_progressBuffer.clear();
  m_busy = true;
  m_exporting = true;
  m_saved.clear();
  m_status = "Exporting a clean MP4…";
  emit changed();
  QStringList args{"-hide_banner",
                   "-loglevel",
                   "error",
                   "-nostdin",
                   "-n",
                   "-ss",
                   QString::number(start, 'f', 3),
                   "-i",
                   m_source.toLocalFile(),
                   "-t",
                   QString::number(m_exportDuration, 'f', 3),
                   "-map",
                   "0:v:0"};
  if (mute)
    args << "-an";
  else
    args << "-map" << "0:a?" << "-c:a" << "aac" << "-b:a" << "192k";
  args << "-vf" << "scale=trunc(iw/2)*2:trunc(ih/2)*2" << "-c:v" << "libx264"
       << "-preset" << "fast" << "-crf" << "18" << "-pix_fmt" << "yuv420p"
       << "-threads" << "2" << "-map_metadata" << "-1" << "-movflags"
       << "+faststart" << "-progress" << "pipe:1" << m_temporary;
  m_encoder.start("ffmpeg", args);
}
void Video::cancel() {
  if (m_encoder.state() != QProcess::NotRunning) {
    m_cancelled = true;
    m_encoder.kill();
  }
}
void Video::revealSaved() {
  if (!m_saved.isEmpty())
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(QFileInfo(m_saved).absolutePath()));
}
void Video::setOutputDirectory(const QUrl &url) {
  if (m_busy || !url.isLocalFile())
    return;
  m_directory = url.toLocalFile();
  QSettings().setValue("videoDirectory", m_directory);
  emit changed();
}
