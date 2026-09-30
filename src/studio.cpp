#include "studio.hpp"
#include "capture-session.hpp"
#include "capture.hpp"
#include "displays.hpp"
#include "edit-json.hpp"
#include "ocr.hpp"
#include "window-targets.hpp"
#include <QBuffer>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMutexLocker>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QWindow>
#include <QtConcurrent>
#include <cmath>
#include <optional>

static QString originalsDirectory() {
  return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
         "/originals";
}
static QString draftsDirectory() {
  return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
         "/drafts";
}
static constexpr qint64 maxDraftMetadataBytes = 32 * 1024 * 1024;
static bool validDraftId(const QString &id) {
  static const QRegularExpression format("^[0-9a-f]{32}$");
  return format.match(id).hasMatch();
}
static QString summarizeOriginals(int *count) {
  const auto files = QDir(originalsDirectory()).entryInfoList(
      {"*.png"}, QDir::Files | QDir::NoSymLinks);
  *count = files.size();
  if (files.isEmpty())
    return "No private originals saved.";
  qint64 bytes = 0;
  for (const auto &file : files)
    bytes += file.size();
  return QString("%1 private originals · %2 · kept until cleared")
      .arg(files.size())
      .arg(QLocale().formattedDataSize(bytes));
}

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
  m_marks.setLockCheck([this] { return m_busy; });
  connect(&m_marks, &MarkDocument::edited, this, [this](bool modified) {
    if (modified)
      invalidateSaved();
    scheduleRender();
  });
  connect(&m_marks, &MarkDocument::message, this, [this](const QString &text) {
    m_status = text;
    emit changed();
  });
  QSettings settings;
  m_options.style = std::clamp(settings.value("style", 0).toInt(), 0, 8);
  double savedPadding = settings.value("padding", 0.05).toDouble();
  if (settings.value("paddingVersion", 1).toInt() < 2) {
    // Style changes also saved the old default. Keep other saved values.
    if (qFuzzyCompare(savedPadding, 0.09)) {
      savedPadding = 0.05;
      settings.setValue("padding", savedPadding);
    }
    settings.setValue("paddingVersion", 2);
  }
  m_options.padding = std::clamp(savedPadding, 0.02, 0.22);
  m_options.aspect = std::clamp(settings.value("aspect", 0).toInt(), 0, 4);
  m_lastAreaMonitor = settings.value("lastArea/monitor").toString();
  m_lastArea = settings.value("lastArea/rect").toRectF();
  m_lastAreaPixels = settings.value("lastArea/pixels").toSize();
  m_directory =
      settings
          .value("outputDirectory", QStandardPaths::writableLocation(
                                        QStandardPaths::PicturesLocation) +
                                        "/Omaframe")
      .toString();
  m_originalsSummary = summarizeOriginals(&m_originalsCount);
  m_draftTimer.setSingleShot(true);
  m_draftTimer.setInterval(250);
  connect(&m_draftTimer, &QTimer::timeout, this, &Studio::saveDraftNow);
  refreshDrafts();
  connect(qGuiApp, &QGuiApplication::screenAdded, this,
          [this](QScreen *) { emit changed(); });
  connect(qGuiApp, &QGuiApplication::screenRemoved, this,
          [this](QScreen *) { emit changed(); });
  if (withDemo)
    loadDemo();
}
Studio::~Studio() { stopReading(); }
QStringList Studio::monitors() const {
  QStringList result{"All displays"};
  for (const auto &names : Displays::describe(Displays::fromScreens()))
    result << names.label;
  return result;
}
bool Studio::hasLastArea() const {
  if (m_lastAreaMonitor.isEmpty() || !m_lastAreaPixels.isValid() ||
      m_lastArea.width() <= 0 || m_lastArea.height() <= 0 ||
      !QRectF(0, 0, 1, 1).contains(m_lastArea))
    return false;
  const auto screens = QGuiApplication::screens();
  return std::any_of(screens.cbegin(), screens.cend(), [this](QScreen *screen) {
                       return screen->name() == m_lastAreaMonitor;
                     });
}
QString Studio::dimensions() const {
  return QString("%1 × %2")
      .arg(m_workingSize.width())
      .arg(m_workingSize.height());
}
QString Studio::outputDimensions() const {
  const QSize s = Frame::outputSize(m_workingSize, m_options, m_edgeRoom);
  return QString("%1 × %2").arg(s.width()).arg(s.height());
}
QString Studio::recoveryAction() const {
  if (m_savedPath.isEmpty() || (!m_copyPending && !m_backupPending))
    return {};
  if (m_copyPending && m_backupPending)
    return "Retry copy and backup";
  return m_copyPending ? "Retry copy" : "Retry backup";
}
void Studio::persistOptions() {
  QSettings s;
  s.setValue("style", m_options.style);
  s.setValue("padding", m_options.padding);
  s.setValue("paddingVersion", 2);
  s.setValue("aspect", m_options.aspect);
}
void Studio::invalidateSaved() {
  m_savedPath.clear();
  m_backupPath.clear();
  m_copyPending = m_backupPending = false;
  if (!m_demo && !m_original.isNull())
    m_draftDirty = true;
}
void Studio::setStyle(int value) {
  if (m_busy || value == style() || value < 0 || value > 8)
    return;
  m_options.style = value;
  invalidateSaved();
  persistOptions();
  scheduleRender();
}
void Studio::setPadding(double value) {
  value = std::clamp(value, 0.02, 0.22);
  if (m_busy || qFuzzyCompare(value, padding()))
    return;
  m_options.padding = value;
  invalidateSaved();
  persistOptions();
  scheduleRender();
}
void Studio::setAspect(int value) {
  if (m_busy || value == aspect() || value < 0 || value > 4)
    return;
  m_options.aspect = value;
  invalidateSaved();
  persistOptions();
  scheduleRender();
}

