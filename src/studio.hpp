#pragma once
#include "renderer.hpp"
#include <QMutex>
#include <QObject>
#include <QQuickImageProvider>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class ImageStore final : public QQuickImageProvider {
public:
  ImageStore() : QQuickImageProvider(QQuickImageProvider::Image) {}
  QImage requestImage(const QString &id, QSize *size,
                      const QSize &requested) override;
  void put(const QString &name, const QImage &image);

private:
  QMutex mutex;
  QHash<QString, QImage> images;
};

class Studio final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool recordingSelection READ recordingSelection NOTIFY changed)
  Q_PROPERTY(int revision READ revision NOTIFY changed)
  Q_PROPERTY(bool hasImage READ hasImage NOTIFY changed)
  Q_PROPERTY(int style READ style WRITE setStyle NOTIFY changed)
  Q_PROPERTY(double padding READ padding WRITE setPadding NOTIFY changed)
  Q_PROPERTY(int aspect READ aspect WRITE setAspect NOTIFY changed)
  Q_PROPERTY(QStringList styles READ styles CONSTANT)
  Q_PROPERTY(QStringList monitors READ monitors NOTIFY changed)
  Q_PROPERTY(bool quickMode READ quickMode NOTIFY changed)
  Q_PROPERTY(QString quickState READ quickState NOTIFY changed)
  Q_PROPERTY(QString captureMonitor READ captureMonitor NOTIFY changed)
  Q_PROPERTY(QString name READ name NOTIFY changed)
  Q_PROPERTY(QString dimensions READ dimensions NOTIFY changed)
  Q_PROPERTY(QString outputDimensions READ outputDimensions NOTIFY changed)
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(QString outputDirectory READ outputDirectory NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(bool rendering READ rendering NOTIFY changed)
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
  Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
  Q_PROPERTY(bool demo READ demo NOTIFY changed)
  Q_PROPERTY(QString savedPath READ savedPath NOTIFY changed)
  Q_PROPERTY(QString recoveryAction READ recoveryAction NOTIFY changed)
  Q_PROPERTY(QString originalsSummary READ originalsSummary NOTIFY changed)
  Q_PROPERTY(int originalsCount READ originalsCount NOTIFY changed)
  Q_PROPERTY(QVariantList windowTargets READ windowTargets NOTIFY changed)
  Q_PROPERTY(bool hasLastArea READ hasLastArea NOTIFY changed)
  Q_PROPERTY(bool hasCrop READ hasCrop NOTIFY changed)
  Q_PROPERTY(QRectF cropBounds READ cropBounds NOTIFY changed)
  Q_PROPERTY(QVariantMap selectedAnnotation READ selectedAnnotation NOTIFY changed)
  Q_PROPERTY(QVariantList drafts READ drafts NOTIFY changed)
  Q_PROPERTY(bool keepOriginals READ keepOriginals WRITE setKeepOriginals NOTIFY changed)
  Q_PROPERTY(int newTextPixels READ newTextPixels NOTIFY changed)
  Q_PROPERTY(bool textEditing READ textEditing NOTIFY changed)
  Q_PROPERTY(bool editing READ editing WRITE setEditing NOTIFY changed)
  /** False until the first-run welcome has been seen or dismissed. */
  Q_PROPERTY(bool welcomed READ welcomed WRITE setWelcomed NOTIFY changed)
  Q_PROPERTY(QSize workingSize READ workingSize NOTIFY changed)
  Q_PROPERTY(QString originalsFolder READ originalsFolder CONSTANT)
  /** The display the pointer is on while selecting, for F. */
  Q_PROPERTY(QString pointerMonitor READ pointerMonitor WRITE setPointerMonitor NOTIFY changed)
  /** Notify after a quick screenshot is copied and saved. */
  Q_PROPERTY(bool notifications READ notifications WRITE setNotifications NOTIFY changed)
  Q_PROPERTY(QSize sourceSize READ sourceSize NOTIFY changed)
public:
  explicit Studio(ImageStore *store, bool withDemo = true);
  int revision() const { return m_revision; }
  bool hasImage() const { return !m_original.isNull(); }
  int style() const { return m_options.style; }
  double padding() const { return m_options.padding; }
  int aspect() const { return m_options.aspect; }
  QStringList styles() const { return Frame::styleNames(); }
  QStringList monitors() const;
  QString name() const { return m_name; }
  QString dimensions() const;
  QString outputDimensions() const;
  QString status() const { return m_status; }
  QString outputDirectory() const { return m_directory; }
  bool busy() const { return m_busy; }
  bool rendering() const { return m_rendering; }
  bool canUndo() const { return !m_undoStates.isEmpty(); }
  bool canRedo() const { return !m_redoStates.isEmpty(); }
  bool demo() const { return m_demo; }
  bool quickMode() const { return m_quickMode; }
  QString quickState() const { return m_quickState; }
  QString captureMonitor() const { return m_captureMonitor; }
  QString savedPath() const { return m_savedPath; }
  QString recoveryAction() const;
  QString originalsSummary() const { return m_originalsSummary; }
  int originalsCount() const { return m_originalsCount; }
  QVariantList windowTargets() const { return m_windowTargets; }
  bool hasLastArea() const;
  bool hasCrop() const;
  QRectF cropBounds() const { return Frame::cropBounds(m_edits); }
  QVariantMap selectedAnnotation() const;
  QVariantList drafts() const { return m_drafts; }
  /** Whether each accepted capture also keeps a private, unedited copy. */
  bool keepOriginals() const;
  void setKeepOriginals(bool);
  /** The font size a new label starts at, in source pixels. */
  int newTextPixels() const;
  bool textEditing() const { return m_hiddenEdit >= 0; }
  bool editing() const { return m_editing; }
  bool welcomed() const;
  /** The cropped image the editor shows, and the whole capture, in pixels. */
  QSize workingSize() const { return m_workingSize; }
  QSize sourceSize() const { return m_original.size(); }
  QString originalsFolder() const;
  QString pointerMonitor() const { return m_pointerMonitor; }
  void setPointerMonitor(const QString &name) {
    if (name == m_pointerMonitor)
      return;
    m_pointerMonitor = name;
    emit changed();
  }
  bool notifications() const;
  void setNotifications(bool);
  void setWelcomed(bool);
  /** The editor skips finish thumbnails while it is open. */
  void setEditing(bool);
  /** True once when a capture started from the open studio window, so the
   *  window can come back after that capture ends. */
  bool takeReturnToStudio() {
    const bool value = m_returnToStudio;
    m_returnToStudio = false;
    return value;
  }
  void setStyle(int);
  void setPadding(double);
  void setAspect(int);
  Q_INVOKABLE void open(const QUrl &url);
  Q_INVOKABLE void loadDemo(int variant = 0);
  /** Returns the studio to its start screen. Drafts are kept. */
  Q_INVOKABLE void closeImage();
  Q_INVOKABLE void edit(const QString &type, double x1, double y1, double x2,
                        double y2, const QString &text = {});
  Q_INVOKABLE void addStroke(const QVariantList &points);
  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();
  Q_INVOKABLE void resetEdits();
  Q_INVOKABLE void clearCrop();
  Q_INVOKABLE int selectAt(double x, double y);
  /** The topmost mark at a point, without selecting it: its bounds in view
   *  coordinates, or an empty map. */
  Q_INVOKABLE QVariantMap hitAt(double x, double y, bool edgesOnly = false) const;
  Q_INVOKABLE void select(int index);
  Q_INVOKABLE void clearSelection();
  /** Hides the selected label from the preview while it is typed on the
   *  canvas. endTextEdit() applies or discards the typed text. */
  Q_INVOKABLE void beginTextEdit();
  Q_INVOKABLE void endTextEdit(const QString &text, bool commit);
  Q_INVOKABLE void moveSelected(double dx, double dy);
  Q_INVOKABLE void nudgeSelected(int dx, int dy);
  Q_INVOKABLE void resizeSelected(int handle, double x, double y);
  Q_INVOKABLE void deleteSelected();
  Q_INVOKABLE void duplicateSelected();
  Q_INVOKABLE void moveSelectedLayer(int direction);
  Q_INVOKABLE void updateSelectedText(const QString &text);
  Q_INVOKABLE void setSelectedColor(const QString &color);
  Q_INVOKABLE void setSelectedSize(double size);
  Q_INVOKABLE void setSelectedFontPixels(int pixels);
  Q_INVOKABLE void setSelectedTextStyle(const QString &style);
  Q_INVOKABLE void setSelectedTextAlignment(const QString &alignment);
  Q_INVOKABLE void setSelectedBackground(const QString &color);
  Q_INVOKABLE void setSelectedBackgroundOpacity(double opacity);
  Q_INVOKABLE void accept();
  Q_INVOKABLE void retryOutput();
  Q_INVOKABLE void clearOriginals();
  Q_INVOKABLE void setOutputDirectory(const QUrl &url);
  Q_INVOKABLE void revealSaved();
  Q_INVOKABLE void resumeDraft(const QString &id);
  Q_INVOKABLE void deleteDraft(const QString &id);
  Q_INVOKABLE void saveDraftNow();
  bool recordingSelection() const { return m_recordingSelection; }
  void setRecordingSelection(bool value) {
    m_recordingSelection = value;
    emit changed();
  }
  void leaveQuickMode() {
    m_quickMode = false;
    m_recordingSelection = false;
    m_quickState = "idle";
    emit changed();
  }
  Q_INVOKABLE void useScreenshotSelection() { setRecordingSelection(false); }
  /** Leave region selection for recording setup, which opens on `monitor`:
   *  the display whose capture bar was used. */
  Q_INVOKABLE void recordInstead(const QString &monitor = {});
  /** Leaves the selector for full recording options on `monitor`. */
  Q_INVOKABLE void recordingOptions(const QString &monitor = {});
  /** Opens the selector ready to record. */
  Q_INVOKABLE void captureVideo();
  /** The display quick-mode surfaces (chooser, recording setup) open on. */
  void setCaptureMonitor(const QString &monitor) { m_captureMonitor = monitor; }
  Q_INVOKABLE void capture(bool region, int monitor = 0);
  Q_INVOKABLE void repeatLastArea();
  Q_INVOKABLE void finishSelection(const QString &monitor, double x1, double y1,
                                   double x2, double y2);
  Q_INVOKABLE void cancelSelection();
  Q_INVOKABLE void chooseFinish(int style);
  Q_INVOKABLE void openEditor();
  Q_INVOKABLE void showFinishes();
  Q_INVOKABLE void dismissQuick();
signals:
  void changed();
  void hideStudio();
  void showStudio();
  void selectionReady(const QStringList &monitors);
  void selectionDone();
  void recordRequested();
  void recordRegionSelected(const QString &monitor, const QRectF &normalized);
  void sourceChanged();
  void videoRequested(const QUrl &url);
  void chooserRequested();
  void editorRequested();
  void dismissRequested();
  void captureFailed();
  /** The selector switched to video; recording options should load. */
  void recordModeEntered();
  void recordOptionsRequested();

private:
  void loadImage(QImage image, QString name, bool demo);
  void scheduleRender();
  void persistOptions();
  void refreshDrafts();
  void invalidateSaved();
  void saveHistory();
  QPointF sourcePoint(double x, double y) const;
  void captureImpl(bool region, int monitor, bool repeat);
  ImageStore *m_store;
  QImage m_original;
  QHash<QString, QImage> m_frozen;
  QVariantList m_windowTargets;
  QRectF m_lastArea;
  QSize m_lastAreaPixels;
  QString m_lastAreaMonitor;
  QSize m_workingSize;
  QVector<Frame::Edit> m_edits;
  struct EditState {
    QVector<Frame::Edit> edits;
    int selected = -1;
  };
  QVector<EditState> m_undoStates, m_redoStates;
  QVariantList m_drafts;
  QTimer m_draftTimer;
  QString m_draftId;
  bool m_draftDirty = false;
  int m_selected = -1;
  Frame::Options m_options;
  QString m_name, m_status, m_directory, m_savedPath, m_backupPath,
      m_originalsSummary;
  int m_revision = 0, m_generation = 0;
  int m_originalsCount = 0;
  bool m_busy = false, m_rendering = false, m_demo = true;
  bool m_quickMode = false, m_recordingSelection = false;
  bool m_copyPending = false, m_backupPending = false;
  QString m_quickState = "idle", m_captureMonitor, m_pointerMonitor;
  QPointF viewPoint(const QPointF &source) const;
  /** With `edgesOnly`, filled areas (boxes, highlights, redactions, blur)
   *  are hit only near their border, so a drawing tool can still start a
   *  new mark inside them. */
  int hitIndex(double x, double y, bool edgesOnly = false) const;
  int m_pendingFinish = -1;
  int m_hiddenEdit = -1;
  bool m_editing = false, m_thumbnailsStale = false, m_returnToStudio = false;
};
