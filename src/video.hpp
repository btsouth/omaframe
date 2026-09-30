#pragma once
#include "marks.hpp"
#include <QHash>
#include <QImage>
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <memory>

class ImageStore;

/** Marks that hide what is under them. The rest (arrows, boxes, labels and
 *  steps) are pictures laid over the video. */
bool hidesVideo(const Frame::Edit &edit);
bool pointsInVideo(const Frame::Edit &edit);
/** A picture of one pointing mark laid over the video: which FFmpeg input it
 *  is, where it goes in the frame, and when it shows. */
struct VideoOverlay {
  int input = 0;
  QRect area;
  double start = 0, end = -1;
};
/** Mark `index` drawn alone on a clear frame of `size`, cut down to the
 *  pixels it covers. `area` gets where they go. A step keeps its number in
 *  the clip. Null when there is nothing to draw. */
QImage renderVideoMark(const QVector<Frame::Edit> &edits, int index, QSize size,
                       QRect *area);
/** FFmpeg filters that burn marks into video frames of `size`, reading the
 *  stream `input` and writing `output`: blur and redaction first, then the
 *  `overlays`, so an arrow over a blurred area stays sharp. `offset` is
 *  subtracted from the marks' times, for input that starts that many seconds
 *  into the source. Empty when no mark shows in the input. */
QStringList videoMarkFilters(const QVector<Frame::Edit> &edits, QSize size,
                             double offset, const QString &input,
                             const QString &output,
                             const QVector<VideoOverlay> &overlays = {});

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
  /** The saved clip's file name and a short "9.0 s · 8.1 MB" summary. */
  Q_PROPERTY(QString savedName READ savedName NOTIFY changed)
  Q_PROPERTY(QString savedSummary READ savedSummary NOTIFY changed)
  Q_PROPERTY(QString gifPath READ gifPath NOTIFY changed)
  Q_PROPERTY(QString gifSummary READ gifSummary NOTIFY changed)
  /** Blur and redaction marks over the video, saved into the exported clip. */
  Q_PROPERTY(MarkDocument *marks READ marks CONSTANT)
  /** The video's frame size in pixels. */
  Q_PROPERTY(QSize frameSize READ frameSize NOTIFY changed)
  /** The pointing marks as pictures for the preview: index, start, end, the
   *  area x, y, w, h as fractions of the frame, and an image source. */
  Q_PROPERTY(QVariantList overlays READ overlays NOTIFY overlaysChanged)
  Q_PROPERTY(QRectF cropBounds READ cropBounds NOTIFY changed)
  Q_PROPERTY(QSize outputSize READ outputSize NOTIFY changed)
  Q_PROPERTY(QVariantMap editState READ editState NOTIFY changed)
  Q_PROPERTY(QVariantList drafts READ drafts NOTIFY draftsChanged)
  Q_PROPERTY(QString draftSignature READ draftSignature NOTIFY changed)
  Q_PROPERTY(QString savedSignature READ savedSignature NOTIFY changed)
  Q_PROPERTY(QUrl cameraSource READ cameraSource NOTIFY changed)
  Q_PROPERTY(QVariantMap cameraLayout READ cameraLayout NOTIFY changed)
  Q_PROPERTY(QRectF cameraBounds READ cameraBounds NOTIFY changed)
  Q_PROPERTY(double cameraDuration READ cameraDuration NOTIFY changed)
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
  QString savedName() const;
  QString savedSummary() const { return m_savedSummary; }
  QString gifPath() const { return m_gifSaved; }
  QString gifSummary() const { return m_gifSummary; }
  MarkDocument *marks() { return &m_marks; }
  QSize frameSize() const { return m_frameSize; }
  QVariantList overlays() const { return m_overlays; }
  QRect cropPixels() const;
  QRectF cropBounds() const;
  QSize outputSize() const { return cropPixels().size(); }
  QVariantMap editState() const { return m_editState; }
  QVariantList drafts() const { return m_drafts; }
  QString draftSignature() const { return m_draftSignature; }
  QString savedSignature() const { return m_savedSignature; }
  QUrl cameraSource() const { return m_cameraSource; }
  QVariantMap cameraLayout() const { return m_cameraLayout; }
  QRectF cameraBounds() const;
  double cameraDuration() const { return m_cameraDuration; }
  Q_INVOKABLE void setEditState(const QVariantMap &state);
  Q_INVOKABLE bool saveDraftNow();
  Q_INVOKABLE void resumeDraft(const QString &id);
  Q_INVOKABLE void deleteDraft(const QString &id);
  Q_INVOKABLE void recordSavedSignature(const QString &signature);
  Q_INVOKABLE void setCameraLayout(const QVariantMap &layout);
  /** Where the preview pictures of the marks are kept for QML. */
  void setImageStore(ImageStore *store) { m_store = store; }
  Q_INVOKABLE void open(const QUrl &url);
  Q_INVOKABLE void exportClip(double start, double end, bool mute);
  Q_INVOKABLE void exportEdited(double start, double end, bool mute,
                               const QVariantList &removedRanges);
  Q_INVOKABLE void exportGif(double start, double end,
                             const QVariantList &removedRanges);
  Q_INVOKABLE bool copyGif();
  Q_INVOKABLE void revealGif();
  Q_INVOKABLE void keepOriginal();
  /** Ends recording review; the recording and any edit are already saved. */
  Q_INVOKABLE void finish();
  /** Puts the saved clip, or the open recording when nothing was saved, on
   *  the clipboard as a file for pasting into chats and file managers.
   *  `original` selects the source while preserving an earlier export. */
  Q_INVOKABLE bool copyFile(bool original = false);
  Q_INVOKABLE void cancel();
  Q_INVOKABLE void revealSaved();
  Q_INVOKABLE void revealSource();
  Q_INVOKABLE void setOutputDirectory(const QUrl &url);
