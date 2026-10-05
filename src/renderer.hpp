#pragma once
#include <QImage>
#include <QMargins>
#include <QRectF>
#include <QString>
#include <QVector>

namespace Frame {
struct Edit {
  QString type;
  QPointF from;
  QPointF to;
  QString text;
  QColor color = QColor("#e75439");
  double size = 1.0;
  QString textStyle = "box";
  QString textAlign = "center";
  QColor background = QColor("#151a20");
  double backgroundOpacity = 1.0;
  QVector<QPointF> points;
  /** Seconds on the source video while the mark shows. Screenshots ignore
   *  them. A negative end means until the end of the clip. */
  double start = 0;
  double end = -1;
  /** The number a step shows. 0 counts the remaining steps by creation order. */
  int number = 0;
  /** Optional text box dimensions, as fractions of the source. Zero sizes to content. */
  QSizeF textBox = {0, 0};
  bool outline = false;
  QString arrowHead = "open";
  bool filled = false;
  double opacity = 95. / 255.;
  QColor numberColor = Qt::white;
  /** Stable creation order. Older drafts use their array order. */
  int stepOrder = 0;
  bool operator==(const Edit &) const = default;
};
struct Options {
  int style = 0;
  double padding = 0.05;
  int aspect = 0;
};
QStringList styleNames();
QRectF cropBounds(const QVector<Edit> &edits);
/** The source pixels actually displayed, including outward rounding. */
QRect cropPixels(QSize size, const QVector<Edit> &edits);
QRectF annotationBounds(const Edit &edit, const QImage &source);
int textPixelSize(const Edit &edit, const QImage &source);
/** How far a blur mark spreads each pixel, in pixels of an image of `size`. */
int blurRadius(const Edit &edit, QSize size);
double textSizeForPixels(int pixels, const QImage &source);
QImage cropImage(const QImage &image, const QVector<Edit> &edits);
QImage applyEdits(const QImage &source, const QVector<Edit> &edits,
                  bool applyCrop = true, QString *error = nullptr);
/** Room to add past each edge of a capture whose edge is one flat color
 *  and which ends close to its content. Zero on edges that are not flat or
 *  already have room. */
QMargins edgeRoom(const QImage &source);
/** The framed image. Flat edges get their edgeRoom first, except on Raw.
 *  Size caps apply after framing and before allocating the result. */
QImage compose(const QImage &source, const Options &options,
               int maximumEdge = 0, qint64 maximumPixels = 0);
QImage demoImage(int variant = 0);
/** The size compose() gives an image of `source` pixels with `room` from
 *  edgeRoom(). */
QSize outputSize(QSize source, const Options &options, QMargins room = {});
} // namespace Frame
