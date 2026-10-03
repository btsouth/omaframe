#pragma once
#include "renderer.hpp"
#include <QImage>
#include <QObject>
#include <QVariantMap>
#include <functional>
#include <optional>

/** The marks on one image: the edit list, selection, undo and redo, and the
 *  label being typed. Screenshots and video frames share it. It never
 *  renders; it reports edits and the owner re-renders. */
class MarkDocument final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
  Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
  Q_PROPERTY(bool hasCrop READ hasCrop NOTIFY changed)
  Q_PROPERTY(QRectF cropBounds READ cropBounds NOTIFY changed)
  Q_PROPERTY(QVariantMap selectedAnnotation READ selectedAnnotation NOTIFY changed)
  Q_PROPERTY(int newTextPixels READ newTextPixels NOTIFY changed)
  Q_PROPERTY(QVariantMap labelDefaults READ labelDefaults NOTIFY changed)
  Q_PROPERTY(QVariantMap labelStyle READ labelStyle NOTIFY changed)
  Q_PROPERTY(QVariantMap toolDefaults READ toolDefaults NOTIFY changed)
  Q_PROPERTY(bool textEditing READ textEditing NOTIFY changed)
  /** Every mark, for drawing them over a video: index, type, x1, y1, x2, y2,
   *  start, end, text, color and size. */
  Q_PROPERTY(QVariantList annotations READ annotations NOTIFY changed)
  /** The length of the video in seconds, or 0 for a screenshot. Marks on a
   *  video show only between their start and end. */
  Q_PROPERTY(double duration READ duration WRITE setDuration NOTIFY changed)
  /** Where the video is paused. New marks start here, and only marks showing
   *  here can be picked up. */
  Q_PROPERTY(double playhead READ playhead WRITE setPlayhead NOTIFY playheadChanged)
public:
  static constexpr int MaxEdits = 100;
  explicit MarkDocument(QObject *parent = nullptr) : QObject(parent) {}
  /** Edits are refused while this returns true, e.g. during a save. */
  void setLockCheck(std::function<bool()> locked) { m_locked = std::move(locked); }
  /** Starts over on a new image with no marks and no history. */
  void reset(const QImage &base);
  /** Reopens saved marks on their image, with no history. */
  void restore(const QImage &base, QVector<Frame::Edit> edits, int selected);
  const QVector<Frame::Edit> &edits() const { return m_edits; }
  /** The edits to preview: all of them except a label being typed. */
  QVector<Frame::Edit> visibleEdits() const;
  int selected() const { return m_selected; }
  bool transforming() const { return m_transform.has_value(); }
  /** The label being typed, which the preview leaves out, or -1. */
  int hiddenIndex() const { return m_hiddenEdit; }

  bool canUndo() const { return !m_undoStates.isEmpty(); }
  bool canRedo() const { return !m_redoStates.isEmpty(); }
  bool hasCrop() const;
  QRectF cropBounds() const;
  QVariantMap selectedAnnotation() const;
  /** The font size a new label starts at, in source pixels. */
  int newTextPixels() const;
  QVariantMap labelDefaults() const;
  QVariantMap labelStyle() const;
  Q_INVOKABLE void refreshLabelStyle() { emit changed(); }
  /** Apply only supplied fields, in one undo step, and remember them for new labels. */
  Q_INVOKABLE void setLabelStyle(const QVariantMap &style);
  Q_INVOKABLE void resetLabelStyle();
  QVariantMap toolDefaults() const;
  Q_INVOKABLE void setToolStyle(const QString &type, const QVariantMap &style);
  Q_INVOKABLE void resetToolStyle(const QString &type);
  Q_INVOKABLE QString stylePreview(const QString &type, const QVariantMap &style) const;
  bool textEditing() const { return m_hiddenEdit >= 0; }
  QVariantList annotations() const;
  double duration() const { return m_duration; }
  void setDuration(double seconds);
  double playhead() const { return m_playhead; }
  void setPlayhead(double seconds);

  Q_INVOKABLE void edit(const QString &type, double x1, double y1, double x2,
                        double y2, const QString &text = {});
  /** Crop the currently visible image, preserving earlier crops in undo history. */
  Q_INVOKABLE bool cropCurrentView(double x1, double y1, double x2, double y2);
  Q_INVOKABLE void addStroke(const QVariantList &points);
  /** Redacts each area, given as fractions of the whole image, in one step
   *  that a single undo takes back. */
  void redactAreas(const QVector<QRectF> &areas);
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
  Q_INVOKABLE void endTextEdit(const QString &text, bool apply);
  /** Preview from the drag's original geometry; committing adds one undo step. */
  Q_INVOKABLE void beginTransform();
  Q_INVOKABLE void previewTransform(int handle, double x, double y);
  Q_INVOKABLE void endTransform(bool apply);
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
  /** When the selected mark shows on a video, in seconds. */
  Q_INVOKABLE void setSelectedTimes(double start, double end);
signals:
  void changed();
  void playheadChanged();
  /** The preview needs rendering again. `modified` is false when only the
   *  label being typed was hidden or shown, with no change to the marks. */
  void edited(bool modified);
  /** A short note for the status line. */
  void message(const QString &text);

private:
  QVariantMap defaultToolStyle(const QString &type) const;
  void applyToolDefaults(Frame::Edit &edit) const;
  bool locked() const { return m_locked && m_locked(); }
  void saveHistory();
  void commit(bool modified = true);
  QPointF sourcePoint(double x, double y) const;
  /** Gives a new mark its times on a video. */
  void timeNewMark(Frame::Edit &edit) const;
  bool showing(const Frame::Edit &edit) const;
  /** With `edgesOnly`, filled areas (boxes, highlights, redactions, blur)
   *  are hit only near their border, so a drawing tool can still start a
   *  new mark inside them. */
  int hitIndex(double x, double y, bool edgesOnly = false) const;
  std::function<bool()> m_locked;
  QImage m_base;
  QVector<Frame::Edit> m_edits;
  struct EditState {
    QVector<Frame::Edit> edits;
    int selected = -1;
  };
  QVector<EditState> m_undoStates, m_redoStates;
  std::optional<EditState> m_transform;
  bool m_previewing = false;
  int m_selected = -1;
  int m_hiddenEdit = -1;
  double m_duration = 0, m_playhead = 0;
};
