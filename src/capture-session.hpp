#pragma once
#include <QHash>
#include <QImage>
#include <QRectF>
#include <QStringList>
#include <functional>

namespace Capture {
using Grab = std::function<bool(const QString &, QImage &, QString &)>;
struct Screens {
  QHash<QString, QImage> images;
  QString error;
};
Screens freeze(const QStringList &names, const Grab &grab);
QImage crop(const QHash<QString, QImage> &images, const QString &name,
            QPointF from, QPointF to);
} // namespace Capture
