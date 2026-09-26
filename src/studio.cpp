#include "studio.hpp"
#include "capture-session.hpp"
#include "capture.hpp"
#include "displays.hpp"
#include <QBuffer>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineF>
#include <QMutexLocker>
#include <QPainter>
#include <QProcess>
#include <QSaveFile>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QWindow>
#include <QtConcurrent>

QImage ImageStore::requestImage(const QString &id, QSize *size,
                                const QSize &requested) {
  QMutexLocker lock(&mutex);
  QImage image = images.value(id.section('?', 0, 0));
  if (size)
    *size = image.size();
  return requested.isValid() ? image.scaled(requested, Qt::KeepAspectRatio,
                                            Qt::SmoothTransformation)
                             : image;
}
void ImageStore::put(const QString &name, const QImage &image) {
  QMutexLocker lock(&mutex);
  images.insert(name, image);
}

Studio::Studio(ImageStore *store, bool withDemo) : m_store(store) {
  QSettings settings;
  m_options.style = std::clamp(settings.value("style", 0).toInt(), 0, 8);
  m_options.padding =
      std::clamp(settings.value("padding", 0.09).toDouble(), 0.02, 0.22);
  m_options.aspect = std::clamp(settings.value("aspect", 0).toInt(), 0, 4);
  m_directory =
      settings
          .value("outputDirectory", QStandardPaths::writableLocation(
                                        QStandardPaths::PicturesLocation) +
                                        "/Omaframe")
          .toString();
  connect(qGuiApp, &QGuiApplication::screenAdded, this,
          [this](QScreen *) { emit changed(); });
  connect(qGuiApp, &QGuiApplication::screenRemoved, this,
          [this](QScreen *) { emit changed(); });
  if (withDemo)
    loadDemo();
}
QStringList Studio::monitors() const {
  QStringList result{"All displays"};
  for (const auto &names : Displays::describe(Displays::fromScreens()))
    result << names.label;
  return result;
}
QString Studio::dimensions() const {
  return QString("%1 × %2")
      .arg(m_workingSize.width())
      .arg(m_workingSize.height());
}
QString Studio::outputDimensions() const {
  const QSize s = Frame::outputSize(m_workingSize, m_options);
  return QString("%1 × %2").arg(s.width()).arg(s.height());
}
void Studio::persistOptions() {
  QSettings s;
  s.setValue("style", m_options.style);
  s.setValue("padding", m_options.padding);
  s.setValue("aspect", m_options.aspect);
}
void Studio::setStyle(int value) {
  if (m_busy || value == style() || value < 0 || value > 8)
    return;
  m_options.style = value;
  persistOptions();
  scheduleRender();
}
void Studio::setPadding(double value) {
  value = std::clamp(value, 0.02, 0.22);
  if (m_busy || qFuzzyCompare(value, padding()))
    return;
  m_options.padding = value;
  persistOptions();
  scheduleRender();
}
void Studio::setAspect(int value) {
  if (m_busy || value == aspect() || value < 0 || value > 4)
    return;
  m_options.aspect = value;
  persistOptions();
  scheduleRender();
}

