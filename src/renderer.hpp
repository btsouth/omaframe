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
};
struct Options {
  int style = 0;
  double padding = 0.09;
  int aspect = 0;
};
QStringList styleNames();
QImage applyEdits(const QImage &source, const QVector<Edit> &edits);
QImage compose(const QImage &source, const Options &options,
               int maximumEdge = 0);
QImage demoImage(int variant = 0);
QSize outputSize(QSize source, const Options &options);
} // namespace Frame
