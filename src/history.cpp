#include "history.hpp"
#include "edit-json.hpp"
#include "marks.hpp"
#include "recording.hpp"
#include "studio.hpp"
#include <QClipboard>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QMimeData>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QtConcurrent>
#include <algorithm>
#include <fcntl.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
constexpr auto privateFile = QFile::ReadOwner | QFile::WriteOwner;
constexpr auto privateDir = privateFile | QFile::ExeOwner;
QMutex cacheMutex;
History::Identity identity(const struct stat &s) {
  return {quint64(s.st_dev),
          quint64(s.st_ino),
          s.st_size,
          s.st_mtim.tv_sec * 1000000000LL + s.st_mtim.tv_nsec,
          s.st_ctim.tv_sec * 1000000000LL + s.st_ctim.tv_nsec,
          true};
}
QByteArray readFile(const QString &path, const History::Identity &expected,
                    qint64 limit) {
  const int fd = ::open(QFile::encodeName(path).constData(),
                        O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0)
    return {};
  struct stat s{};
  QByteArray bytes;
  if (::fstat(fd, &s) == 0 && S_ISREG(s.st_mode) && identity(s) == expected &&
      s.st_size <= limit) {
    QFile file;
    if (file.open(fd, QIODevice::ReadOnly, QFileDevice::DontCloseHandle))
      bytes = file.readAll();
    if (::fstat(fd, &s) != 0 || identity(s) != expected)
      bytes.clear();
  }
  ::close(fd);
  return bytes;
}
QJsonObject document(const QString &path, const History::Identity &id) {
  return QJsonDocument::fromJson(readFile(path, id, 2 * 1024 * 1024)).object();
}
bool safeFolder(const QString &path) {
  const auto data =
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  const auto runtime =
      QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
  const QString canonical = QFileInfo(path).canonicalFilePath();
  if (canonical.isEmpty() ||
      canonical != QDir::cleanPath(QFileInfo(path).absoluteFilePath()))
    return false;
  for (const auto &privatePath : {data, runtime})
    if (!privatePath.isEmpty() &&
        (canonical == privatePath || canonical.startsWith(privatePath + '/')))
      return false;
  return true;
}
} // namespace
History::Identity History::identify(const QString &path) {
  const QFileInfo file(path);
  // Refuse symlinks in the file or any parent, including a folder replaced
  // since scanning.
  if (file.canonicalFilePath() != QDir::cleanPath(file.absoluteFilePath()))
    return {};
  struct stat s{};
  if (::lstat(QFile::encodeName(path).constData(), &s) != 0 ||
      !S_ISREG(s.st_mode))
    return {};
  return identity(s);
}
bool History::unchanged(const Entry &e) {
  if (!e.identity.valid || identify(e.path) != e.identity)
    return false;
  if (!e.draftPath.isEmpty() && identify(e.draftPath) != e.draftIdentity)
    return false;
  if (!e.sourcePath.isEmpty() && identify(e.sourcePath) != e.sourceIdentity)
    return false;
  return true;
}
QDateTime History::captureTime(const QString &name, qint64 modified) {
  static const QRegularExpression shot(
      "^Omaframe-([0-9]{4}-[0-9]{2}-[0-9]{2}_[0-9]{2}-[0-9]{2}-[0-9]{2}-[0-9]{"
      "3})-.+\\.png$");
  static const QRegularExpression video(
      "^Recording-([0-9]{4}-[0-9]{2}-[0-9]{2}_[0-9]{2}-[0-9]{2}-[0-9]{2})-.+\\."
      "mp4$");
  const auto match =
      name.startsWith("Omaframe-") ? shot.match(name) : video.match(name);
  if (match.hasMatch()) {
    const auto time =
        QDateTime::fromString(match.captured(1), name.startsWith("Omaframe-")
                                                     ? "yyyy-MM-dd_HH-mm-ss-zzz"
                                                     : "yyyy-MM-dd_HH-mm-ss");
    if (time.isValid())
      return time;
  }
  return QDateTime::fromMSecsSinceEpoch(modified);
}
QString History::dayLabel(const QDate &day, const QDate &today) {
  const auto age = day.daysTo(today);
  if (age == 0)
    return "Today";
  if (age == 1)
    return "Yesterday";
  const QLocale locale;
  if (age > 1 && age < 7)
    return locale.dayName(day.dayOfWeek(), QLocale::LongFormat);
  return locale.toString(day, day.year() == today.year() ? "ddd, MMM d"
                                                         : "ddd, MMM d, yyyy");
}
QString History::discoveredKind(const QString &name) {
  if (name.startsWith('.') || name.endsWith("-webcam.mp4") ||
      name.contains(".part") || name.contains(".cleaning"))
    return {};
  static const QRegularExpression shot("^Omaframe-[^/]+\\.png$");
  static const QRegularExpression recording("^Recording-[^/]+\\.mp4$");
  static const QRegularExpression edited(
      "^[^/.][^/]*-edited(?:-[2-9][0-9]*|-[1-9][0-9]+)?\\.(mp4|gif)$");
  if (shot.match(name).hasMatch())
    return "Screenshot";
  if (recording.match(name).hasMatch() || edited.match(name).hasMatch())
    return "Recording";
  return {};
}
QStringList History::previousFolders() {
  return QSettings().value("history/previousFolders").toStringList();
}
void History::rememberFolder(const QString &path) {
  if (path.isEmpty())
    return;
  auto folders = previousFolders();
  folders.removeAll(path);
  folders.prepend(path);
  while (folders.size() > 8)
    folders.removeLast();
  QSettings().setValue("history/previousFolders", folders);
}
QVector<History::Entry>
History::scan(const QStringList &folders, const QString &data,
              const std::shared_ptr<std::atomic_bool> &cancel) {
  QVector<Entry> result;
  QSet<QString> seen;
  for (const auto &folder : folders) {
    if (*cancel)
      return {};
    if (!safeFolder(folder))
      continue;
    Recording::recoverParts(folder);
    const auto files = QDir(folder).entryInfoList(
        QDir::Files | QDir::NoSymLinks, QDir::NoSort);
    for (const auto &file : files) {
      if (*cancel)
        return {};
      const auto kind = discoveredKind(file.fileName());
      const auto path = file.absoluteFilePath();
      if (kind.isEmpty() || seen.contains(path))
        continue;
      const auto id = identify(path);
      if (!id.valid)
        continue;
      seen.insert(path);
      Entry e;
      e.path = path;
      e.name = file.fileName();
      e.kind = kind;
      e.identity = id;
      e.modified = id.modified / 1000000;
      e.captured = captureTime(e.name, e.modified).toMSecsSinceEpoch();
      e.incomplete = e.name.endsWith("-incomplete.mp4");
      result.append(e);
    }
  }
  QHash<QString, int> exports;
  for (int i = 0; i < result.size(); ++i)
    exports.insert(result[i].path, i);
  for (const QString &kind : {"image", "video"}) {
    QDir dir(data + (kind == "image" ? "/drafts" : "/video-drafts"));
    for (const auto &file : dir.entryInfoList(
             {"*.json"}, QDir::Files | QDir::NoSymLinks, QDir::Time)) {
      if (*cancel)
        return {};
      const QString id = file.completeBaseName();
      static const QRegularExpression imageId("^[0-9a-f]{32}$"),
          videoId("^[0-9a-f]{64}$");
      if (!(kind == "image" ? imageId : videoId).match(id).hasMatch())
        continue;
      Entry e;
      e.path = e.draftPath = file.absoluteFilePath();
      e.identity = e.draftIdentity = identify(e.path);
      const auto doc = document(e.path, e.identity);
      if (doc.value("version").toInt() != 1)
        continue;
      e.draft = true;
      e.draftId = id;
      e.draftKind = kind;
      e.name = doc.value("name").toString(kind == "image" ? "Screenshot draft"
                                                          : "Video draft");
      e.kind = kind == "image" ? "Screenshot" : "Recording";
      e.modified = e.identity.modified / 1000000;
      e.captured = e.modified;
      e.sourcePath = kind == "image"
                         ? dir.filePath(id + ".png")
                         : QUrl(doc.value("source").toString()).toLocalFile();
      e.sourceIdentity = identify(e.sourcePath);
      if (!e.sourceIdentity.valid)
        continue;
      if (kind == "video" && (double(e.sourceIdentity.size) !=
                                  doc.value("sourceSize").toDouble() ||
                              double(e.sourceIdentity.modified / 1000000) !=
                                  doc.value("sourceModified").toDouble()))
        continue;
      const auto saved = doc.value("savedPath").toString();
      if (exports.contains(saved)) {
        auto &output = result[exports[saved]];
        // Newest linked draft wins. Never create a second raw-source row.
        if (output.draftId.isEmpty()) {
          output.draftId = id;
          output.draftKind = kind;
          output.draftPath = e.draftPath;
          output.draftIdentity = e.draftIdentity;
          output.sourcePath = e.sourcePath;
          output.sourceIdentity = e.sourceIdentity;
        }
      } else
        result.append(e);
    }
  }
  std::sort(result.begin(), result.end(), [](const Entry &a, const Entry &b) {
    return a.captured == b.captured ? a.path < b.path : a.captured > b.captured;
  });
  return result;
}
QImage History::draftPreview(const Entry &e) {
  if (!e.draft || e.draftKind != "image" || !unchanged(e))
    return {};
  const auto doc = document(e.draftPath, e.draftIdentity);
  if (doc.value("version") != 1 || !doc.value("edits").isArray() ||
      !doc.value("name").isString() || !doc.value("style").isDouble() ||
      !doc.value("padding").isDouble() || !doc.value("aspect").isDouble() ||
      !doc.value("selected").isDouble() || !doc.value("savedPath").isString())
    return {};
  const auto array = doc.value("edits").toArray();
  if (array.size() > MarkDocument::MaxEdits)
    return {};
  QVector<Frame::Edit> edits;
  for (const auto &value : array) {
    auto edit = Frame::editFromJson(value.toObject());
    if (!edit)
      return {};
    edit->stepOrder = std::max(0, value.toObject().value("stepOrder").toInt());
    edits.append(*edit);
  }
  QImageReader reader(e.sourcePath);
  reader.setAllocationLimit(200);
  if (!ScrollUi::withinImageBudget(reader.size()))
    return {};
  const auto source = reader.read();
  if (source.isNull())
    return {};
  QString error;
  const auto edited = Frame::applyEdits(source, edits, true, &error);
  if (!error.isEmpty() || edited.isNull())
    return {};
  Frame::Options options;
  options.style = std::clamp(doc.value("style").toInt(), 0, 8);
  options.padding = std::clamp(doc.value("padding").toDouble(.05), .02, .22);
  options.aspect = std::clamp(doc.value("aspect").toInt(), 0, 4);
  const auto finished = Frame::compose(edited, options, 1600, 2 * 1024 * 1024);
  if (!unchanged(e))
    return {};
  return finished.scaled(320, 200, Qt::KeepAspectRatio,
                         Qt::SmoothTransformation);
}
QString History::thumbnailKey(const Entry &e) {
  const auto id = e.identity;
  const auto bytes =
      QByteArray("v2:") + e.path.toUtf8() + QByteArray::number(id.device) +
      ':' + QByteArray::number(id.inode) + ':' + QByteArray::number(id.size) +
      ':' + QByteArray::number(id.modified) + ':' +
      QByteArray::number(id.changed) + ':' +
      QByteArray::number(e.sourceIdentity.modified) + ':' +
      QByteArray::number(e.sourceIdentity.changed);
  return QString::fromLatin1(QCryptographicHash::hash(
                                 e.path.toUtf8(), QCryptographicHash::Sha256)
                                 .toHex()
                                 .left(16)) +
         '-' +
         QString::fromLatin1(
             QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)
                 .toHex());
}
void History::trimCache(const QString &directory, qint64 cap) {
  if (QFileInfo(directory).canonicalFilePath() !=
      QDir::cleanPath(QFileInfo(directory).absoluteFilePath()))
    return;
  const auto files = QDir(directory).entryInfoList(
      {"*.jpg"}, QDir::Files | QDir::NoSymLinks, QDir::Time);
  qint64 bytes = 0;
  for (const auto &f : files)
    bytes += f.size();
  for (auto i = files.crbegin(); i != files.crend() && bytes > cap; ++i)
    if (QFile::remove(i->absoluteFilePath()))
      bytes -= i->size();
}
QImage History::thumbnail(const Entry &e, const QString &cache,
                          const std::shared_ptr<std::atomic_bool> &cancel,
                          qint64 *duration) {
  if (duration)
    *duration = -1;
  if (*cancel || !unchanged(e))
    return {};
  if (e.draft)
    return draftPreview(e);
  if (e.kind == "Screenshot") {
    QImageReader reader(e.path);
    reader.setAllocationLimit(32);
    const auto size = reader.size();
    if (!ScrollUi::withinImageBudget(size))
      return {};
    reader.setScaledSize(size.scaled(320, 200, Qt::KeepAspectRatio));
    auto image = reader.read();
    return !*cancel && unchanged(e) ? image : QImage();
  }
  const QString path = cache + '/' + thumbnailKey(e) + ".jpg";
  {
    QMutexLocker lock(&cacheMutex);
    if (identify(path).valid && QFileInfo(path).size() <= 1024 * 1024) {
      QFile::setPermissions(path, privateFile);
      QImageReader reader(path);
      reader.setAllocationLimit(2);
      const auto size = reader.size();
      QImage image = size.width() <= 320 && size.height() <= 200 ? reader.read()
                                                                 : QImage();
      if (!image.isNull() && image.width() <= 320 && image.height() <= 200) {
        bool ok = false;
        const auto ms = image.text("Description").toLongLong(&ok);
        if (duration && ok && ms >= 0)
          *duration = ms;
        return image;
      }
    }
  }
  QProcess ffmpeg;
  const auto parent = ::getpid();
  ffmpeg.setChildProcessModifier([parent] {
    ::prctl(PR_SET_PDEATHSIG, SIGKILL);
    if (::getppid() != parent)
      ::kill(::getpid(), SIGKILL);
  });
  ffmpeg.setProcessChannelMode(QProcess::SeparateChannels);
  ffmpeg.start("ffmpeg", {"-v",
                          "info",
                          "-nostdin",
                          "-threads",
                          "1",
                          "-i",
                          e.path,
                          "-map",
                          "0:v:0",
                          "-frames:v",
                          "1",
                          "-vf",
                          "scale=320:200:force_original_aspect_ratio=decrease",
                          "-threads",
                          "1",
                          "-f",
                          "image2pipe",
                          "-vcodec",
                          "mjpeg",
                          "pipe:1"});
  QByteArray bytes, diagnostics;
  QElapsedTimer timer;
  timer.start();
  while (ffmpeg.state() != QProcess::NotRunning && timer.elapsed() < 5000 &&
         !*cancel) {
    ffmpeg.waitForReadyRead(50);
    bytes += ffmpeg.readAllStandardOutput();
    const auto output = ffmpeg.readAllStandardError();
    if (diagnostics.size() < 64 * 1024)
      diagnostics += output.left(64 * 1024 - diagnostics.size());
    if (bytes.size() > 1024 * 1024)
      break;
  }
  bytes += ffmpeg.readAllStandardOutput();
  if (ffmpeg.state() != QProcess::NotRunning) {
    ffmpeg.kill();
    ffmpeg.waitForFinished(1000);
    return {};
  }
  if (*cancel || ffmpeg.exitCode() != 0 || bytes.size() > 1024 * 1024 ||
      !unchanged(e))
    return {};
  diagnostics += ffmpeg.readAllStandardError().left(
      std::max(0, 64 * 1024 - int(diagnostics.size())));
  static const QRegularExpression durationPattern(
      "Duration: ([0-9]+):([0-9]{2}):([0-9]{2})\\.([0-9]{2})");
  const auto match = durationPattern.match(QString::fromUtf8(diagnostics));
  qint64 ms = -1;
  if (match.hasMatch())
    ms = ((match.captured(1).toLongLong() * 60 + match.captured(2).toInt()) *
              60 +
          match.captured(3).toInt()) *
             1000 +
         match.captured(4).toInt() * 10;
  if (duration)
    *duration = ms;
  auto image = QImage::fromData(bytes, "JPEG");
  if (image.isNull() || image.width() > 320 || image.height() > 200)
    return {};
  QMutexLocker lock(&cacheMutex);
  if (*cancel || !unchanged(e))
    return {};
  if (!QDir().mkpath(cache) ||
      QFileInfo(cache).canonicalFilePath() !=
          QDir::cleanPath(QFileInfo(cache).absoluteFilePath()))
    return image;
  QFile::setPermissions(cache, privateDir);
  const auto prefix = thumbnailKey(e).left(17);
  for (const auto &old : QDir(cache).entryInfoList(
           {prefix + "*.jpg"}, QDir::Files | QDir::NoSymLinks))
    if (old.absoluteFilePath() != path)
      QFile::remove(old.absoluteFilePath());
  QSaveFile file(path);
  if (file.open(QIODevice::WriteOnly) && file.setPermissions(privateFile)) {
    QImageWriter writer(&file, "JPEG");
    writer.setText("Description", QString::number(ms));
    if (writer.write(image))
      file.commit();
  }
  trimCache(cache, 16 * 1024 * 1024);
  return image;
}
QImage HistoryImages::requestImage(const QString &id, QSize *size,
                                   const QSize &) {
  QMutexLocker lock(&mutex);
  const auto image = images.object(id);
  if (size)
    *size = image ? image->size() : QSize();
  return image ? *image : QImage();
}
void HistoryImages::put(const QString &key, const QImage &image) {
  QMutexLocker lock(&mutex);
  images.insert(key, new QImage(image), int(image.sizeInBytes()));
}
CaptureHistoryModel::CaptureHistoryModel(HistoryImages *images, QObject *parent)
    : QAbstractListModel(parent), m_images(images) {
  m_pool.setMaxThreadCount(2);
  m_previousFolders = previousFolders();
}
CaptureHistoryModel::~CaptureHistoryModel() {
  if (m_scanCancel)
    *m_scanCancel = true;
  cancelThumbnails();
  m_pool.waitForDone();
}
int CaptureHistoryModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : m_rows.size();
}
QHash<int, QByteArray> CaptureHistoryModel::roleNames() const {
  return {{Name, "captureName"},
          {Kind, "kind"},
          {Day, "day"},
          {When, "when"},
          {Detail, "detail"},
          {Incomplete, "incomplete"},
          {Draft, "draft"},
          {HasDraft, "hasDraft"},
          {Thumbnail, "thumbnail"},
          {FilePath, "filePath"},
          {CanEdit, "canEdit"},
          {Duration, "duration"}};
}
QVariant CaptureHistoryModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() >= m_rows.size())
    return {};
  const auto &e = m_rows[index.row()];
  const auto date = QDateTime::fromMSecsSinceEpoch(e.captured);
  switch (role) {
  case Name:
    return e.name;
  case Kind:
    return e.kind;
  case Day:
    return History::dayLabel(date.date());
  case When:
    return QLocale().toString(date.time(), QLocale::ShortFormat);
  case Detail:
    return e.draft ? "Editable draft"
                   : QString::number(e.identity.size / 1024) + " KiB";
  case Incomplete:
    return e.incomplete;
  case Draft:
    return e.draft;
  case HasDraft:
    return !e.draftId.isEmpty();
  case Thumbnail:
    return m_thumbnails.value(History::thumbnailKey(e));
  case CanEdit:
    return e.draft || !e.path.endsWith(".gif", Qt::CaseInsensitive);
  case Duration: {
    const auto ms = m_durations.value(History::thumbnailKey(e), -1);
    if (ms < 0)
      return QString();
    const auto seconds = ms / 1000;
    return QString::number(seconds / 60) + ':' +
           QString::number(seconds % 60).rightJustified(2, '0');
  }
  case FilePath:
    return e.path;
  }
  return {};
}
void CaptureHistoryModel::cancelThumbnails() {
  QMutexLocker lock(&cacheMutex);
  for (const auto &cancel : m_requests)
    *cancel = true;
  m_requests.clear();
}
void CaptureHistoryModel::rebuild() {
  cancelThumbnails();
  beginResetModel();
  m_rows.clear();
  m_thumbnails.clear();
  m_durations.clear();
  for (const auto &e : m_all) {
    if (!e.name.contains(m_search, Qt::CaseInsensitive))
      continue;
    if (m_filter == "Drafts" && e.draftId.isEmpty())
      continue;
    if (m_filter == "Screenshots" && e.kind != "Screenshot")
      continue;
    if (m_filter == "Recordings" && e.kind != "Recording")
      continue;
    m_rows.append(e);
  }
  endResetModel();
  emit changed();
}
void CaptureHistoryModel::setFilter(const QString &value) {
  if (m_filter != value) {
    m_filter = value;
    rebuild();
  }
}
void CaptureHistoryModel::setSearch(const QString &value) {
  if (m_search != value) {
    m_search = value;
    rebuild();
  }
}
void CaptureHistoryModel::foldersChanged() {
  const auto folders = previousFolders();
  if (m_previousFolders != folders) {
    m_previousFolders = folders;
    emit changed();
  }
}
void CaptureHistoryModel::removeFolder(const QString &path) {
  auto list = previousFolders();
  list.removeAll(path);
  QSettings().setValue("history/previousFolders", list);
  if (m_scanned)
    refresh();
  else
    emit changed();
}
void CaptureHistoryModel::refresh() {
  m_scanned = true;
  if (m_scanCancel)
    *m_scanCancel = true;
  cancelThumbnails();
  m_thumbnails.clear();
  if (!m_rows.isEmpty())
    emit dataChanged(index(0), index(m_rows.size() - 1), {Thumbnail});
  m_busy = true;
  emit changed();
  const int generation = ++m_generation;
  const auto cancel = m_scanCancel = std::make_shared<std::atomic_bool>(false);
  QSettings settings;
  QStringList folders = previousFolders();
  folders.prepend(
      settings
          .value("videoDirectory", QStandardPaths::writableLocation(
                                       QStandardPaths::MoviesLocation) +
                                       "/Omaframe")
          .toString());
  folders.prepend(
      settings
          .value("outputDirectory", QStandardPaths::writableLocation(
                                        QStandardPaths::PicturesLocation) +
                                        "/Omaframe")
          .toString());
  folders.removeDuplicates();
  const auto data =
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  auto *watcher = new QFutureWatcher<QVector<History::Entry>>(this);
  connect(watcher, &QFutureWatcherBase::finished, this,
          [this, watcher, generation] {
            auto rows = watcher->result();
            watcher->deleteLater();
            if (generation != m_generation)
              return;
            m_all = std::move(rows);
            m_busy = false;
            rebuild();
          });
  const auto cache =
      QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
      "/history";
  watcher->setFuture(QtConcurrent::run(&m_pool, [folders, data, cancel, cache] {
    auto entries = History::scan(folders, data, cancel);
    QSet<QString> valid;
    for (const auto &e : entries)
      if (!e.draft && e.kind == "Recording")
        valid.insert(History::thumbnailKey(e) + ".jpg");
    QMutexLocker lock(&cacheMutex);
    if (!*cancel && QFileInfo(cache).canonicalFilePath() ==
                        QDir::cleanPath(QFileInfo(cache).absoluteFilePath())) {
      for (const auto &f :
           QDir(cache).entryInfoList({"*.jpg"}, QDir::Files | QDir::NoSymLinks))
        if (!valid.contains(f.fileName()))
          QFile::remove(f.absoluteFilePath());
      History::trimCache(cache, 16 * 1024 * 1024);
    }
    return entries;
  }));
}
void CaptureHistoryModel::setVisibleRange(int first, int last) {
  QSet<QString> visible;
  first = std::max(0, first);
  last = std::min({last, first + 20, int(m_rows.size()) - 1});
  for (int i = first; i <= last; ++i)
    visible.insert(History::thumbnailKey(m_rows[i]));
  {
    QMutexLocker lock(&cacheMutex);
    for (auto i = m_requests.begin(); i != m_requests.end();) {
      if (!visible.contains(i.key())) {
        *i.value() = true;
        i = m_requests.erase(i);
      } else
        ++i;
    }
  }
  for (auto i = m_thumbnails.begin(); i != m_thumbnails.end();) {
    if (!visible.contains(i.key()))
      i = m_thumbnails.erase(i);
    else
      ++i;
  }
  const auto cache =
      QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
      "/history";
  for (int i = first; i <= last; ++i) {
    const auto e = m_rows[i];
    const auto key = History::thumbnailKey(e);
    if (m_thumbnails.contains(key) || m_requests.contains(key))
      continue;
    const auto cancel = std::make_shared<std::atomic_bool>(false);
    m_requests.insert(key, cancel);
    const int generation = m_generation;
    auto *watcher = new QFutureWatcher<QPair<QImage, qint64>>(this);
    connect(
        watcher, &QFutureWatcherBase::finished, this,
        [this, watcher, cancel, key, generation, row = i] {
          auto result = watcher->result();
          auto image = result.first;
          watcher->deleteLater();
          if (*cancel || generation != m_generation)
            return;
          m_durations.insert(key, result.second);
          m_requests.remove(key);
          m_thumbnails.insert(key, image.isNull() ? QString()
                                                  : "image://history/" + key);
          if (!image.isNull())
            m_images->put(key, image);
          if (row < m_rows.size() && History::thumbnailKey(m_rows[row]) == key)
            emit dataChanged(index(row), index(row), {Thumbnail, Duration});
        });
    watcher->setFuture(QtConcurrent::run(&m_pool, [e, cache, cancel] {
      qint64 duration;
      auto image = History::thumbnail(e, cache, cancel, &duration);
      return qMakePair(image, duration);
    }));
  }
}
QString CaptureHistoryModel::kindAt(int row) const {
  return row >= 0 && row < m_rows.size() ? m_rows[row].kind : QString();
}
QString CaptureHistoryModel::nameAt(int row) const {
  return row >= 0 && row < m_rows.size() ? m_rows[row].name : QString();
}
QString CaptureHistoryModel::keyAt(int row) const {
  return row >= 0 && row < m_rows.size() ? History::thumbnailKey(m_rows[row])
                                         : QString();
}
QString CaptureHistoryModel::pathAt(int row) const {
  return row >= 0 && row < m_rows.size() ? m_rows[row].path : QString();
}
bool CaptureHistoryModel::isDraftAt(int row) const {
  return row >= 0 && row < m_rows.size() && m_rows[row].draft;
}
bool CaptureHistoryModel::canEditAt(int row) const {
  return row >= 0 && row < m_rows.size() &&
         (m_rows[row].draft ||
          !m_rows[row].path.endsWith(".gif", Qt::CaseInsensitive));
}
bool CaptureHistoryModel::hasDraftAt(int row) const {
  return row >= 0 && row < m_rows.size() && !m_rows[row].draftId.isEmpty();
}
bool CaptureHistoryModel::action(int row, const QString &action,
                                 const QString &expectedKey) {
  if (row < 0 || row >= m_rows.size())
    return false;
  const auto e = m_rows[row];
  if ((!expectedKey.isEmpty() && expectedKey != History::thumbnailKey(e)) ||
      !History::unchanged(e)) {
    m_status =
        "This file is missing or changed. Refresh History and try again.";
    emit changed();
    refresh();
    return false;
  }
  bool ok = false;
  if (action == "resume" || (e.draft && action == "edit")) {
    emit navigate(e.draftKind == "video" ? "video-draft" : "image-draft",
                  QUrl(e.draftId));
    ok = true;
  } else if (e.draft && action == "delete-draft") {
    emit deleteDraft(e.draftKind, e.draftId);
    refresh();
    ok = true;
  } else if (!e.draft) {
    const auto url = QUrl::fromLocalFile(e.path);
    if (action == "edit" && canEditAt(row)) {
      emit navigate("open", url);
      ok = true;
    } else if (action == "external")
      ok = QDesktopServices::openUrl(url);
    else if (action == "reveal")
      ok = QDesktopServices::openUrl(
          QUrl::fromLocalFile(QFileInfo(e.path).absolutePath()));
    else if (action == "trash") {
      ok = QFile::moveToTrash(e.path);
      if (ok)
        refresh();
    } else if (action == "pin" && e.kind == "Screenshot") {
      QImage image;
      if (!image.loadFromData(readFile(e.path, e.identity, 256 * 1024 * 1024))) {
        m_status = "Could not read the saved image. It may have changed.";
        emit changed();
        return false;
      }
      emit pin(image);
      ok = true;
    } else if (action == "copy") {
      auto *mime = new QMimeData;
      if (e.kind == "Screenshot") {
        const auto bytes = readFile(e.path, e.identity, 256 * 1024 * 1024);
        if (bytes.isEmpty()) {
          delete mime;
          m_status = "Could not read the saved PNG. It may have changed.";
          emit changed();
          return false;
        }
        mime->setData("image/png", bytes);
      } else
        mime->setUrls({url});
      QGuiApplication::clipboard()->setMimeData(mime);
      ok = true;
    }
  }
  m_status = ok ? (action == "copy" ? "Copied again." : action == "pin" ? "Pinned to the screen." : QString())
                : "Could not complete that action.";
  emit changed();
  return ok;
}
