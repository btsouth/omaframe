#include "file-picker.hpp"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QWindow>
#include <algorithm>
#if __has_include(<QtGui/private/qdesktopunixservices_p.h>)
#include <QtGui/private/qdesktopunixservices_p.h>
#include <QtGui/private/qguiapplication_p.h>
#include <qpa/qplatformintegration.h>
#define OMAFRAME_PORTAL_WINDOW 1
#endif

namespace FilePicks {

Purpose purposeFromName(const QString &name) {
  if (name == "screenshots")
    return Purpose::ScreenshotFolder;
  if (name == "recordings")
    return Purpose::RecordingFolder;
  return Purpose::Open;
}
static QString purposeName(Purpose purpose) {
  switch (purpose) {
  case Purpose::ScreenshotFolder:
    return "screenshots";
  case Purpose::RecordingFolder:
    return "recordings";
  case Purpose::Open:
    break;
  }
  return "open";
}
bool isFolder(Purpose purpose) { return purpose != Purpose::Open; }

QStringList imageSuffixes() {
  return {"png", "jpg", "jpeg", "webp", "bmp", "gif", "avif", "heic"};
}
QStringList recordingSuffixes() {
  return {"mp4", "webm", "mkv", "mov", "m4v", "avi"};
}
bool isRecording(const QString &path) {
  return recordingSuffixes().contains(QFileInfo(path).suffix().toLower());
}

QDBusArgument &operator<<(QDBusArgument &argument, const Glob &glob) {
  argument.beginStructure();
  argument << glob.type << glob.pattern;
  argument.endStructure();
  return argument;
}
const QDBusArgument &operator>>(const QDBusArgument &argument, Glob &glob) {
  argument.beginStructure();
  argument >> glob.type >> glob.pattern;
  argument.endStructure();
  return argument;
}
QDBusArgument &operator<<(QDBusArgument &argument, const Filter &filter) {
  argument.beginStructure();
  argument << filter.label << filter.globs;
  argument.endStructure();
  return argument;
}
const QDBusArgument &operator>>(const QDBusArgument &argument, Filter &filter) {
  argument.beginStructure();
  argument >> filter.label >> filter.globs;
  argument.endStructure();
  return argument;
}
void registerPortalTypes() {
  static const bool once = [] {
    qDBusRegisterMetaType<Glob>();
    qDBusRegisterMetaType<QList<Glob>>();
    qDBusRegisterMetaType<Filter>();
    qDBusRegisterMetaType<QList<Filter>>();
    return true;
  }();
  Q_UNUSED(once)
}

static QStringList suffixesOf(bool images, bool recordings) {
  QStringList suffixes;
  if (images)
    suffixes += imageSuffixes();
  if (recordings)
    suffixes += recordingSuffixes();
  return suffixes;
}
static Filter filterOf(const QString &label, const QStringList &suffixes) {
  Filter filter{label, {}};
  // Portal patterns are case sensitive, and cameras write IMG_0001.JPG.
  for (const auto &suffix : suffixes)
    filter.globs << Glob{0, "*." + suffix} << Glob{0, "*." + suffix.toUpper()};
  return filter;
}
QList<Filter> mediaFilters() {
  return {filterOf("Images and recordings", suffixesOf(true, true)),
          filterOf("Images", suffixesOf(true, false)),
          filterOf("Recordings", suffixesOf(false, true))};
}
QStringList nameFilters() {
  const auto label = [](const QString &name, const QStringList &suffixes) {
    QStringList globs;
    for (const auto &suffix : suffixes)
      globs << "*." + suffix;
    return name + " (" + globs.join(' ') + ")";
  };
  return {label("Images and recordings", suffixesOf(true, true)),
          label("Images", suffixesOf(true, false)),
          label("Recordings", suffixesOf(false, true))};
}

QString firstExistingFolder(const QStringList &candidates, const QString &home) {
  for (const auto &candidate : candidates)
    if (!candidate.isEmpty() && QFileInfo(candidate).isDir())
      return QDir::cleanPath(candidate);
  return home;
}
QStringList startCandidates(Purpose purpose, bool recording,
                            const QString &lastImageFolder,
                            const QString &lastVideoFolder,
                            const QString &imageFolder,
                            const QString &videoFolder) {
  const auto pictures =
      QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
  const auto movies =
      QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
  QStringList candidates;
  switch (purpose) {
  case Purpose::Open:
    if (recording)
      candidates << lastVideoFolder << videoFolder << movies;
    else
      candidates << lastImageFolder << imageFolder;
    candidates << pictures;
    break;
  case Purpose::ScreenshotFolder:
    candidates << imageFolder << pictures;
    break;
  case Purpose::RecordingFolder:
    candidates << videoFolder << movies;
    break;
  }
  candidates.removeAll(QString());
  return candidates;
}

Request buildRequest(Purpose purpose, const QString &title,
                     const QString &parentWindow, const QString &folder,
                     const QString &token) {
  Request request{"OpenFile", parentWindow, title, {}};
  auto &options = request.options;
  options["handle_token"] = token;
  options["modal"] = true;
  options["multiple"] = false;
  if (isFolder(purpose))
    options["directory"] = true;
  else {
    const auto filters = mediaFilters();
    options["filters"] = QVariant::fromValue(filters);
    options["current_filter"] = QVariant::fromValue(filters.first());
  }
  // current_folder is a NUL terminated byte string, not a string.
  options["current_folder"] = QDir::toNativeSeparators(folder).toLocal8Bit() + '\0';
  return request;
}

static QStringList urisOf(const QVariant &value) {
  if (value.metaType() == QMetaType::fromType<QDBusArgument>())
    return qdbus_cast<QStringList>(value.value<QDBusArgument>());
  if (value.metaType() == QMetaType::fromType<QStringList>())
    return value.toStringList();
  QStringList uris;
  for (const auto &item : value.toList())
    uris << item.toString();
  return uris;
}
Reply parseResponse(uint code, const QVariantMap &results) {
  Reply reply;
  // 1 is Cancel and 2 is a dialog closed some other way, such as by its
  // window. Neither should open a second dialog.
  if (code != 0) {
    reply.status = Reply::Cancelled;
    return reply;
  }
  for (const auto &uri : urisOf(results.value("uris"))) {
    const QUrl url(uri);
    if (url.isLocalFile())
      reply.urls << url;
  }
  if (!reply.urls.isEmpty())
    reply.status = Reply::Chosen;
  return reply;
}

namespace {
constexpr auto kService = "org.freedesktop.portal.Desktop";

// Waits for the Response of one request, then removes itself.
class Listener : public QObject {
  Q_OBJECT
public:
  Listener(PortalBus::Done done, QObject *parent)
      : QObject(parent), m_done(std::move(done)) {}
  void fail() { finish(false, 0, {}); }
public slots:
  void onResponse(uint code, const QVariantMap &results) {
    finish(true, code, results);
  }

private:
  void finish(bool delivered, uint code, const QVariantMap &results) {
    if (m_finished)
      return;
    m_finished = true;
    auto done = std::move(m_done);
    deleteLater();
    done(delivered, code, results);
  }
  PortalBus::Done m_done;
  bool m_finished = false;
};

class SessionBus : public PortalBus {
public:
  void call(const Request &request, Done done) override {
    registerPortalTypes();
    auto bus = QDBusConnection::sessionBus();
    auto *listener = new Listener(std::move(done), &m_owner);
    if (!bus.isConnected()) {
      listener->fail();
      return;
    }
    // The portal answers on a request path both sides can compute before the
    // call, so the Response cannot arrive unheard.
    QString sender = bus.baseService().mid(1);
    sender.replace('.', '_');
    const QString path =
        "/org/freedesktop/portal/desktop/request/" + sender + "/" +
        request.options.value("handle_token").toString();
    bus.connect(kService, path, "org.freedesktop.portal.Request", "Response",
                listener, SLOT(onResponse(uint, QVariantMap)));
    auto message = QDBusMessage::createMethodCall(
        kService, "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.FileChooser", request.method);
    message << request.parent << request.title << request.options;
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(message), listener);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, listener,
                     [listener, watcher, bus, path]() mutable {
                       QDBusPendingReply<QDBusObjectPath> reply = *watcher;
                       if (reply.isError())
                         listener->fail();
                       else if (reply.value().path() != path)
                         // An older portal picked its own request path.
                         bus.connect(kService, reply.value().path(),
                                     "org.freedesktop.portal.Request",
                                     "Response", listener,
                                     SLOT(onResponse(uint, QVariantMap)));
                     });
  }

private:
  QObject m_owner;
};
} // namespace

