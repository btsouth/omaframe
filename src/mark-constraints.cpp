#include "mark-constraints.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace MarkConstraints {
bool shape(const QString &type) {
  return type == "box" || type == "ellipse" || type == "highlight" ||
         type == "redact" || type == "blur";
}
Result resolve(const QString &type, QPointF anchor, QPointF pointer,
               QSizeF canvas, bool constrain, double aspect) {
  Result result;
  for (double n : {anchor.x(), anchor.y(), pointer.x(), pointer.y(),
                   canvas.width(), canvas.height(), aspect})
    if (!std::isfinite(n)) return result;
  if (canvas.width() <= 0 || canvas.height() <= 0 || aspect <= 0)
    return result;
  result.anchor = anchor;
  if (!constrain || (type != "arrow" && type != "line" && !shape(type))) {
    result.valid = true;
    result.point = {std::clamp(pointer.x(), 0., 1.), std::clamp(pointer.y(), 0., 1.)};
    return result;
  }
  const QPointF a(anchor.x() * canvas.width(), anchor.y() * canvas.height());
  QPointF v((pointer.x() - anchor.x()) * canvas.width(),
            (pointer.y() - anchor.y()) * canvas.height());
  if (type == "arrow" || type == "line") {
    const double length = std::hypot(v.x(), v.y());
    if (!std::isfinite(length)) return result;
    if (length > 0) {
      constexpr double sector = std::numbers::pi / 4;
      const double angle = std::atan2(v.y(), v.x());
      // Increasing screen angle is clockwise, including exact half-sector ties.
      const int index = int(std::floor(angle / sector + .5 + 1e-12));
      v = {length * std::cos(index * sector), length * std::sin(index * sector)};
      result.label = QString::number((index % 8 + 8) % 8 * 45) + QChar(0x00b0);
    }
  } else {
    const double h = std::max(std::abs(v.x()) / aspect, std::abs(v.y()));
    v = {std::copysign(h * aspect, v.x()), std::copysign(h, v.y())};
    result.label = qFuzzyCompare(aspect, 1.) ? "1:1" : QString::number(aspect, 'g', 3) + ":1";
  }
  if (!std::isfinite(v.x()) || !std::isfinite(v.y())) return result;
  // Intersect the feasible interval of a + t*v, with 0 <= t <= 1.
  double low = 0, high = 1;
  auto bound = [&](double origin, double delta, double extent) {
    if (std::abs(delta) < 1e-10) return origin >= 0 && origin <= extent;
    double first = -origin / delta, last = (extent - origin) / delta;
    if (first > last) std::swap(first, last);
    low = std::max(low, first); high = std::min(high, last);
    return low <= high;
  };
  if (!bound(a.x(), v.x(), canvas.width()) ||
      !bound(a.y(), v.y(), canvas.height())) return result;
  const QPointF p = a + v * high;
  result.point = {p.x() / canvas.width(), p.y() / canvas.height()};
  result.valid = true;
  if (v.isNull()) result.label.clear();
  return result;
}
QRect videoCropPixels(QSize source, QRectF crop) {
  if (source.isEmpty()) return {};
  if (source.width() < 2 || source.height() < 2) return {QPoint(0, 0), source};
  const int left = std::clamp(int(crop.x() * source.width()) / 2 * 2, 0, source.width() - 2);
  const int top = std::clamp(int(crop.y() * source.height()) / 2 * 2, 0, source.height() - 2);
  const int right = std::clamp(int(crop.right() * source.width()) / 2 * 2, left + 2, source.width());
  const int bottom = std::clamp(int(crop.bottom() * source.height()) / 2 * 2, top + 2, source.height());
  return {left, top, right - left, bottom - top};
}
} // namespace MarkConstraints