void Studio::setEditing(bool value) {
  if (m_editing == value)
    return;
  m_editing = value;
  emit changed();
  if (!m_editing && m_thumbnailsStale && !m_original.isNull())
    scheduleRender();
}
QString Studio::originalsFolder() const { return originalsDirectory(); }
bool Studio::notifications() const {
  return QSettings().value("notifications", true).toBool();
}
void Studio::setNotifications(bool value) {
  if (value == notifications())
    return;
  QSettings().setValue("notifications", value);
  emit changed();
}
bool Studio::welcomed() const {
  return QSettings().value("welcomed", false).toBool();
}
void Studio::setWelcomed(bool value) {
  if (value == welcomed())
    return;
  QSettings().setValue("welcomed", value);
  emit changed();
}
bool Studio::keepOriginals() const {
  return QSettings().value("privacy/keepOriginals", false).toBool();
}
void Studio::setKeepOriginals(bool value) {
  if (value == keepOriginals())
    return;
  QSettings().setValue("privacy/keepOriginals", value);
  emit changed();
}

struct PreviewResult {
  QImage source, uncropped, preview;
  QVector<QImage> thumbnails;
  QSize workingSize;
  QMargins edgeRoom;
};
void Studio::scheduleRender() {
  if (m_draftDirty)
    m_draftTimer.start();
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
            m_store->put("uncropped", result.uncropped);
            m_store->put("preview", result.preview);
            for (int i = 0; i < result.thumbnails.size(); ++i)
              m_store->put(QString("style%1").arg(i), result.thumbnails[i]);
            m_workingSize = result.workingSize;
            m_edgeRoom = result.edgeRoom;
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
  const QVector<Frame::Edit> edits = m_marks.visibleEdits();
  const bool thumbnails = !m_editing;
  m_thumbnailsStale = !thumbnails;
  watcher->setFuture(QtConcurrent::run(
      [source = m_original, edits, options = m_options, thumbnails] {
        PreviewResult result;
        const QImage uncropped = Frame::applyEdits(source, edits, false);
        const QImage working = Frame::cropImage(uncropped, edits);
        result.workingSize = working.size();
        result.edgeRoom = Frame::edgeRoom(working);
        result.uncropped = uncropped.scaled(1800, 1800, Qt::KeepAspectRatio,
                                            Qt::SmoothTransformation);
        result.source = working.scaled(1800, 1800, Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation);
        result.preview = Frame::compose(working, options, 1600);
        for (int i = 0; thumbnails && i < 9; ++i) {
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
  saveDraftNow();
  image.setDevicePixelRatio(1);
  m_original = std::move(image);
  m_workingSize = m_original.size();
  m_edgeRoom = {};
  m_name = std::move(name);
  m_demo = demo;
  m_marks.reset(m_original);
  invalidateSaved();
  m_draftTimer.stop();
  m_draftDirty = false;
  m_draftId.clear();
  m_status = demo ? "Sample image. Try a finish or the editor."
                  : "Ready when you are.";
  startReading();
  scheduleRender();
  emit sourceChanged();
}
void Studio::closeImage() {
  if (m_busy || m_original.isNull())
    return;
  saveDraftNow();
  ++m_generation;
  m_original = {};
  m_workingSize = {};
  m_edgeRoom = {};
  m_name.clear();
  m_marks.reset({});
  stopReading();
  m_draftId.clear();
  m_draftDirty = false;
  m_rendering = false;
  invalidateSaved();
  m_status = "Ready when you are.";
  emit changed();
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
void Studio::setOutputDirectory(const QUrl &url) {
  if (!url.isLocalFile() || m_busy)
    return;
  m_directory = url.toLocalFile();
  QSettings().setValue("outputDirectory", m_directory);
  if (m_quickMode && m_quickState == "failed" && recoveryAction().isEmpty()) {
    m_quickState = "choosing";
    m_status = "Save folder changed. Choose a finish to try again.";
  }
  emit changed();
}
void Studio::revealSaved() {
  if (!m_savedPath.isEmpty())
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(QFileInfo(m_savedPath).absolutePath()));
}
void Studio::refreshDrafts() {
  QVariantList drafts;
  const QDir directory(draftsDirectory());
  const auto files = directory.entryInfoList({"*.json"}, QDir::Files | QDir::NoSymLinks,
                                             QDir::Time);
  for (const auto &file : files) {
    const QString id = file.completeBaseName();
    if (!validDraftId(id) || file.size() > maxDraftMetadataBytes ||
        !QFileInfo::exists(directory.filePath(id + ".png")))
      continue;
    QFile input(file.absoluteFilePath());
    if (!input.open(QIODevice::ReadOnly))
      continue;
    const auto document = QJsonDocument::fromJson(input.readAll()).object();
    if (document.value("version").toInt() != 1)
      continue;
    drafts.append(QVariantMap{
        {"id", id},
        {"name", document.value("name").toString("Screenshot")},
        {"when", file.lastModified().toString("MMM d, h:mm AP")},
        {"modified", file.lastModified().toMSecsSinceEpoch()},
        {"edits", document.value("edits").toArray().size()},
        {"image", QUrl::fromLocalFile(directory.filePath(id + ".png")).toString()},
        {"exported", QFileInfo::exists(document.value("savedPath").toString())}});
  }
  m_drafts = drafts;
  emit changed();
}
void Studio::saveDraftNow() {
  if (m_marks.transforming()) {
    m_draftTimer.start();
    return;
  }
  m_draftTimer.stop();
  if (!m_draftDirty || m_demo || m_original.isNull())
    return;
  if (m_draftId.isEmpty() && m_marks.edits().isEmpty()) {
    m_draftDirty = false;
    return;
  }
  const QString directory = draftsDirectory();
  if (!QDir().mkpath(directory)) {
    m_status = "Could not save editable draft. Check the application data folder.";
    emit changed();
    return;
  }
  QFile::setPermissions(directory, QFile::ReadOwner | QFile::WriteOwner |
                                      QFile::ExeOwner);
  if (m_draftId.isEmpty())
    m_draftId = QUuid::createUuid().toString(QUuid::Id128);
  const QString sourcePath = directory + "/" + m_draftId + ".png";
  if (!QFileInfo::exists(sourcePath)) {
    QSaveFile source(sourcePath);
    if (!source.open(QIODevice::WriteOnly) ||
        !source.setPermissions(QFile::ReadOwner | QFile::WriteOwner)) {
      m_status = "Could not save the private draft image.";
      emit changed();
      return;
    }
    QImageWriter writer(&source, "png");
    if (!writer.write(m_original) || !source.commit()) {
      m_status = "Could not finish saving the private draft image.";
      emit changed();
      return;
    }
  }
  QJsonArray edits;
  for (const auto &edit : m_marks.edits())
    edits.append(Frame::editToJson(edit));
  const QJsonObject document{{"version", 1}, {"name", m_name},
                             {"style", m_options.style},
                             {"padding", m_options.padding},
                             {"aspect", m_options.aspect},
                             {"selected", m_marks.selected()},
                             {"savedPath", m_savedPath}, {"edits", edits}};
  QSaveFile metadata(directory + "/" + m_draftId + ".json");
  const QByteArray serialized = QJsonDocument(document).toJson(QJsonDocument::Compact);
  if (!metadata.open(QIODevice::WriteOnly) ||
      !metadata.setPermissions(QFile::ReadOwner | QFile::WriteOwner) ||
      metadata.write(serialized) != serialized.size() ||
      !metadata.commit()) {
    m_status = "Could not finish saving the editable draft.";
    emit changed();
    return;
  }
  m_draftDirty = false;
  refreshDrafts();
}
void Studio::resumeDraft(const QString &id) {
  if (m_busy || !validDraftId(id))
    return;
  saveDraftNow();
  const QString directory = draftsDirectory();
  QFile metadata(directory + "/" + id + ".json");
  if (!metadata.open(QIODevice::ReadOnly) ||
      metadata.size() > maxDraftMetadataBytes) {
    m_status = "Could not read that editable draft.";
    emit changed();
    return;
  }
  const QJsonObject document = QJsonDocument::fromJson(metadata.readAll()).object();
  const QJsonArray savedEdits = document.value("edits").toArray();
  if (document.value("version").toInt() != 1 || savedEdits.size() > MarkDocument::MaxEdits) {
    m_status = "This editable draft is damaged or unsupported.";
    emit changed();
    return;
  }
  QVector<Frame::Edit> edits;
  for (const auto &value : savedEdits) {
    const auto edit = Frame::editFromJson(value.toObject());
    if (!edit) {
      m_status = "This editable draft contains a damaged annotation.";
      emit changed();
      return;
    }
    edits.append(*edit);
  }
  QImageReader reader(directory + "/" + id + ".png");
  const QSize size = reader.size();
  if (!size.isValid() || qint64(size.width()) * size.height() > 40000000) {
    m_status = "This editable draft has an invalid source image.";
    emit changed();
    return;
  }
  QImage image = reader.read();
  if (image.isNull()) {
    m_status = "Could not reopen the draft image.";
    emit changed();
    return;
  }
  m_original = std::move(image);
  m_workingSize = m_original.size();
  m_edgeRoom = {};
  m_marks.restore(m_original, std::move(edits),
                  document.value("selected").toInt(-1));
  m_options.style = std::clamp(document.value("style").toInt(), 0, 8);
  m_options.padding = std::clamp(document.value("padding").toDouble(0.05), 0.02, 0.22);
  m_options.aspect = std::clamp(document.value("aspect").toInt(), 0, 4);
  m_name = document.value("name").toString("Screenshot");
  m_demo = false;
  m_savedPath = document.value("savedPath").toString();
  if (!QFileInfo::exists(m_savedPath))
    m_savedPath.clear();
  m_backupPath.clear();
  m_copyPending = m_backupPending = false;
  m_draftId = id;
  m_draftDirty = false;
  m_draftTimer.stop();
  m_status = "Editable draft reopened.";
  startReading();
  scheduleRender();
  emit sourceChanged();
  emit editorRequested();
}
void Studio::deleteDraft(const QString &id) {
  if (m_busy || !validDraftId(id))
    return;
  const QString directory = draftsDirectory();
  bool removed = true;
  for (const QString &suffix : {".json", ".png"}) {
    const QString path = directory + "/" + id + suffix;
    if (QFileInfo::exists(path) && !QFile::remove(path))
      removed = false;
  }
  if (m_draftId == id) {
    m_draftTimer.stop();
    m_draftId.clear();
    m_draftDirty = false;
  }
  m_status = removed ? "Editable draft removed." : "Could not remove all draft files.";
  refreshDrafts();
}
void Studio::clearOriginals() {
  if (m_busy)
    return;
  QDir directory(originalsDirectory());
  int removed = 0, failed = 0;
  for (const auto &file : directory.entryList({"*.png"}, QDir::Files | QDir::NoSymLinks)) {
    if (directory.remove(file))
      ++removed;
    else
      ++failed;
  }
  m_originalsSummary = summarizeOriginals(&m_originalsCount);
  m_status = failed ? QString("Removed %1 originals; %2 could not be removed.")
                          .arg(removed).arg(failed)
                    : QString("Removed %1 private originals.").arg(removed);
  emit changed();
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
static bool copyPng(const QString &path, QString &error) {
  QFile input(path);
  if (!input.open(QIODevice::ReadOnly)) {
    error = "Could not read the saved PNG for clipboard copy.";
    return false;
  }
  QProcess clipboard;
  clipboard.start("wl-copy", {"--type", "image/png"});
  if (!clipboard.waitForStarted(3000)) {
    error = "Could not start wl-copy. Check that it is installed.";
    return false;
  }
  clipboard.write(input.readAll());
  clipboard.closeWriteChannel();
  const bool copied = clipboard.waitForFinished(10000) &&
                      clipboard.exitStatus() == QProcess::NormalExit &&
                      clipboard.exitCode() == 0;
  if (clipboard.state() != QProcess::NotRunning) {
    clipboard.kill();
    clipboard.waitForFinished();
  }
  if (!copied)
    error = "wl-copy could not accept the saved PNG.";
  return copied;
}
struct ExportResult {
  QString path, backupPath, error, copyError, backupError;
  bool copied = false, backupSaved = false;
};
static QString exportStatus(const ExportResult &r) {
  if (r.path.isEmpty())
    return r.error;
  if (r.copied && r.backupSaved)
    return "Copied to the clipboard and saved as " + QFileInfo(r.path).fileName() + ".";
  QStringList problems;
  if (!r.copied)
    problems << "Clipboard copy failed: " + r.copyError;
  if (!r.backupSaved)
    problems << "Private original backup failed: " + r.backupError;
  return "Finished PNG saved. " + problems.join(" ");
}
void Studio::accept() {
  if (m_busy || m_rendering || m_original.isNull())
    return;
  saveDraftNow();
  const QSize output = Frame::outputSize(m_workingSize, m_options, m_edgeRoom);
  if (qint64(output.width()) * output.height() > 80000000) {
    if (m_quickMode)
      m_quickState = "failed";
    m_status = "This canvas would exceed 80 megapixels. Use a smaller border "
               "or a different aspect ratio.";
    emit changed();
    return;
  }
  m_busy = true;
  m_copyPending = m_backupPending = false;
  m_backupPath.clear();
  if (m_quickMode)
    m_quickState = "saving";
  m_status = "Saving full-resolution PNG and copying…";
  emit changed();
  const QString originalDirectory = keepOriginals() ? originalsDirectory() : QString();
  auto *watcher = new QFutureWatcher<ExportResult>(this);
  connect(watcher, &QFutureWatcher<ExportResult>::finished, this,
          [this, watcher] {
            auto r = watcher->result();
            watcher->deleteLater();
            m_busy = false;
            m_savedPath = r.path;
            if (!r.path.isEmpty() && !m_draftId.isEmpty()) {
              m_draftDirty = true;
              m_draftTimer.start();
            }
            m_backupPath = r.backupPath;
            m_copyPending = !r.path.isEmpty() && !r.copied;
            m_backupPending = !r.path.isEmpty() && !r.backupSaved;
            m_status = exportStatus(r);
            m_originalsSummary = summarizeOriginals(&m_originalsCount);
            if (m_quickMode)
              m_quickState = r.path.isEmpty() || m_copyPending || m_backupPending
                                 ? "failed"
                                 : "done";
            emit changed();
            // Keep failures visible. Never dismiss a capture that was not both
            // saved and copied, including a failed original backup.
            if (m_quickMode && m_quickState == "done")
              emit dismissRequested();
          });
  watcher->setFuture(QtConcurrent::run([source = m_original, edits = m_marks.edits(),
                                        options = m_options,
                                        directory = m_directory,
                                        originalDirectory] {
    ExportResult r;
    if (!QDir().mkpath(directory)) {
      r.error = "Could not create the save folder. Choose another folder and try again.";
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
    if (originalDirectory.isEmpty()) {
      // Keeping private originals is off: nothing else to write.
      r.backupSaved = true;
    } else {
      r.backupPath = originalDirectory + "/" + id + ".png";
      r.backupSaved = QDir().mkpath(originalDirectory) &&
                      writePng(r.backupPath, source, r.backupError, true);
      if (!r.backupSaved && r.backupError.isEmpty())
        r.backupError = "Could not create the original backup folder.";
    }
    r.copied = copyPng(path, r.copyError);
    return r;
  }));
}
void Studio::retryOutput() {
  if (m_busy || m_savedPath.isEmpty() || (!m_copyPending && !m_backupPending))
    return;
  m_busy = true;
  if (m_quickMode)
    m_quickState = "saving";
  m_status = "Retrying the saved screenshot…";
  emit changed();
  auto *watcher = new QFutureWatcher<ExportResult>(this);
  connect(watcher, &QFutureWatcher<ExportResult>::finished, this,
          [this, watcher] {
            const auto r = watcher->result();
            watcher->deleteLater();
            m_busy = false;
            m_copyPending = !r.copied;
            m_backupPending = !r.backupSaved;
            m_status = exportStatus(r);
            m_originalsSummary = summarizeOriginals(&m_originalsCount);
            if (m_quickMode)
              m_quickState = m_copyPending || m_backupPending ? "failed" : "done";
            emit changed();
            if (m_quickMode && m_quickState == "done")
              emit dismissRequested();
          });
  watcher->setFuture(QtConcurrent::run([path = m_savedPath,
                                        backupPath = m_backupPath,
                                        source = m_original,
                                        copyPending = m_copyPending,
                                        backupPending = m_backupPending] {
    ExportResult r;
    r.path = path;
    r.backupPath = backupPath;
    r.copied = !copyPending || copyPng(path, r.copyError);
    r.backupSaved = !backupPending ||
                    (QDir().mkpath(QFileInfo(backupPath).absolutePath()) &&
                     writePng(backupPath, source, r.backupError, true));
    if (!r.backupSaved && r.backupError.isEmpty())
      r.backupError = "Could not create the original backup folder.";
    return r;
  }));
}

void Studio::capture(bool region, int monitor) {
  captureImpl(region, monitor, false);
}
void Studio::repeatLastArea() {
  if (!hasLastArea()) {
    capture(true);
    return;
  }
  m_recordingSelection = false;
  captureImpl(false, 0, true);
}
void Studio::captureImpl(bool region, int monitor, bool repeat) {
  if (m_busy)
    return;
  const auto open = QGuiApplication::allWindows();
  // A capture started from the open studio window returns there afterwards.
  // Keep that until it is used: going through recording options hides the
  // window before the second selector opens.
  if (!m_quickMode && !m_returnToStudio)
    m_returnToStudio = std::any_of(open.cbegin(), open.cend(), [](QWindow *w) {
      return w->isVisible() && w->title() == "Omaframe";
    });
  m_busy = true;
  m_quickMode = true;
  m_quickState = "capturing";
  m_pendingFinish = -1;
  m_status = repeat ? "Capturing the last area…" : "Capturing…";
  m_frozen.clear();
  m_windowTargets.clear();
  m_pointerMonitor.clear();
  // Only wait for dismissal animations when this process has a visible UI.
  // A fresh shortcut launch has nothing of its own to hide.
  const auto windows = QGuiApplication::allWindows();
  const bool wasVisible =
      std::any_of(windows.cbegin(), windows.cend(),
                  [](QWindow *w) { return w->isVisible(); });
  emit changed();
  emit hideStudio();
  QStringList requested, screens;
  for (QScreen *screen : QGuiApplication::screens())
    screens << screen->name();
  // A region starts on whichever display the user chooses with the pointer.
  // Snapshot all displays before placing any selection overlays.
  if (repeat)
    requested << m_lastAreaMonitor;
  else if (region && monitor == 0)
    requested = screens;
  else if (const auto screens = QGuiApplication::screens();
             monitor > 0 && monitor <= screens.size())
    requested << screens[monitor - 1]->name();
  QTimer::singleShot(
      wasVisible ? 220 : 0, this,
      [this, region, repeat, requested, screens, lastArea = m_lastArea,
       lastPixels = m_lastAreaPixels]() mutable {
        struct SelectionCapture {
          Capture::Screens screens;
          QVariantList targets;
          QString pointer;
          /** The last area's display is off, so a repeat selects again. */
          bool lastAreaDark = false;
        };
        auto *watcher = new QFutureWatcher<SelectionCapture>(this);
        connect(watcher, &QFutureWatcher<SelectionCapture>::finished, this,
                [this, watcher, region, repeat, lastArea, lastPixels] {
                  auto result = watcher->result();
                  watcher->deleteLater();
                  if (!result.screens.error.isEmpty()) {
                    // Report it where the user is looking, not on the primary
                    // display, which may be off or out of sight.
                    if (!result.pointer.isEmpty())
                      m_captureMonitor = result.pointer;
                    m_busy = false;
                    m_quickState = "capture-error";
                    m_status = "Capture failed: " + result.screens.error;
                    emit changed();
                    emit captureFailed();
                    return;
                  }
                  QImage repeated;
                  if (repeat && !result.lastAreaDark) {
                    const auto frame = result.screens.images.constBegin();
                    if (frame.value().size() == lastPixels)
                      repeated = Capture::crop(result.screens.images, frame.key(),
                                               lastArea.topLeft(),
                                               lastArea.bottomRight());
                  }
                  if (region || (repeat && repeated.isNull())) {
                    m_frozen = result.screens.images;
                    m_windowTargets = result.targets;
                    // F can only answer for a display that was frozen.
                    m_pointerMonitor = m_frozen.contains(result.pointer)
                                           ? result.pointer
                                           : QString();
                    for (auto it = m_frozen.cbegin(); it != m_frozen.cend();
                         ++it)
                      m_store->put("capture/" + it.key(), it.value());
                    m_quickState = "selecting";
                    if (repeat)
                      m_status = result.lastAreaDark
                                     ? "That display is off. Select an area again."
                                     : "The display changed. Select an area again.";
                    ++m_revision;
                    emit changed();
                    emit selectionReady(m_frozen.keys());
                  } else {
                    m_busy = false;
                    const auto frame = result.screens.images.constBegin();
                    m_captureMonitor = frame.key();
                    loadImage(repeat ? repeated : frame.value(),
                              repeat ? "Repeated area capture" : "Screen capture",
                              false);
                    m_quickState = "choosing";
                    emit changed();
                    emit chooserRequested();
                  }
                });
        watcher->setFuture(QtConcurrent::run([requested, screens, region,
                                              repeat]() mutable {
          if (requested.isEmpty()) {
            QProcess process;
            process.start("hyprctl", {"-j", "monitors"});
            if (!process.waitForFinished(2000)) {
              process.kill();
              process.waitForFinished();
              return SelectionCapture{
                  {{}, "Could not identify the active display."}, {}};
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
          SelectionCapture result;
          if (region || repeat) {
            auto query = [](const QString &what) {
              QProcess p;
              p.start("hyprctl", {"-j", what});
              if (!p.waitForFinished(1000)) {
                p.kill();
                p.waitForFinished();
                return QJsonArray{};
              }
              return QJsonDocument::fromJson(p.readAllStandardOutput()).array();
            };
            const auto monitors = query("monitors");
            const auto clients = query("clients");
            // A display that is off never sends a frame. Repeating an area
            // there selects again on the displays that are on, like a
            // repeat after the display changed size.
            if (repeat && WindowTargets::dark(monitors).contains(requested.value(0))) {
              requested = screens;
              result.lastAreaDark = true;
            }
            requested = WindowTargets::awake(monitors, requested);
            result.targets =
                WindowTargets::fromHyprland(monitors, clients, requested);
            // The display under the pointer answers F before the pointer moves.
            QProcess cursor;
            cursor.start("hyprctl", {"-j", "cursorpos"});
            if (cursor.waitForFinished(1000)) {
              const auto at = QJsonDocument::fromJson(cursor.readAllStandardOutput()).object();
              const QPointF point(at.value("x").toDouble(), at.value("y").toDouble());
              for (const auto &value : monitors) {
                const auto m = value.toObject();
                const double scale = m.value("scale").toDouble(1);
                int width = m.value("width").toInt(), height = m.value("height").toInt();
                if (m.value("transform").toInt() % 2)
                  std::swap(width, height);
                if (scale > 0 && QRectF(m.value("x").toDouble(), m.value("y").toDouble(),
                                        width / scale, height / scale)
                                     .contains(point))
                  result.pointer = m.value("name").toString();
              }
            } else {
              cursor.kill();
              cursor.waitForFinished();
            }
          }
          result.screens = Capture::freeze(requested, [](const QString &name,
                                                         QImage &image,
                                                         QString &error) {
            MonitorInfo info;
            info.name = name;
            return captureOutputSurface(info, image, error);
          });
          return result;
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
  const QSize imagePixels = m_frozen.value(monitor).size();
  m_captureMonitor = monitor;
  for (const auto &name : m_frozen.keys())
    m_store->put("capture/" + name, {});
  m_frozen.clear();
  m_busy = false;
  emit selectionDone();
  if (m_recordingSelection) {
    leaveQuickMode();
    emit recordRegionSelected(monitor, QRectF(QPointF(x1, y1), QPointF(x2, y2))
                                           .normalized()
                                           .intersected(QRectF(0, 0, 1, 1)));
    return;
  }
  m_lastAreaMonitor = monitor;
  m_lastArea = QRectF(QPointF(x1, y1), QPointF(x2, y2))
                   .normalized()
                   .intersected(QRectF(0, 0, 1, 1));
  m_lastAreaPixels = imagePixels;
  QSettings settings;
  settings.setValue("lastArea/monitor", m_lastAreaMonitor);
  settings.setValue("lastArea/rect", m_lastArea);
  settings.setValue("lastArea/pixels", m_lastAreaPixels);
  const bool whole = m_lastArea.left() <= 0.001 && m_lastArea.top() <= 0.001 &&
                     m_lastArea.right() >= 0.999 && m_lastArea.bottom() >= 0.999;
  loadImage(result, whole ? "Display capture" : "Region capture", false);
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
  if (!recoveryAction().isEmpty() && value == style()) {
    retryOutput();
    return;
  }
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

struct ReadResult {
  bool ok = false;
  QString text;
  QVector<QRectF> secrets;
};
void Studio::stopReading() {
  if (m_readCancel)
    m_readCancel->store(true);
  m_readCancel.reset();
  ++m_readGeneration;
  m_reading = m_textRead = m_copyTextPending = false;
  m_secrets.clear();
  m_text.clear();
  m_textNote.clear();
}
void Studio::startReading() {
  stopReading();
  static const bool installed = Ocr::available();
  if (!installed || m_demo || m_original.isNull())
    return;
  m_reading = true;
  m_readCancel = std::make_shared<std::atomic_bool>(false);
  const int generation = m_readGeneration;
  auto *watcher = new QFutureWatcher<ReadResult>(this);
  connect(watcher, &QFutureWatcher<ReadResult>::finished, this,
          [this, watcher, generation] {
            const ReadResult result = watcher->result();
            watcher->deleteLater();
            if (generation != m_readGeneration)
              return;
            m_reading = false;
            m_textRead = result.ok;
            m_text = result.text;
            m_secrets = result.secrets;
            if (m_copyTextPending) {
              m_copyTextPending = false;
              if (result.ok)
                writeText();
              else
                m_textNote = "Could not read the text.";
            }
            emit changed();
          });
  // The picker never waits for this. A finish chosen first is saved as
  // usual, and a new image cancels the read.
  watcher->setFuture(QtConcurrent::run(
      [image = m_original, cancel = m_readCancel] {
        ReadResult result;
        const auto words = Ocr::read(image, cancel.get());
        if (!words)
          return result;
        result.ok = true;
        result.text = Ocr::text(*words);
        const QSizeF size = image.size();
        for (const QRect &found : Ocr::findSecrets(*words)) {
          // A little room past the letters, so no edge of one shows.
          const double grow = 3 + found.height() * 0.15;
          const QRectF area = QRectF(found).adjusted(-grow, -grow, grow, grow);
          result.secrets << QRectF(area.x() / size.width(),
                                   area.y() / size.height(),
                                   area.width() / size.width(),
                                   area.height() / size.height())
                                .intersected(QRectF(0, 0, 1, 1));
        }
        return result;
      }));
}
QVector<QRectF> Studio::uncoveredSecrets() const {
  QVector<QRectF> open;
  const auto &edits = m_marks.edits();
  for (const QRectF &secret : m_secrets) {
    const double area = secret.width() * secret.height();
    const bool hidden =
        std::any_of(edits.cbegin(), edits.cend(), [&](const Frame::Edit &e) {
          if (e.type != "redact" && e.type != "blur")
            return false;
          const QRectF part =
              QRectF(e.from, e.to).normalized().intersected(secret);
          return part.width() * part.height() >= area * 0.9;
        });
    if (!hidden)
      open << secret;
  }
  return open;
}
QString Studio::textNote() const {
  return m_copyTextPending ? QString("Reading text…") : m_textNote;
}
void Studio::hideSecrets() {
  if (m_busy)
    return;
  const QVector<QRectF> open = uncoveredSecrets();
  if (open.isEmpty())
    return;
  const qsizetype before = m_marks.edits().size();
  m_marks.redactAreas(open);
  if (m_marks.edits().size() == before)
    return;
  // Never claim the image is clean: OCR misses things.
  m_textNote = open.size() == 1
                   ? "Hid 1 possible secret. OCR can miss some, so check "
                     "before sharing."
                   : QString("Hid %1 possible secrets. OCR can miss some, so "
                             "check before sharing.")
                         .arg(open.size());
  m_status = m_textNote;
  emit changed();
}
void Studio::copyText() {
  if (!canReadText())
    return;
  if (m_reading) {
    m_copyTextPending = true;
    emit changed();
    return;
  }
  writeText();
  emit changed();
}
void Studio::writeText() {
  const QString text = m_text.trimmed();
  if (text.isEmpty()) {
    m_textNote = "No text found.";
    return;
  }
  QProcess clipboard;
  clipboard.start("wl-copy", {"--type", "text/plain;charset=utf-8"});
  bool copied = clipboard.waitForStarted(3000);
  if (copied) {
    clipboard.write(text.toUtf8());
    clipboard.closeWriteChannel();
    copied = clipboard.waitForFinished(3000) &&
             clipboard.exitStatus() == QProcess::NormalExit &&
             clipboard.exitCode() == 0;
  }
  if (clipboard.state() != QProcess::NotRunning) {
    clipboard.kill();
    clipboard.waitForFinished();
  }
  m_textNote = copied ? "Copied the text." : "Could not copy the text.";
}

void Studio::recordInstead(const QString &monitor) {
  if (m_quickState != "selecting")
    return;
  if (!monitor.isEmpty())
    m_captureMonitor = monitor;
  // Stay in the selector: the same drag or click now starts a recording.
  m_recordingSelection = true;
  emit changed();
  emit recordModeEntered();
}
void Studio::recordingOptions(const QString &monitor) {
  if (m_quickState != "selecting")
    return;
  if (!monitor.isEmpty())
    m_captureMonitor = monitor;
  for (const auto &name : m_frozen.keys())
    m_store->put("capture/" + name, {});
  m_frozen.clear();
  m_busy = false;
  emit selectionDone();
  leaveQuickMode();
  emit recordOptionsRequested();
}
void Studio::captureVideo() {
  if (m_busy)
    return;
  m_recordingSelection = true;
  captureImpl(true, 0, false);
  emit recordModeEntered();
}