struct PreviewResult {
  QImage source, preview;
  QVector<QImage> thumbnails;
  QSize workingSize;
};
void Studio::scheduleRender() {
  const int generation = ++m_generation;
  m_rendering = true;
  emit changed();
  auto *watcher = new QFutureWatcher<PreviewResult>(this);
  connect(watcher, &QFutureWatcher<PreviewResult>::finished, this,
          [this, watcher, generation] {
            const auto result = watcher->result();
            watcher->deleteLater();
            if (generation != m_generation)
              return;
            m_store->put("source", result.source);
            m_store->put("preview", result.preview);
            for (int i = 0; i < result.thumbnails.size(); ++i)
              m_store->put(QString("style%1").arg(i), result.thumbnails[i]);
            m_workingSize = result.workingSize;
            m_rendering = false;
            ++m_revision;
            emit changed();
            if (m_pendingFinish >= 0) {
              m_pendingFinish = -1;
              accept();
            }
          });
  // Each request owns immutable implicitly-shared input. Obsolete results never
  // reach the UI.
  watcher->setFuture(QtConcurrent::run(
      [source = m_original, edits = m_edits, options = m_options] {
        PreviewResult result;
        const QImage working = Frame::applyEdits(source, edits);
        result.workingSize = working.size();
        result.source = working.scaled(1800, 1800, Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation);
        result.preview = Frame::compose(working, options, 1600);
        for (int i = 0; i < 9; ++i) {
          auto opt = options;
          opt.style = i;
          result.thumbnails.append(Frame::compose(working, opt, 640));
        }
        return result;
      }));
}
void Studio::loadImage(QImage image, QString name, bool demo) {
  if (image.isNull())
    return;
  image.setDevicePixelRatio(1);
  m_original = std::move(image);
  m_workingSize = m_original.size();
  m_name = std::move(name);
  m_demo = demo;
  m_edits.clear();
  m_redo.clear();
  m_savedPath.clear();
  m_status = demo ? "Sample image. Try a finish, or capture your own."
                  : "Ready when you are.";
  scheduleRender();
  emit sourceChanged();
}
void Studio::loadDemo(int variant) {
  if (!m_busy)
    loadImage(Frame::demoImage(variant),
              variant == 1 ? "Terminal sample" : "Noon workspace", true);
}
void Studio::open(const QUrl &url) {
  if (m_busy || !url.isLocalFile())
    return;
  if (QStringList{"mp4", "webm", "mkv", "mov", "m4v", "avi"}.contains(
          QFileInfo(url.toLocalFile()).suffix().toLower())) {
    emit videoRequested(url);
    return;
  }
  m_busy = true;
  m_status = "Opening image…";
  emit changed();
  struct Result {
    QImage image;
    QString error;
  };
  auto *watcher = new QFutureWatcher<Result>(this);
  connect(
      watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, url] {
        auto r = watcher->result();
        watcher->deleteLater();
        m_busy = false;
        if (r.image.isNull()) {
          m_status = r.error;
          emit changed();
        } else
          loadImage(r.image, QFileInfo(url.toLocalFile()).fileName(), false);
      });
  watcher->setFuture(QtConcurrent::run([url] {
    QImageReader reader(url.toLocalFile());
    reader.setAutoTransform(true);
    const QSize s = reader.size();
    if (s.isValid() && (qint64(s.width()) * s.height() > 40000000))
      return Result{{},
                    "This image is too large. The first version supports up to "
                    "40 megapixels."};
    QImage image = reader.read();
    return Result{image, image.isNull()
                             ? "Could not open image: " + reader.errorString()
                             : QString()};
  }));
}
void Studio::edit(const QString &type, double x1, double y1, double x2,
                  double y2, const QString &text) {
  if (m_busy || m_rendering)
    return;
  if (!QStringList{"crop", "arrow", "highlight", "redact", "text", "step"}
           .contains(type))
    return;
  QPointF a(std::clamp(x1, 0., 1.), std::clamp(y1, 0., 1.)),
      b(std::clamp(x2, 0., 1.), std::clamp(y2, 0., 1.));
  if (type == "text" && text.trimmed().isEmpty())
    return;
  if (type != "step" && type != "text" && QLineF(a, b).length() < 0.006)
    return;
  if (m_edits.size() >= 100) {
    m_status = "This image has reached the 100-edit limit.";
    emit changed();
    return;
  }
  m_edits.append({type, a, b, text.left(240)});
  m_redo.clear();
  m_savedPath.clear();
  m_status = type == "redact"
                 ? "Redaction applied. Exported pixels are fully replaced."
                 : "Edit applied. Undo is always available.";
  scheduleRender();
}
void Studio::undo() {
  if (m_busy || m_rendering || m_edits.isEmpty())
    return;
  m_redo.append(m_edits.takeLast());
  m_savedPath.clear();
  scheduleRender();
}
void Studio::redo() {
  if (m_busy || m_rendering || m_redo.isEmpty())
    return;
  m_edits.append(m_redo.takeLast());
  m_savedPath.clear();
  scheduleRender();
}
void Studio::resetEdits() {
  if (m_busy || m_rendering || m_edits.isEmpty())
    return;
  while (!m_edits.isEmpty())
    m_redo.append(m_edits.takeLast());
  m_savedPath.clear();
  scheduleRender();
}
void Studio::setOutputDirectory(const QUrl &url) {
  if (!url.isLocalFile() || m_busy)
    return;
  m_directory = url.toLocalFile();
  QSettings().setValue("outputDirectory", m_directory);
  emit changed();
}
void Studio::revealSaved() {
  if (!m_savedPath.isEmpty())
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(QFileInfo(m_savedPath).absolutePath()));
}

