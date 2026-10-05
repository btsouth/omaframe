#include "video.hpp"
#include "studio.hpp"
#include <QDataStream>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>
#include <QtConcurrent>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <csignal>
#include <sys/prctl.h>
#include <sys/resource.h>

static bool decodesVideoFrame(const QString &path, const QStringList &seek = {}) {
  QProcess decode;
  QStringList args{"-hide_banner", "-loglevel", "error", "-xerror"};
  args << seek << "-i" << path << "-map" << "0:v:0" << "-frames:v" << "1"
       << "-f" << "framecrc" << "pipe:1";
  decode.start("ffmpeg", args);
  if (!decode.waitForFinished(10000)) {
    decode.kill();
    decode.waitForFinished();
    return false;
  }
  bool frame = false;
  for (const auto &line : decode.readAllStandardOutput().split('\n'))
    frame |= !line.trimmed().isEmpty() && !line.startsWith('#');
  return decode.exitStatus() == QProcess::NormalExit &&
         decode.exitCode() == 0 && frame;
}

static QString seconds(double value) { return QString::number(value, 'f', 3); }

bool hidesVideo(const Frame::Edit &edit) {
  return edit.type == "blur" || edit.type == "redact";
}
bool pointsInVideo(const Frame::Edit &edit) {
  return !hidesVideo(edit) && edit.type != "crop";
}

QImage renderVideoMark(const QVector<Frame::Edit> &edits, int index, QSize size,
                       QRect *area) {
  if (index < 0 || index >= edits.size() || size.isEmpty() ||
      !pointsInVideo(edits[index]))
    return {};
  Frame::Edit edit = edits[index];
  if (edit.type == "step") {
    edit.number = 0;
    for (int i = 0; i <= index; ++i)
      edit.number += edits[i].type == "step";
  }
  QImage clear(size, QImage::Format_ARGB32_Premultiplied);
  clear.fill(Qt::transparent);
  const QImage drawn = Frame::applyEdits(clear, {edit}, false);
  // Look for drawn pixels near the mark only: arrow heads, outlines and
  // label boxes reach a little past its points, never far.
  const QRectF bounds = Frame::annotationBounds(edit, clear);
  const int margin = std::max(size.width(), size.height()) / 20 + 16;
  const QRect search =
      QRect(QPoint(int(bounds.left() * size.width()), int(bounds.top() * size.height())),
            QPoint(int(bounds.right() * size.width()), int(bounds.bottom() * size.height())))
          .adjusted(-margin, -margin, margin, margin)
          .intersected(drawn.rect());
  int left = search.right() + 1, right = -1, top = search.bottom() + 1, bottom = -1;
  for (int y = search.top(); y <= search.bottom(); ++y) {
    const QRgb *row = reinterpret_cast<const QRgb *>(drawn.constScanLine(y));
    for (int x = search.left(); x <= search.right(); ++x)
      if (qAlpha(row[x])) {
        left = std::min(left, x);
        right = std::max(right, x);
        top = std::min(top, y);
        bottom = std::max(bottom, y);
      }
  }
  if (right < 0)
    return {};
  *area = QRect(QPoint(left, top), QPoint(right, bottom));
  return drawn.copy(*area);
}

// The part of a filter that turns it on while a mark shows, with its times
// moved back by `offset`. False when the mark ends before the input starts.
static bool markTiming(double start, double end, double offset, QString *enable) {
  const bool untilEnd = end < 0;
  start = std::max(0., start - offset);
  end -= offset;
  if (!untilEnd && end <= start)
    return false;
  *enable = untilEnd ? QString(":enable='gte(t,%1)'").arg(seconds(start))
                     : QString(":enable='between(t,%1,%2)'").arg(seconds(start), seconds(end));
  return true;
}