std::unique_ptr<PortalBus> sessionPortalBus() {
  return std::make_unique<SessionBus>();
}

QString portalWindowHandle(QWindow *window) {
#ifdef OMAFRAME_PORTAL_WINDOW
  if (!window || !window->isVisible() || !QGuiApplicationPrivate::platformIntegration())
    return {};
  if (auto *services = dynamic_cast<QDesktopUnixServices *>(
          QGuiApplicationPrivate::platformIntegration()->services()))
    return services->portalWindowIdentifier(window);
#else
  Q_UNUSED(window)
#endif
  return {};
}

} // namespace FilePicks

using namespace FilePicks;

FilePicker::FilePicker(Folder imageFolder, Folder videoFolder,
                       std::unique_ptr<PortalBus> bus, QObject *parent)
    : QObject(parent), m_imageFolder(std::move(imageFolder)),
      m_videoFolder(std::move(videoFolder)),
      m_bus(bus ? std::move(bus) : sessionPortalBus()) {}

QString FilePicker::lastFolder(bool recording) const {
  return QSettings().value(recording ? "openFolder/video" : "openFolder/image").toString();
}

QUrl FilePicker::startFolder(const QString &purpose, bool recording) const {
  return QUrl::fromLocalFile(firstExistingFolder(
      startCandidates(purposeFromName(purpose), recording, lastFolder(false),
                      lastFolder(true), m_imageFolder(), m_videoFolder()),
      QDir::homePath()));
}

