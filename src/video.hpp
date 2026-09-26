#pragma once
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QUrl>
#include <memory>

class Video : public QObject {
  Q_OBJECT
  Q_PROPERTY(QUrl source READ source NOTIFY changed)
  Q_PROPERTY(QString name READ name NOTIFY changed)
  Q_PROPERTY(QString dimensions READ dimensions NOTIFY changed)
  Q_PROPERTY(double duration READ duration NOTIFY changed)
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(QString outputDirectory READ outputDirectory NOTIFY changed)
  Q_PROPERTY(QString savedPath READ savedPath NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(bool exporting READ exporting NOTIFY changed)
  Q_PROPERTY(double progress READ progress NOTIFY changed)
  Q_PROPERTY(int audioTracks READ audioTracks NOTIFY changed)
  /** Evenly spaced frames for the timeline, as file URLs; empty until ready. */
  Q_PROPERTY(QStringList thumbnails READ thumbnails NOTIFY changed)
public:
  static constexpr int ThumbnailCount = 16;
  explicit Video(QObject *parent = nullptr);
  ~Video() override;
  QUrl source() const { return m_source; }
  QString name() const { return m_name; }
  QString dimensions() const { return m_dimensions; }
  double duration() const { return m_duration; }
  QString status() const { return m_status; }
  QString outputDirectory() const { return m_directory; }
  QString savedPath() const { return m_saved; }
  bool busy() const { return m_busy; }
  bool exporting() const { return m_busy && m_exporting; }
  double progress() const { return m_progress; }
  int audioTracks() const { return m_audioTracks; }
  QStringList thumbnails() const { return m_thumbnails; }
  Q_INVOKABLE void open(const QUrl &url);
  Q_INVOKABLE void exportClip(double start, double end, bool mute);
  Q_INVOKABLE void cancel();
  Q_INVOKABLE void revealSaved();
  Q_INVOKABLE void setOutputDirectory(const QUrl &url);
signals:
  void changed();
  void loaded();
  void opening();

private:
  void makeThumbnails();
  QUrl m_source;
  QStringList m_thumbnails;
  std::unique_ptr<QTemporaryDir> m_thumbnailDir;
  int m_generation = 0;
  QString m_name, m_dimensions, m_status = "Open a recording to get started.",
                                m_directory, m_saved, m_temporary, m_final,
                                m_error;
  double m_duration = 0, m_progress = 0, m_exportDuration = 0;
  bool m_busy = false, m_cancelled = false, m_exporting = false;
  int m_audioTracks = 0;
  QProcess m_encoder;
  QByteArray m_progressBuffer;
};
