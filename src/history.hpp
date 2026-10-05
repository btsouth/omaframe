#pragma once
#include <QAbstractListModel>
#include <QCache>
#include <QImage>
#include <QJsonObject>
#include <QMutex>
#include <QQuickImageProvider>
#include <QThreadPool>
#include <atomic>
#include <memory>

namespace History {
struct Identity {
  quint64 device = 0, inode = 0;
  qint64 size = 0, modified = 0, changed = 0;
  bool valid = false;
  bool operator==(const Identity &) const = default;
};
struct Entry {
  QString path, name, kind, draftId, draftKind, draftPath, sourcePath;
  Identity identity, draftIdentity, sourceIdentity;
  qint64 modified = 0;
  bool incomplete = false, draft = false;
};
Identity identify(const QString &path);
bool unchanged(const Entry &entry);
QString discoveredKind(const QString &name);
QStringList previousFolders();
void rememberFolder(const QString &path);
QVector<Entry> scan(const QStringList &folders, const QString &dataDirectory,
                    const std::shared_ptr<std::atomic_bool> &cancel);
QImage draftPreview(const Entry &entry);
QString thumbnailKey(const Entry &entry);
void trimCache(const QString &directory, qint64 byteCap);
QImage thumbnail(const Entry &entry, const QString &cacheDirectory,
                 const std::shared_ptr<std::atomic_bool> &cancel);
}

class HistoryImages final : public QQuickImageProvider {
public:
  HistoryImages() : QQuickImageProvider(Image), images(8 * 1024 * 1024) {}
  QImage requestImage(const QString &id, QSize *size, const QSize &) override;
  void put(const QString &key, const QImage &image);
private:
  QMutex mutex;
  QCache<QString, QImage> images;
};

class CaptureHistoryModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY changed)
  Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(QStringList previousFolders READ previousFolders NOTIFY changed)
public:
  enum Role { Name = Qt::UserRole + 1, Kind, Day, When, Detail, Incomplete,
              Draft, HasDraft, Thumbnail, FilePath };
  explicit CaptureHistoryModel(HistoryImages *images, QObject *parent = nullptr);
  ~CaptureHistoryModel() override;
  int rowCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  QString filter() const { return m_filter; }
  QString search() const { return m_search; }
  QString status() const { return m_status; }
  bool busy() const { return m_busy; }
  QStringList previousFolders() const { return History::previousFolders(); }
  void setFilter(const QString &value);
  void setSearch(const QString &value);
  Q_INVOKABLE void refresh();
  Q_INVOKABLE void removeFolder(const QString &path);
  Q_INVOKABLE void setVisibleRange(int first, int last);
  Q_INVOKABLE bool action(int row, const QString &action, const QString &expectedKey = {});
  Q_INVOKABLE QString keyAt(int row) const;
  Q_INVOKABLE QString pathAt(int row) const;
  Q_INVOKABLE bool isDraftAt(int row) const;
  Q_INVOKABLE bool hasDraftAt(int row) const;
  Q_INVOKABLE QString nameAt(int row) const;
signals:
  void changed();
  void navigate(const QString &command, const QUrl &path);
  void deleteDraft(const QString &kind, const QString &id);
private:
  void rebuild();
  void cancelThumbnails();
  HistoryImages *m_images;
  QVector<History::Entry> m_all, m_rows;
  QHash<QString, QString> m_thumbnails;
  QString m_filter = "All", m_search, m_status;
  bool m_busy = false;
  int m_generation = 0;
  QThreadPool m_pool;
  std::shared_ptr<std::atomic_bool> m_scanCancel;
  QHash<QString, std::shared_ptr<std::atomic_bool>> m_requests;
};