// The stock combo box shows "Images (*.png *.jpg ...)", too long to read when
// closed. The dialog selects a filter by the list's text, so the list stays
// and only the closed box shows the short name.
class FilterNames : public QObject {
  Q_OBJECT
public:
  FilterNames(QQuickItem *combo, QStringList labels)
      : QObject(combo), m_combo(combo), m_labels(std::move(labels)) {
    connect(combo, SIGNAL(currentIndexChanged()), this, SLOT(update()));
    update();
  }
public slots:
  void update() {
    const int index = m_combo->property("currentIndex").toInt();
    if (index >= 0 && index < m_labels.size())
      m_combo->setProperty("displayText", m_labels.at(index));
  }

private:
  QQuickItem *m_combo;
  QStringList m_labels;
};
static void shortenFilters(QQuickItem *item, const QStringList &labels) {
  if (item->inherits("QQuickComboBox"))
    new FilterNames(item, labels);
  for (auto *child : item->childItems())
    shortenFilters(child, labels);
}
// A dialog's popup is created when it opens and has no QML handle, so find it
// from its popup item. Its QML types are named FileDialog_QMLTYPE_n and
// FolderDialog_QMLTYPE_n.
static void fitPopups(QQuickItem *item, const QSize &size, const QStringList &labels) {
  QObject *popup = item->parent();
  const QByteArray type = popup ? QByteArray(popup->metaObject()->className()) : QByteArray();
  if (QByteArray(item->metaObject()->className()) == "QQuickPopupItem" &&
      (type.startsWith("FileDialog") || type.startsWith("FolderDialog"))) {
    popup->setProperty("implicitWidth", size.width());
    popup->setProperty("implicitHeight", size.height());
    if (type.startsWith("FileDialog"))
      shortenFilters(item, labels);
  }
  for (auto *child : item->childItems())
    fitPopups(child, size, labels);
}

void FilePicker::fitDialog(QObject *dialog, QWindow *window) const {
  auto *quick = qobject_cast<QQuickWindow *>(window);
  if (!dialog || !quick)
    return;
  const QSize size(std::clamp(window->width() * 7 / 10, 600, 1040),
                   std::clamp(window->height() * 3 / 4, 400, 720));
  QStringList labels;
  for (const auto &filter : mediaFilters())
    labels << filter.label;
  QTimer::singleShot(0, dialog, [=] { fitPopups(quick->contentItem(), size, labels); });
}

void FilePicker::rememberOpened(const QUrl &file) {
  if (!file.isLocalFile())
    return;
  const QString path = file.toLocalFile();
  QSettings().setValue(isRecording(path) ? "openFolder/video" : "openFolder/image",
                       QFileInfo(path).absolutePath());
}

void FilePicker::openMedia(QWindow *parent, bool recording) {
  start(Purpose::Open, parent, recording);
}
void FilePicker::chooseFolder(QWindow *parent, const QString &purpose) {
  start(purposeFromName(purpose), parent, false);
}

void FilePicker::start(Purpose purpose, QWindow *parent, bool recording) {
  if (m_busy)
    return;
  const QString name = purposeName(purpose);
  static const QHash<QString, QString> titles{
      {"open", "Open an image or recording"},
      {"screenshots", "Save screenshots in"},
      {"recordings", "Save recordings and clips in"}};
  const QString folder = startFolder(name, recording).toLocalFile();
  const auto request = buildRequest(purpose, titles.value(name),
                                    portalWindowHandle(parent), folder,
                                    QString("omaframe%1").arg(++m_requests));
  m_busy = true;
  emit busyChanged();
  m_bus->call(request, [this, purpose, name](bool delivered, uint code,
                                             const QVariantMap &results) {
    m_busy = false;
    emit busyChanged();
    const Reply reply = delivered ? parseResponse(code, results) : Reply{};
    if (reply.status == Reply::Cancelled)
      return;
    if (reply.status == Reply::Failed ||
        (isFolder(purpose) && !QFileInfo(reply.urls.first().toLocalFile()).isDir())) {
      emit fallback(name);
      return;
    }
    if (isFolder(purpose))
      emit folderChosen(name, reply.urls.first());
    else {
      rememberOpened(reply.urls.first());
      emit fileChosen(reply.urls.first());
    }
  });
}

#include "file-picker.moc"