QStringList videoMarkFilters(const QVector<Frame::Edit> &edits, QSize size,
                             double offset, const QString &input,
                             const QString &output,
                             const QVector<VideoOverlay> &overlays) {
  if (size.isEmpty())
    return {};
  QStringList chains;
  QString current = input;
  int index = 0;
  for (const auto &edit : edits) {
    QString enable;
    if (!hidesVideo(edit) || !markTiming(edit.start, edit.end, offset, &enable))
      continue;
    // Even edges line up with the half-size colour planes of yuv420p, so the
    // covered area never leaves a fringe of the original colour.
    const QRectF area =
        QRectF(QPointF(edit.from.x() * size.width(), edit.from.y() * size.height()),
               QPointF(edit.to.x() * size.width(), edit.to.y() * size.height()))
            .normalized();
    const int left = std::clamp(int(std::floor(area.left() / 2)) * 2, 0, size.width());
    const int top = std::clamp(int(std::floor(area.top() / 2)) * 2, 0, size.height());
    const int right = std::clamp(int(std::ceil(area.right() / 2)) * 2, 0, size.width());
    const int bottom = std::clamp(int(std::ceil(area.bottom() / 2)) * 2, 0, size.height());
    const int width = right - left, height = bottom - top;
    if (width < 2 || height < 2)
      continue;
    const QString next = QString("[mark%1]").arg(index);
    const QString rect = QString("%1:%2:%3:%4").arg(width).arg(height).arg(left).arg(top);
    if (edit.type == "redact") {
      // The same solid colour as a redacted screenshot.
      chains << QString("%1drawbox=x=%2:y=%3:w=%4:h=%5:color=0x151a20@1:t=fill%6%7")
                    .arg(current).arg(left).arg(top).arg(width).arg(height)
                    .arg(enable, next);
    } else {
      // Three times the screenshot's radius: at that strength text in a
      // recording is gone, not just soft, and the saved video is never easier
      // to read than the preview in review.
      const int sigma = 3 * Frame::blurRadius(edit, size);
      chains << QString("%1split[mark%2base][mark%2area]").arg(current).arg(index)
             << QString("[mark%1area]crop=%2,gblur=sigma=%3[mark%1soft]")
                    .arg(index).arg(rect).arg(sigma)
             << QString("[mark%1base][mark%1soft]overlay=%2:%3%4%5")
                    .arg(index).arg(left).arg(top).arg(enable, next);
    }
    current = next;
    ++index;
  }
  for (const auto &overlay : overlays) {
    QString enable;
    if (!markTiming(overlay.start, overlay.end, offset, &enable))
      continue;
    const QString next = QString("[mark%1]").arg(index);
    chains << QString("%1[%2:v]overlay=%3:%4%5%6")
                  .arg(current).arg(overlay.input).arg(overlay.area.x())
                  .arg(overlay.area.y()).arg(enable, next);
    current = next;
    ++index;
  }
  if (chains.isEmpty())
    return {};
  chains.last().chop(current.size());
  chains.last() += output;
  return chains;
}

static void removeAbandonedExports(const QString &directory) {
  static const QRegularExpression pattern(
      R"(^\..+-edited(?:-(?:[2-9]|[1-9][0-9]+))?-[0-9a-f]{6}\.part\.(mp4|gif)$)");
  const auto cutoff = QDateTime::currentDateTimeUtc().addSecs(-3600);
  for (const auto &file : QDir(directory).entryInfoList(
           QDir::Files | QDir::Hidden | QDir::NoSymLinks)) {
    if (pattern.match(file.fileName()).hasMatch() &&
        file.lastModified() < cutoff)
      QFile::remove(file.absoluteFilePath());
  }
}

