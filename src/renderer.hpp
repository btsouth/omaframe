#pragma once
#include <QImage>
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
};
struct Options {
  int style = 0;
  double padding = 0.05;
  int aspect = 0;
};
QStringList styleNames();
QRectF cropBounds(const QVector<Edit> &edits);
QRectF annotationBounds(const Edit &edit, const QImage &source);
int textPixelSize(const Edit &edit, const QImage &source);
/** How far a blur mark spreads each pixel, in pixels of an image of `size`. */
int blurRadius(const Edit &edit, QSize size);
double textSizeForPixels(int pixels, const QImage &source);
QImage cropImage(const QImage &image, const QVector<Edit> &edits);
QImage applyEdits(const QImage &source, const QVector<Edit> &edits,
                  bool applyCrop = true);
QImage compose(const QImage &source, const Options &options,
               int maximumEdge = 0);
QImage demoImage(int variant = 0);
QSize outputSize(QSize source, const Options &options);
} // namespace Frame