static bool writePng(const QString &path, const QImage &image, QString &error,
                     bool privateFile = false) {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    error = file.errorString();
    return false;
  }
  if (privateFile &&
      !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)) {
    error = "Could not set private permissions on the original backup.";
    return false;
  }
  QImageWriter writer(&file, "png");
  writer.setCompression(60);
  if (!writer.write(image)) {
    error = writer.errorString();
    return false;
  }
  if (!file.commit()) {
    error = file.errorString();
    return false;
  }
  return true;
}
struct ExportResult {
  QString path, error;
  bool copied = false;
};
void Studio::accept() {
  if (m_busy || m_rendering || m_original.isNull())
    return;
  const QSize output = Frame::outputSize(m_workingSize, m_options);
  if (qint64(output.width()) * output.height() > 80000000) {
    if (m_quickMode)
      m_quickState = "failed";
    m_status = "This canvas would exceed 80 megapixels. Use a smaller border "
               "or a different aspect ratio.";
    emit changed();
    return;
  }
  m_busy = true;
  if (m_quickMode)
    m_quickState = "saving";
  m_status = "Saving full-resolution PNG and copying…";
  emit changed();
  const QString originalDirectory =
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
      "/originals";
  auto *watcher = new QFutureWatcher<ExportResult>(this);
  connect(watcher, &QFutureWatcher<ExportResult>::finished, this,
          [this, watcher] {
            auto r = watcher->result();
            watcher->deleteLater();
            m_busy = false;
            m_savedPath = r.path;
            m_status = r.error.isEmpty()
                           ? "Copied and saved. Ready to paste anywhere."
                           : r.error;
            if (m_quickMode)
              m_quickState = r.error.isEmpty() ? "done" : "failed";
            emit changed();
            // Keep failures visible. Never dismiss a capture that was not both
            // saved and copied, including a failed original backup.
            if (m_quickMode && r.error.isEmpty())
              emit dismissRequested();
          });
  watcher->setFuture(QtConcurrent::run([source = m_original, edits = m_edits,
                                        options = m_options,
                                        directory = m_directory,
                                        originalDirectory] {
    ExportResult r;
    if (!QDir().mkpath(directory) || !QDir().mkpath(originalDirectory)) {
      r.error = "Could not create the save folder. Choose another folder and "
                "try again.";
      return r;
    }
    const QString id =
        QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss-zzz") + "-" +
        QUuid::createUuid().toString(QUuid::Id128).left(6);
    const QString path = directory + "/Omaframe-" + id + ".png";
    QImage composed = Frame::compose(Frame::applyEdits(source, edits), options);
    // A fresh raster strips imported PNG text and other source metadata,
    // including in Raw mode.
    QImage flattened(composed.size(), QImage::Format_ARGB32_Premultiplied);
    flattened.fill(Qt::transparent);
    {
      QPainter painter(&flattened);
      painter.drawImage(0, 0, composed);
    }
    if (!writePng(path, flattened, r.error)) {
      r.error = "Could not save image: " + r.error;
      return r;
    }
    r.path = path;
    QString originalError;
    const bool originalSaved = writePng(originalDirectory + "/" + id + ".png",
                                        source, originalError, true);
    QProcess clipboard;
    clipboard.start("wl-copy", {"--type", "image/png"});
    if (clipboard.waitForStarted(3000)) {
      QFile input(path);
      if (input.open(QIODevice::ReadOnly)) {
        clipboard.write(input.readAll());
        clipboard.closeWriteChannel();
        r.copied = clipboard.waitForFinished(10000) &&
                   clipboard.exitStatus() == QProcess::NormalExit &&
                   clipboard.exitCode() == 0;
      }
      if (clipboard.state() != QProcess::NotRunning) {
        clipboard.kill();
        clipboard.waitForFinished();
      }
    }
    if (!r.copied)
      r.error = "Saved successfully, but clipboard copy failed. Check that "
                "wl-copy is installed.";
    if (!originalSaved)
      r.error +=
          (r.error.isEmpty() ? QString() : " ") +
          QString("The finished image is saved; the original backup failed: %1")
              .arg(originalError);
    return r;
  }));
}

