#pragma once
#include "renderer.hpp"
#include <QMutex>
#include <QObject>
#include <QQuickImageProvider>
#include <QUrl>
#include <QVariantList>

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
  bool canUndo() const { return !m_edits.isEmpty(); }
  bool canRedo() const { return !m_redo.isEmpty(); }
  bool demo() const { return m_demo; }
  bool quickMode() const { return m_quickMode; }
  QString quickState() const { return m_quickState; }
  QString captureMonitor() const { return m_captureMonitor; }
  QString savedPath() const { return m_savedPath; }
  void setStyle(int);
  void setPadding(double);
  void setAspect(int);
  Q_INVOKABLE void open(const QUrl &url);
  Q_INVOKABLE void loadDemo(int variant = 0);
  Q_INVOKABLE void edit(const QString &type, double x1, double y1, double x2,
                        double y2, const QString &text = {});
  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();
  Q_INVOKABLE void resetEdits();
  Q_INVOKABLE void accept();
  Q_INVOKABLE void setOutputDirectory(const QUrl &url);
  Q_INVOKABLE void revealSaved();
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
  /** The display quick-mode surfaces (chooser, recording setup) open on. */
  void setCaptureMonitor(const QString &monitor) { m_captureMonitor = monitor; }
  Q_INVOKABLE void capture(bool region, int monitor = 0);
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

private:
  void loadImage(QImage image, QString name, bool demo);
  void scheduleRender();
  void persistOptions();
  ImageStore *m_store;
  QImage m_original;
  QHash<QString, QImage> m_frozen;
  QSize m_workingSize;
  QVector<Frame::Edit> m_edits, m_redo;
  Frame::Options m_options;
  QString m_name, m_status, m_directory, m_savedPath;
  int m_revision = 0, m_generation = 0;
  bool m_busy = false, m_rendering = false, m_demo = true;
  bool m_quickMode = false, m_recordingSelection = false;
  QString m_quickState = "idle", m_captureMonitor;
  int m_pendingFinish = -1;
};
