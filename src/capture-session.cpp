#include "capture-session.hpp"
#include <algorithm>

Capture::Screens Capture::freeze(const QStringList &names, const Grab &grab) {
  Screens result;
  for (const auto &name : names) {
    QImage image;
    QString error;
    if (!grab(name, image, error) || image.isNull()) {
      result.images.clear();
      result.error = QString("%1: %2").arg(name, error);
      return result;
    }
    image.setDevicePixelRatio(1);
    result.images.insert(name, image);
  }
  if (result.images.isEmpty())
    result.error = "No displays are available.";
  return result;
}

QImage Capture::crop(const QHash<QString, QImage> &images, const QString &name,
                     QPointF from, QPointF to) {
  const QImage image = images.value(name);
  if (image.isNull())
    return {};
  const auto point = [&](QPointF p) {
    return QPointF(std::clamp(p.x(), 0., 1.) * image.width(),
                   std::clamp(p.y(), 0., 1.) * image.height());
  };
  const QRect rect = QRectF(point(from), point(to))
                         .normalized()
                         .toAlignedRect()
                         .intersected(image.rect());
  return rect.width() < 4 || rect.height() < 4 ? QImage() : image.copy(rect);
}