void Studio::capture(bool region, int monitor) {
  if (m_busy)
    return;
  m_busy = true;
  m_quickMode = true;
  m_quickState = "capturing";
  m_pendingFinish = -1;
  m_status = "Capturing…";
  m_frozen.clear();
  // Only wait for dismissal animations when this process has a visible UI.
  // A fresh shortcut launch has nothing of its own to hide.
  const auto windows = QGuiApplication::allWindows();
  const bool wasVisible =
      std::any_of(windows.cbegin(), windows.cend(),
                  [](QWindow *w) { return w->isVisible(); });
  emit changed();
  emit hideStudio();
  QStringList requested;
  // A region starts on whichever display the user chooses with the pointer.
  // Snapshot all displays before placing any selection overlays.
  if (region && monitor == 0) {
    for (QScreen *screen : QGuiApplication::screens())
      requested << screen->name();
  } else if (const auto screens = QGuiApplication::screens();
             monitor > 0 && monitor <= screens.size())
    requested << screens[monitor - 1]->name();
  QTimer::singleShot(
      wasVisible ? 220 : 0, this, [this, region, requested]() mutable {
        auto *watcher = new QFutureWatcher<Capture::Screens>(this);
        connect(watcher, &QFutureWatcher<Capture::Screens>::finished, this,
                [this, watcher, region] {
                  auto result = watcher->result();
                  watcher->deleteLater();
                  if (!result.error.isEmpty()) {
                    m_busy = false;
                    m_quickState = "capture-error";
                    m_status = "Capture failed: " + result.error;
                    emit changed();
                    emit captureFailed();
                    return;
                  }
                  if (region) {
                    m_frozen = result.images;
                    for (auto it = m_frozen.cbegin(); it != m_frozen.cend();
                         ++it)
                      m_store->put("capture/" + it.key(), it.value());
                    m_quickState = "selecting";
                    ++m_revision;
                    emit changed();
                    emit selectionReady(m_frozen.keys());
                  } else {
                    m_busy = false;
                    m_captureMonitor = result.images.constBegin().key();
                    loadImage(result.images.constBegin().value(),
                              "Screen capture", false);
                    m_quickState = "choosing";
                    emit changed();
                    emit chooserRequested();
                  }
                });
        watcher->setFuture(QtConcurrent::run([requested]() mutable {
          if (requested.isEmpty()) {
            QProcess process;
            process.start("hyprctl", {"-j", "monitors"});
            if (!process.waitForFinished(2000)) {
              process.kill();
              process.waitForFinished();
              return Capture::Screens{{},
                                      "Could not identify the active display."};
            }
            const QJsonArray outputs =
                QJsonDocument::fromJson(process.readAllStandardOutput())
                    .array();
            for (const auto &value : outputs) {
              auto output = value.toObject();
              if (output.value("focused").toBool())
                requested << output.value("name").toString();
            }
            if (requested.isEmpty() && !outputs.isEmpty())
              requested << outputs.first().toObject().value("name").toString();
          }
          return Capture::freeze(requested, [](const QString &name,
                                               QImage &image, QString &error) {
            MonitorInfo info;
            info.name = name;
            return captureOutputSurface(info, image, error);
          });
        }));
      });
}
void Studio::finishSelection(const QString &monitor, double x1, double y1,
                             double x2, double y2) {
  if (m_quickState != "selecting")
    return;
  const QImage result = Capture::crop(m_frozen, monitor, {x1, y1}, {x2, y2});
  if (result.isNull())
    return;
  m_captureMonitor = monitor;
  for (const auto &name : m_frozen.keys())
    m_store->put("capture/" + name, {});
  m_frozen.clear();
  m_busy = false;
  emit selectionDone();
  if (m_recordingSelection) {
    m_quickState = "record-setup";
    emit changed();
    emit recordRegionSelected(monitor, QRectF(QPointF(x1, y1), QPointF(x2, y2))
                                           .normalized()
                                           .intersected(QRectF(0, 0, 1, 1)));
    return;
  }
  loadImage(result, "Region capture", false);
  m_quickState = "choosing";
  emit changed();
  emit chooserRequested();
}
void Studio::cancelSelection() {
  if (m_quickState != "selecting")
    return;
  for (const auto &name : m_frozen.keys())
    m_store->put("capture/" + name, {});
  m_frozen.clear();
  m_busy = false;
  m_quickState = "cancelled";
  m_status = "Capture cancelled.";
  emit selectionDone();
  emit changed();
  emit dismissRequested();
}
void Studio::chooseFinish(int value) {
  if (!m_quickMode || (m_quickState != "choosing" && m_quickState != "failed"))
    return;
  if (m_busy || m_pendingFinish >= 0 || value < 0 || value > 8)
    return;
  setStyle(value);
  if (m_rendering)
    m_pendingFinish = value;
  else
    accept();
}
void Studio::openEditor() {
  if (m_busy || m_pendingFinish >= 0)
    return;
  m_quickState = "editing";
  emit changed();
  emit editorRequested();
}
void Studio::showFinishes() {
  if (!m_quickMode || m_busy)
    return;
  m_quickState = "choosing";
  emit changed();
  emit chooserRequested();
}
void Studio::dismissQuick() {
  if (m_busy || m_pendingFinish >= 0)
    return;
  m_quickState = "cancelled";
  emit changed();
  emit dismissRequested();
}

void Studio::recordInstead(const QString &monitor) {
  if (m_quickState != "selecting")
    return;
  if (!monitor.isEmpty())
    m_captureMonitor = monitor;
  for (const auto &name : m_frozen.keys())
    m_store->put("capture/" + name, {});
  m_frozen.clear();
  m_busy = false;
  m_quickState = "record-setup";
  emit selectionDone();
  emit changed();
  emit recordRequested();
}
