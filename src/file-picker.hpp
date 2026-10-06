#pragma once
#include <QDBusArgument>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <functional>
#include <memory>

class QWindow;

// File and folder choosing. The system's file chooser comes first, through
// the xdg-desktop-portal FileChooser interface; when there is none, QML shows
// its own dialog, started in a sensible folder.
namespace FilePicks {

enum class Purpose { Open, ScreenshotFolder, RecordingFolder };
Purpose purposeFromName(const QString &name);
bool isFolder(Purpose purpose);

/** What Studio::open accepts: images load directly, recordings open the
 * video editor. */
QStringList imageSuffixes();
QStringList recordingSuffixes();
bool isRecording(const QString &path);

// Portal filters are a(sa(us)): a label and glob patterns (type 0).
struct Glob {
  uint type = 0;
  QString pattern;
};
struct Filter {
  QString label;
  QList<Glob> globs;
};
QDBusArgument &operator<<(QDBusArgument &argument, const Glob &glob);
const QDBusArgument &operator>>(const QDBusArgument &argument, Glob &glob);
QDBusArgument &operator<<(QDBusArgument &argument, const Filter &filter);
const QDBusArgument &operator>>(const QDBusArgument &argument, Filter &filter);
void registerPortalTypes();

/** "Images and recordings", "Images", "Recordings". */
QList<Filter> mediaFilters();
/** The same filters as Qt Quick name filters, "Images (*.png *.jpg)". */
QStringList nameFilters();

/** The first candidate that exists as a folder, or home. The default output
 * folder is created on the first save, so it may not exist yet. */
QString firstExistingFolder(const QStringList &candidates,
                            const QString &home);
/** Where a dialog starts, best first: the folder last used for this kind of
 * file, the configured output folder, then the standard folder. */
QStringList startCandidates(Purpose purpose, bool recording,
                            const QString &lastImageFolder,
                            const QString &lastVideoFolder,
                            const QString &imageFolder,
                            const QString &videoFolder);

// One portal call: the method, its parent window and arguments.
struct Request {
  QString method;
  QString parent;
  QString title;
  QVariantMap options;
};
Request buildRequest(Purpose purpose, const QString &title,
                     const QString &parentWindow, const QString &folder,
                     const QString &token);

struct Reply {
  enum Status { Chosen, Cancelled, Failed } status = Failed;
  QList<QUrl> urls;
};
/** Turns a Request.Response signal into a result. */
Reply parseResponse(uint code, const QVariantMap &results);

/** The D-Bus boundary, so tests can stand in for the portal. */
class PortalBus {
public:
  /** delivered is false when the portal could not be reached. */
  using Done =
      std::function<void(bool delivered, uint code, const QVariantMap &results)>;
  virtual ~PortalBus() = default;
  virtual void call(const Request &request, Done done) = 0;
};
std::unique_ptr<PortalBus> sessionPortalBus();

} // namespace FilePicks

Q_DECLARE_METATYPE(FilePicks::Glob)
Q_DECLARE_METATYPE(FilePicks::Filter)

class FilePicker : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  Q_PROPERTY(QStringList nameFilters READ nameFilters CONSTANT)
public:
  using Folder = std::function<QString()>;
  FilePicker(Folder imageFolder, Folder videoFolder,
             std::unique_ptr<FilePicks::PortalBus> bus = nullptr,
             QObject *parent = nullptr);
  bool busy() const { return m_busy; }
  QStringList nameFilters() const { return FilePicks::nameFilters(); }
  /** Asks the system chooser for an image or recording. A recording context
   * starts in the video folder. */
  Q_INVOKABLE void openMedia(QWindow *parent, bool recording = false);
  /** purpose is "screenshots" or "recordings". */
  Q_INVOKABLE void chooseFolder(QWindow *parent, const QString &purpose);
  /** The start folder for QML's own dialog; purpose is "open", "screenshots"
   * or "recordings". */
  Q_INVOKABLE QUrl startFolder(const QString &purpose,
                               bool recording = false) const;
  /** Enlarges an open Qt Quick dialog relative to its window: the stock size
   * is 600 by 400, which truncates the filter name. */
  Q_INVOKABLE void fitDialog(QObject *dialog, QWindow *window) const;
  /** Remembers the folder of a file chosen in QML's own dialog. */
  Q_INVOKABLE void rememberOpened(const QUrl &file);
signals:
  void busyChanged();
  void fileChosen(const QUrl &file);
  void folderChosen(const QString &purpose, const QUrl &folder);
  /** The system chooser is not available; show QML's own dialog. */
  void fallback(const QString &purpose);

private:
  void start(FilePicks::Purpose purpose, QWindow *parent, bool recording);
  QString lastFolder(bool recording) const;
  Folder m_imageFolder, m_videoFolder;
  std::unique_ptr<FilePicks::PortalBus> m_bus;
  bool m_busy = false;
  int m_requests = 0;
};