Video::Video(QObject *parent) : QObject(parent) {
  m_draftTimer.setSingleShot(true);
  m_draftTimer.setInterval(250);
  connect(&m_draftTimer, &QTimer::timeout, this, &Video::saveDraftNow);
  refreshDrafts();
  m_marks.setLockCheck([this] { return m_busy; });
  connect(&m_marks, &MarkDocument::message, this, [this](const QString &text) {
    m_status = text;
    emit changed();
  });
  connect(&m_marks, &MarkDocument::edited, this, [this](bool modified) {
    renderOverlays();
    emit changed();
    if (modified && !m_source.isEmpty()) {
      m_draftDeleted = false;
      m_draftTimer.start();
    }
  });
  m_directory =
      QSettings()
          .value("videoDirectory", QStandardPaths::writableLocation(
                                       QStandardPaths::MoviesLocation) +
                                       "/Omaframe")
          .toString();
  removeAbandonedExports(m_directory);
  // x264 uses every core; run it below normal priority so the desktop stays
  // responsive during an export.
  m_encoder.setChildProcessModifier([] {
    ::prctl(PR_SET_PDEATHSIG, SIGKILL);
    setpriority(PRIO_PROCESS, 0, 10);
  });
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
              emit exportFailed();
            }
          });
  connect(&m_encoder, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
          this, [this](int code, QProcess::ExitStatus status) {
            if (m_cancelled) {
              m_busy = false;
              QFile::remove(m_temporary);
              m_status = "Export cancelled. Your recording is unchanged.";
            } else if (code != 0 || status != QProcess::NormalExit) {
              m_busy = false;
              QFile::remove(m_temporary);
              qWarning().noquote() << "Video export failed:" << m_error.simplified();
              m_status = "Couldn't save the video. Check the save folder and "
                         "free space, then try again.";
            } else {
              m_status = m_gifExport ? "Checking the exported GIF…"
                                     : "Checking the exported video…";
              emit changed();
              auto *watcher = new QFutureWatcher<bool>(this);
              connect(watcher, &QFutureWatcher<bool>::finished, this,
                      [this, watcher] {
                        const bool valid = watcher->result();
                        bool saved = false;
                        watcher->deleteLater();
                        m_busy = false;
                        if (m_cancelled) {
                          QFile::remove(m_temporary);
                          m_status = "Export cancelled. Your recording is unchanged.";
                        } else if (!valid) {
                          QFile::remove(m_temporary);
                          m_status = "The exported video could not be played. "
                                     "Your original recording is unchanged.";
                        } else if (!QFile::rename(m_temporary, m_final)) {
                          QFile::remove(m_temporary);
                          m_status = "Could not finish saving the clip. "
                                     "Check the save folder.";
                        } else {
                          saved = true;
                          m_progress = 1;
                          const QString summary =
                              QString("%1 s · %2")
                                  .arg(m_exportDuration, 0, 'f', 1)
                                  .arg(QLocale().formattedDataSize(
                                      QFileInfo(m_final).size()));
                          if (m_gifExport) {
                            m_gifSaved = m_final;
                            m_gifSummary = summary;
                          } else {
                            m_saved = m_final;
                            m_savedSummary = summary;
                          }
                          m_status = "Saved " + QFileInfo(m_final).fileName() +
                                     " · " + summary +
                                     ". The original is unchanged.";
                        }
                        emit changed();
                        if (saved && m_gifExport)
                          emit gifExported(QUrl::fromLocalFile(m_gifSaved));
                        else if (saved)
                          emit exported(QUrl::fromLocalFile(m_saved));
                        else
                          emit exportFailed();
                      });
              watcher->setFuture(QtConcurrent::run(
                  [path = m_temporary, expected = m_exportDuration,
                   gif = m_gifExport,
                   expectedAudio = m_muteExport ? 0 : m_audioTracks] {
                    if (QFileInfo(path).size() < (gif ? 16 : 1024))
                      return false;
                    QProcess probe;
                    probe.start("ffprobe", {"-v", "error", "-show_entries",
                                            "format=duration:stream=codec_type,width,height",
                                            "-of", "json", path});
                    if (!probe.waitForFinished(10000)) {
                      probe.kill();
                      probe.waitForFinished();
                      return false;
                    }
                    const auto doc = QJsonDocument::fromJson(
                                         probe.readAllStandardOutput())
                                         .object();
                    const double duration = doc.value("format")
                                                .toObject()
                                                .value("duration")
                                                .toString()
                                                .toDouble();
                    bool hasVideo = false;
                    int audioTracks = 0;
                    for (const auto &item : doc.value("streams").toArray()) {
                      const auto stream = item.toObject();
                      hasVideo |= stream.value("codec_type").toString() ==
                                      "video" &&
                                  stream.value("width").toInt() > 0 &&
                                  stream.value("height").toInt() > 0;
                      audioTracks += stream.value("codec_type").toString() == "audio";
                    }
                    if (probe.exitCode() != 0 || !hasVideo ||
                        audioTracks != expectedAudio ||
                        !std::isfinite(duration) ||
                        std::abs(duration - expected) >
                            std::max(0.35, expected * 0.02))
                      return false;
                    const double tail = std::min(1.0, duration);
                    return decodesVideoFrame(path) &&
                           decodesVideoFrame(
                               path, {"-sseof", QString::number(-tail, 'f', 3)});
                  }));
              return;
            }
            emit changed();
            emit exportFailed();
          });
}
Video::~Video() {
  saveDraftNow();
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
  saveDraftNow();
  m_busy = true;
  m_exporting = false;
  m_status = "Reading recording…";
  emit opening();
  emit changed();
  struct Result {
    double duration = 0;
    QString dimensions, error;
    QSize frameSize;
    int audioTracks = 0;
    bool copyCompatible = false;
  };
  auto *watcher = new QFutureWatcher<Result>(this);
  connect(watcher, &QFutureWatcher<Result>::finished, this,
          [this, watcher, url] {
            const auto r = watcher->result();
            watcher->deleteLater();
            m_busy = false;
            if (!r.error.isEmpty()) {
              m_pendingDraft.clear();
              m_status = r.error;
              emit changed();
              return;
            }
            m_source = url;
            const QFileInfo sourceInfo(url.toLocalFile());
            m_sourceSize = sourceInfo.size();
            m_sourceModified = sourceInfo.lastModified().toMSecsSinceEpoch();
            m_name = QFileInfo(url.toLocalFile()).fileName();
            m_duration = r.duration;
            m_dimensions = r.dimensions;
            m_frameSize = r.frameSize;
            // The marks only need the frame's size, for placing labels.
            QImage frame(m_frameSize, QImage::Format_ARGB32_Premultiplied);
            frame.fill(Qt::transparent);
            m_marks.reset(frame);
            m_marks.setDuration(m_duration);
            m_marks.setPlayhead(0);
            renderOverlays();
            m_audioTracks = r.audioTracks;
            m_copyCompatible = r.copyCompatible;
            m_saved.clear();
            m_savedSummary.clear();
            m_gifSaved.clear();
            m_gifSummary.clear();
            m_progress = 0;
            m_status = "Edits are saved as a new file. " + m_name +
                       " stays as it is.";
            m_draftId.clear();
            m_draftDeleted = false;
            m_draftSignature.clear();
            m_savedSignature.clear();
            m_editState = {{"clipStart", 0.},
                           {"clipEnd", m_duration},
                           {"muted", false},
                           {"cuts", QVariantList{}}};
            readCameraTrack();
            restorePendingDraft();
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
    bool compatibleAudio = true;
    bool h264 = false;
    for (const auto &value : doc.value("streams").toArray()) {
      const auto stream = value.toObject();
      const QString type = stream.value("codec_type").toString();
      if (type == "audio") {
        ++r.audioTracks;
        compatibleAudio &= stream.value("codec_name").toString() == "aac";
      }
      if (type == "video" && !hasVideo) {
        hasVideo = true;
        h264 = stream.value("codec_name").toString() == "h264";
        r.frameSize = QSize(stream.value("width").toInt(),
                            stream.value("height").toInt());
        // FFmpeg and the player turn frames upright, so marks are placed on
        // the upright picture. A quarter turn swaps its width and height.
        int rotation = stream.value("tags").toObject().value("rotate").toString().toInt();
        for (const auto &side : stream.value("side_data_list").toArray())
          if (side.toObject().contains("rotation"))
            rotation = side.toObject().value("rotation").toInt();
        if (qAbs(rotation) % 180 == 90)
          r.frameSize.transpose();
        r.dimensions = QString("%1 × %2")
                           .arg(r.frameSize.width())
                           .arg(r.frameSize.height());
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
    r.copyCompatible = h264 && compatibleAudio &&
                       QStringList{"mp4", "mov", "m4v"}.contains(
                           QFileInfo(url.toLocalFile()).suffix().toLower());
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
    // For short clips, decode once instead of starting and seeking FFmpeg
    // sixteen times. Long clips keep bounded seeks rather than a full decode.
    if (duration <= 90) {
      const QString fps = QString::number(ThumbnailCount / duration, 'f', 8);
      const QString first = QString::number(duration / (2 * ThumbnailCount), 'f', 6);
      QProcess batch;
      batch.start("ffmpeg",
                  {"-hide_banner", "-loglevel", "error", "-nostdin", "-y",
                   "-i", file, "-an", "-vf",
                   "fps=fps=" + fps + ":start_time=" + first + ",scale=-2:120",
                   "-frames:v", QString::number(ThumbnailCount), "-q:v", "5",
                   "-start_number", "0", dir + "/%02d.jpg"});
      const bool complete = batch.waitForFinished(15000) &&
                            batch.exitStatus() == QProcess::NormalExit &&
                            batch.exitCode() == 0;
      if (batch.state() != QProcess::NotRunning) {
        batch.kill();
        batch.waitForFinished();
      }
      if (complete) {
        for (int i = 0; i < ThumbnailCount; ++i) {
          const QString path =
              QString("%1/%2.jpg").arg(dir).arg(i, 2, 10, QChar('0'));
          if (!QFileInfo::exists(path))
            break;
          result << QUrl::fromLocalFile(path).toString();
        }
        if (result.size() == ThumbnailCount)
          return result;
        result.clear();
      }
    }
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
// What a mark looks like on its own, leaving out when it shows.
static QByteArray markLook(const QVector<Frame::Edit> &edits, int index,
                           QSize size) {
  const Frame::Edit &edit = edits[index];
  int number = 0;
  if (edit.type == "step")
    for (int i = 0; i <= index; ++i)
      number += edits[i].type == "step";
  QByteArray key;
  QDataStream out(&key, QIODevice::WriteOnly);
  out << size << edit.type << edit.from << edit.to << edit.text << edit.color
      << edit.size << edit.textStyle << edit.textAlign << edit.background
      << edit.backgroundOpacity << edit.points << number << edit.textBox
      << edit.outline << edit.arrowHead << edit.filled << edit.opacity << edit.numberColor;
  return key;
}
void Video::renderOverlays() {
  QVariantList list;
  QHash<QByteArray, DrawnMark> drawn;
  const auto &edits = m_marks.edits();
  for (int i = 0; i < edits.size(); ++i) {
    if (i == m_marks.hiddenIndex() || !pointsInVideo(edits[i]))
      continue;
    const QByteArray look = markLook(edits, i, m_frameSize);
    DrawnMark mark = drawn.value(look, m_drawnMarks.value(look));
    if (mark.image.isNull())
      mark.image = renderVideoMark(edits, i, m_frameSize, &mark.area);
    if (mark.image.isNull())
      continue;
    drawn.insert(look, mark);
    const QString name = QString("mark%1").arg(i);
    if (m_store)
      m_store->put(name, mark.image);
    const double width = m_frameSize.width(), height = m_frameSize.height();
    list.append(QVariantMap{
        {"index", i},
        {"start", edits[i].start},
        {"end", edits[i].end < 0 ? m_duration : edits[i].end},
        {"x", mark.area.x() / width},
        {"y", mark.area.y() / height},
        {"w", mark.area.width() / width},
        {"h", mark.area.height() / height},
        // The same picture keeps the same address, so QML only reloads
        // the marks that changed.
        {"source", QString("image://videomarks/%1?%2")
                       .arg(name)
                       .arg(qHash(look), 0, 16)}});
  }
  m_drawnMarks = drawn;
  m_overlays = list;
  emit overlaysChanged();
}
bool Video::writeOverlays(QStringList &inputs, QVector<VideoOverlay> &overlays) {
  m_overlayDir.reset();
  const auto &edits = m_marks.edits();
  for (int i = 0; i < edits.size(); ++i) {
    QRect area;
    const QImage image = renderVideoMark(edits, i, m_frameSize, &area);
    if (image.isNull())
      continue;
    if (!m_overlayDir)
      m_overlayDir = std::make_unique<QTemporaryDir>();
    const QString path = m_overlayDir->filePath(QString("mark%1.png").arg(i));
    if (!m_overlayDir->isValid() || !image.save(path, "PNG"))
      return false;
    inputs << "-i" << path;
    overlays.append({int(overlays.size()) + 1, area, edits[i].start, edits[i].end});
  }
  return true;
}
void Video::exportClip(double start, double end, bool mute) {
  exportEdited(start, end, mute, {});
}
void Video::exportEdited(double start, double end, bool mute,
                         const QVariantList &removedRanges) {
  exportEditedAs(start, end, mute, removedRanges, false);
}
void Video::exportGif(double start, double end,
                      const QVariantList &removedRanges) {
  exportEditedAs(start, end, true, removedRanges, true);
}
void Video::exportEditedAs(double start, double end, bool mute,
                           const QVariantList &removedRanges, bool gif) {
  if (m_busy || m_source.isEmpty())
    return;
  if (!std::isfinite(start) || !std::isfinite(end) || start < 0 ||
      end > m_duration + 0.05 || end - start < 0.1) {
    m_status = "Choose a clip at least a tenth of a second long.";
    emit changed();
    emit exportFailed();
    return;
  }
  QVector<QPair<double, double>> cuts;
  for (const auto &value : removedRanges) {
    const auto range = value.toMap();
    bool startOk = false, endOk = false;
    const double first = range.value("start").toDouble(&startOk);
    const double last = range.value("end").toDouble(&endOk);
    if (!startOk || !endOk || !std::isfinite(first) || !std::isfinite(last) ||
        first < 0 || last > m_duration + 0.05 || last - first + 0.000001 < 0.1) {
      m_status = "A removed section has invalid times.";
      emit changed();
      emit exportFailed();
      return;
    }
    if (last > start && first < end)
      cuts.append({std::max(first, start), std::min(last, end)});
  }
  std::sort(cuts.begin(), cuts.end(), [](const auto &a, const auto &b) {
    return a.first < b.first;
  });
  QVector<QPair<double, double>> kept;
  double cursor = start;
  for (const auto &cut : cuts) {
    if (cut.first > cursor + 0.001)
      kept.append({cursor, cut.first});
    cursor = std::max(cursor, cut.second);
  }
  if (end > cursor + 0.001)
    kept.append({cursor, end});
  double outputDuration = 0;
  for (const auto &range : kept)
    outputDuration += range.second - range.first;
  if (outputDuration + 0.000001 < 0.1) {
    m_status = "Keep at least a tenth of a second after removing sections.";
    emit changed();
    emit exportFailed();
    return;
  }
  if (gif && outputDuration > 30.000001) {
    m_status = "Keep GIFs to 30 seconds or less. Trim the ends or remove a "
               "part of the clip.";
    emit changed();
    emit exportFailed();
    return;
  }
  if (kept.size() == 1) {
    start = kept.first().first;
    end = kept.first().second;
  }
  if (!QDir().mkpath(m_directory)) {
    m_status = "Could not create the save folder.";
    emit changed();
    emit exportFailed();
    return;
  }
  // Arrows, boxes, labels and steps go in as pictures, one input each after
  // the recording.
  QStringList overlayInputs;
  QVector<VideoOverlay> overlays;
  if (!writeOverlays(overlayInputs, overlays)) {
    m_status = "Could not prepare the marks for the video. Check the free space "
               "in the temporary folder.";
    emit changed();
    emit exportFailed();
    return;
  }
  // Name the clip after its recording, so the two sit together.
  const QString base = QFileInfo(m_source.toLocalFile()).completeBaseName();
  const QString extension = gif ? ".gif" : ".mp4";
  m_final = m_directory + "/" + base + "-edited" + extension;
  for (int n = 2; QFileInfo::exists(m_final); ++n)
    m_final =
        m_directory + QString("/%1-edited-%2").arg(base).arg(n) + extension;
  m_temporary = m_directory + "/." + QFileInfo(m_final).completeBaseName() +
                "-" + QUuid::createUuid().toString(QUuid::Id128).left(6) +
                ".part" + extension;
  m_gifExport = gif;
  m_exportDuration = outputDuration;
  m_muteExport = mute;
  m_cancelled = false;
  m_progress = 0;
  m_error.clear();
  m_progressBuffer.clear();
  m_busy = true;
  m_exporting = true;
  if (!gif) {
    m_saved.clear();
    m_savedSummary.clear();
  }
  m_status = gif ? "Exporting a looping GIF…" : "Exporting a clean MP4…";
  // Marks are drawn on the source before any trim or cut, so their times are
  // always times in the recording.
  const QVector<Frame::Edit> marks = m_marks.edits();
  const bool marked =
      !videoMarkFilters(marks, m_frameSize, 0, "[0:v:0]", "[marked]", overlays)
           .isEmpty();
  // A clip beginning at the first frame can be shortened at the end without
  // re-encoding. Start trims, middle cuts and marks need new frames.
  const bool cameraVisible =
      !m_cameraSource.isEmpty() && m_cameraLayout.value("visible").toBool();
  const bool composed = m_marks.hasCrop() || cameraVisible;
  const bool streamCopy = !gif && !marked && !composed && kept.size() == 1 &&
                          m_copyCompatible && start <= 0.001;
  if (streamCopy)
    m_status = "Saving the original quality without re-encoding…";
  emit changed();
  if (streamCopy) {
    QStringList args{"-hide_banner", "-loglevel", "error", "-nostdin", "-n",
                     "-i", m_source.toLocalFile()};
    if (end < m_duration - 0.001)
      args << "-t" << QString::number(m_exportDuration, 'f', 3);
    args << "-map" << "0:v:0";
    if (!mute)
      args << "-map" << "0:a?";
    args << "-c" << "copy" << "-map_metadata" << "-1"
         << "-movflags" << "+faststart" << "-progress" << "pipe:1"
         << m_temporary;
    m_encoder.start("ffmpeg", args);
    return;
  }
  if (gif || kept.size() > 1 || composed) {
    QStringList filters = videoMarkFilters(marks, m_frameSize, 0, "[0:v:0]",
                                           "[marked]", overlays);
    QString base = marked ? "[marked]" : "[0:v:0]";
    if (m_marks.hasCrop()) {
      const QRect crop = cropPixels();
      filters << QString("%1crop=%2:%3:%4:%5[cropped]")
                     .arg(base)
                     .arg(crop.width())
                     .arg(crop.height())
                     .arg(crop.x())
                     .arg(crop.y());
      base = "[cropped]";
    }
    if (cameraVisible) {
      const QRectF bounds = cameraBounds();
      const QSize size = outputSize();
      const int w = qRound(bounds.width() * size.width());
      const int h = qRound(bounds.height() * size.height());
      filters << QString("[%1:v]scale=%2:%3,format=rgba[webcam]")
                     .arg(1 + overlays.size())
                     .arg(w)
                     .arg(h);
      filters << QString("%1[webcam]overlay=%2:%3:eof_action=pass:repeatlast=0["
                         "composed]")
                     .arg(base)
                     .arg(qRound(bounds.x() * size.width()))
                     .arg(qRound(bounds.y() * size.height()));
      base = "[composed]";
    }
    QString videoInputs;
    if (kept.size() > 1) {
      QString copies;
      for (int i = 0; i < kept.size(); ++i)
        copies += QString("[part%1]").arg(i);
      filters << QString("%1split=%2%3").arg(base).arg(kept.size()).arg(copies);
    }
    for (int i = 0; i < kept.size(); ++i) {
      const auto &range = kept[i];
      const QString times = QString("start=%1:end=%2")
                                .arg(range.first, 0, 'f', 3)
                                .arg(range.second, 0, 'f', 3);
      filters << QString(
                     "%1trim=%2,setpts=PTS-STARTPTS,"
                     "scale=trunc(iw/2)*2:trunc(ih/2)*2,format=yuv420p[v%3]")
                     .arg(kept.size() > 1 ? QString("[part%1]").arg(i) : base)
                     .arg(times)
                     .arg(i);
      videoInputs += QString("[v%1]").arg(i);
      if (!mute) {
        for (int track = 0; track < m_audioTracks; ++track)
          filters << QString("[0:a:%1]atrim=%2,asetpts=PTS-STARTPTS[a%1_%3]")
                         .arg(track).arg(times).arg(i);
      }
    }
    filters << QString("%1concat=n=%2:v=1:a=0[v]")
                   .arg(videoInputs).arg(kept.size());
    if (!mute) {
      for (int track = 0; track < m_audioTracks; ++track) {
        QString inputs;
        for (int i = 0; i < kept.size(); ++i)
          inputs += QString("[a%1_%2]").arg(track).arg(i);
        filters << QString("%1concat=n=%2:v=0:a=1[a%3]")
                       .arg(inputs).arg(kept.size()).arg(track);
      }
    }
    if (gif) {
      // A fresh palette per frame keeps encoding streaming, rather than
      // retaining the whole clip while a global palette is generated.
      filters << "[v]fps=15,scale=w='min(720,iw)':h='min(720,ih)':"
                 "force_original_aspect_ratio=decrease:flags=lanczos,"
                 "split[gifframes][gifcolors]"
              << "[gifcolors]palettegen=stats_mode=single[palette]"
              << "[gifframes][palette]paletteuse=new=1:dither=bayer:bayer_"
                 "scale=3[gif]";
    }
    QStringList args{"-hide_banner", "-loglevel", "error", "-nostdin", "-n",
                     "-i", m_source.toLocalFile()};
    if (marked)
      args << overlayInputs;
    if (cameraVisible)
      args << "-i" << m_cameraSource.toLocalFile();
    args << "-filter_complex" << filters.join(';') << "-map"
         << (gif ? "[gif]" : "[v]");
    if (mute)
      args << "-an";
    else {
      for (int track = 0; track < m_audioTracks; ++track)
        args << "-map" << QString("[a%1]").arg(track);
      if (m_audioTracks)
        args << "-c:a" << "aac" << "-b:a" << "192k";
    }
    if (gif)
      args << "-c:v" << "gif" << "-loop" << "0";
    else
      args << "-c:v" << "libx264" << "-preset" << "veryfast" << "-crf" << "16"
           << "-pix_fmt" << "yuv420p" << "-movflags" << "+faststart";
    args << "-map_metadata" << "-1" << "-progress" << "pipe:1" << m_temporary;
    m_encoder.start("ffmpeg", args);
    return;
  }
  QStringList args{"-hide_banner",
                   "-loglevel",
                   "error",
                   "-nostdin",
                   "-n",
                   "-ss",
                   QString::number(start, 'f', 3),
                   "-i",
                   m_source.toLocalFile()};
  // Seeking the input makes its first frame time zero, so the marks move
  // back by the same amount.
  QStringList markFilters = videoMarkFilters(marks, m_frameSize, start,
                                             "[0:v:0]", "[marked]", overlays);
  // The pictures come after the recording and its seek, and before -t, which
  // limits the output to the clip.
  if (!markFilters.isEmpty())
    args << overlayInputs;
  args << "-t" << QString::number(m_exportDuration, 'f', 3);
  if (markFilters.isEmpty()) {
    args << "-map" << "0:v:0";
  } else {
    markFilters << "[marked]scale=trunc(iw/2)*2:trunc(ih/2)*2[v]";
    args << "-filter_complex" << markFilters.join(';') << "-map" << "[v]";
  }
  if (mute)
    args << "-an";
  else
    args << "-map" << "0:a?" << "-c:a" << "aac" << "-b:a" << "192k";
  if (markFilters.isEmpty())
    args << "-vf" << "scale=trunc(iw/2)*2:trunc(ih/2)*2";
  args << "-c:v" << "libx264"
       << "-preset" << "veryfast" << "-crf" << "16" << "-pix_fmt" << "yuv420p"
       << "-map_metadata" << "-1" << "-movflags"
       << "+faststart" << "-progress" << "pipe:1" << m_temporary;
  m_encoder.start("ffmpeg", args);
}
void Video::cancel() {
  if (m_busy && m_exporting)
    m_cancelled = true;
  if (m_encoder.state() != QProcess::NotRunning) {
    m_encoder.kill();
  }
}
void Video::keepOriginal() {
  if (m_busy || !m_source.isLocalFile() ||
      !QFileInfo::exists(m_source.toLocalFile()))
    return;
  m_saved = m_source.toLocalFile();
  m_status = "Recording saved as " + QFileInfo(m_saved).fileName() + ".";
  emit changed();
  emit originalAccepted(m_source);
}
QString Video::savedName() const {
  return m_saved.isEmpty() ? QString() : QFileInfo(m_saved).fileName();
}
bool Video::copyFile(bool original) {
  const QString path =
      original || m_saved.isEmpty() ? m_source.toLocalFile() : m_saved;
  return copyPath(path);
}
bool Video::copyGif() { return copyPath(m_gifSaved); }
bool Video::copyPath(const QString &path) {
  if (path.isEmpty() || !QFileInfo::exists(path)) {
    m_status = "The file is missing, so it could not be copied.";
    emit changed();
    return false;
  }
  QProcess clipboard;
  clipboard.start("wl-copy", {"--type", "text/uri-list"});
  const bool started = clipboard.waitForStarted(3000);
  if (started) {
    clipboard.write(QUrl::fromLocalFile(path).toEncoded() + "\r\n");
    clipboard.closeWriteChannel();
  }
  const bool copied = started && clipboard.waitForFinished(5000) &&
                      clipboard.exitCode() == 0;
  if (clipboard.state() != QProcess::NotRunning) {
    clipboard.kill();
    clipboard.waitForFinished();
  }
  m_status = copied
                 ? QFileInfo(path).fileName() +
                       " is on the clipboard. Paste it into a chat or folder."
                 : "The file is saved, but it could not be copied. Check "
                   "that wl-clipboard is installed.";
  emit changed();
  return copied;
}
void Video::finish() {
  if (m_busy)
    return;
  emit originalAccepted(m_source);
}
void Video::revealSource() {
  if (m_source.isLocalFile())
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(QFileInfo(m_source.toLocalFile()).absolutePath()));
}
void Video::revealSaved() {
  if (!m_saved.isEmpty())
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(QFileInfo(m_saved).absolutePath()));
}
void Video::revealGif() {
  if (!m_gifSaved.isEmpty())
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(QFileInfo(m_gifSaved).absolutePath()));
}
void Video::setOutputDirectory(const QUrl &url) {
  if (m_busy || !url.isLocalFile())
    return;
  m_directory = url.toLocalFile();
  QSettings().setValue("videoDirectory", m_directory);
  emit changed();
}
