#pragma once
#include <QPointF>
#include <QRect>
#include <QSizeF>
#include <QString>

namespace MarkConstraints {
struct Result {
  bool valid = false;
  QPointF anchor,
      point; // Fractions of the displayed canvas, including off-crop anchors.
  QString label;
};
bool shape(const QString &type);
// Raw view fractions become pixels before constraining. Bounds shorten the
// result along its ray; an unreachable crop leaves the preview unchanged.
Result resolve(const QString &type, QPointF anchor, QPointF pointer,
               QSizeF canvas, bool constrain, double aspect = 1.);
QRect videoCropPixels(QSize source, QRectF crop);
} // namespace MarkConstraints