signals:
  void changed();
  void loaded();
  void opening();
  void exported(const QUrl &file);
  void gifExported(const QUrl &file);
  void exportFailed();
  void originalAccepted(const QUrl &file);
  void overlaysChanged();
  void opened();
  void draftsChanged();

private:
  void exportEditedAs(double start, double end, bool mute,
                      const QVariantList &removedRanges, bool gif);
  bool copyPath(const QString &path);
  void makeThumbnails();
  void renderOverlays();
  /** Writes each pointing mark as a PNG for FFmpeg, adding an input to
   *  `inputs` and its place to `overlays` for each. False when one could not
   *  be written. */
  bool writeOverlays(QStringList &inputs, QVector<VideoOverlay> &overlays);
  void refreshDrafts();
  void restorePendingDraft();
  void readCameraTrack();
  bool validEditState(const QVariantMap &state) const;
  QTimer m_draftTimer;
  QVariantMap m_editState, m_pendingDraft;
  QVariantMap m_cameraLayout{
      {"visible", false}, {"width", 0.24}, {"x", 0.74}, {"y", 0.72}};
  QVariantList m_drafts;
  QString m_draftId, m_draftSignature, m_savedSignature;
  bool m_draftDeleted = false;
  qint64 m_sourceSize = 0, m_sourceModified = 0;
  QUrl m_cameraSource;
  double m_cameraDuration = 0;
  QUrl m_source;
  MarkDocument m_marks;
  QSize m_frameSize;
  ImageStore *m_store = nullptr;
  QVariantList m_overlays;
  // Each mark's picture, keyed by everything that changes how it looks, so
  // moving one mark redraws only that one.
  struct DrawnMark {
    QImage image;
    QRect area;
  };
  QHash<QByteArray, DrawnMark> m_drawnMarks;
  std::unique_ptr<QTemporaryDir> m_overlayDir;
  QStringList m_thumbnails;
  std::unique_ptr<QTemporaryDir> m_thumbnailDir;
  int m_generation = 0;
  QString m_name, m_dimensions, m_status = "Open a recording to get started.",
                                m_directory, m_saved, m_temporary, m_final,
                                m_error;
  double m_duration = 0, m_progress = 0, m_exportDuration = 0;
  bool m_busy = false, m_cancelled = false, m_exporting = false;
  bool m_copyCompatible = false;
  bool m_muteExport = false;
  bool m_gifExport = false;
  QString m_savedSummary;
  QString m_gifSaved, m_gifSummary;
  int m_audioTracks = 0;
  QProcess m_encoder;
  QByteArray m_progressBuffer;
};
