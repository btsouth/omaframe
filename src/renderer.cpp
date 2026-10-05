#include "renderer.hpp"
#include <QFont>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <algorithm>
#include <array>
#include <cmath>

namespace Frame {
QStringList styleNames() {
  return {"Paper",   "Slate",  "Deep",    "Aurora", "Adaptive",
          "Outline", "Studio", "Ambient", "Raw"};
}

static QRect pixelRect(QSize size, QPointF from, QPointF to) {
  // Normalized integer edges can land a few ulps either side of a pixel
  // after serialization. Do not enlarge those crops by another pixel.
  const auto pixel = [](double value) {
    const double rounded = std::round(value);
    return std::abs(value - rounded) < 1e-7 ? rounded : value;
  };
  QRectF r(QPointF(pixel(from.x() * size.width()), pixel(from.y() * size.height())),
           QPointF(pixel(to.x() * size.width()), pixel(to.y() * size.height())));
  return r.normalized().toAlignedRect().intersected(QRect(QPoint(), size));
}

QRectF cropBounds(const QVector<Edit> &edits) {
  QRectF crop(0, 0, 1, 1);
  for (const Edit &edit : edits)
    if (edit.type == "crop") {
      const QRectF candidate(edit.from, edit.to);
      const QRectF clipped = candidate.normalized().intersected(QRectF(0, 0, 1, 1));
      if (clipped.width() > 0 && clipped.height() > 0)
        crop = clipped;
    }
  return crop;
}

static double annotationUnit(QSize size) {
  return std::max(2., std::min(size.width(), size.height()) / 240.);
}
static double annotationUnit(const QImage &source) {
  return annotationUnit(source.size());
}
int blurRadius(const Edit &edit, QSize size) {
  return std::clamp(qRound(annotationUnit(size) * 4 * edit.size), 2, 48);
}

double textSizeForPixels(int pixels, const QImage &source) {
  return std::clamp(pixels, 8, 4096) / (annotationUnit(source) * 6.);
}

int textPixelSize(const Edit &edit, const QImage &source) {
  return std::clamp(qRound(annotationUnit(source) * 6. * edit.size), 8, 4096);
}

struct TextLayout {
  QString visibleText;
  QSizeF size;
};

static TextLayout layoutText(const Edit &edit, const QImage &source,
                             const QFont &font) {
  const QFontMetrics metrics(font, &source);
  const double inset = std::max(4., textPixelSize(edit, source) * 0.27);
  const int lineLimit = std::max(1, qFloor((edit.textBox.width() > 0 ? edit.textBox.width() * source.width()
                                                     : source.width() * 0.85) - inset * 2));
  QStringList lines;
  for (const QString &paragraph : edit.text.split('\n')) {
    QString line;
    for (const QString &word : paragraph.split(' ')) {
      const QString candidate = line.isEmpty() ? word : line + ' ' + word;
      if (metrics.horizontalAdvance(candidate) <= lineLimit) {
        line = candidate;
        continue;
      }
      if (!line.isEmpty()) {
        lines.append(line);
        line.clear();
      }
      for (const QChar ch : word) {
        const QString next = line + ch;
        if (!line.isEmpty() && metrics.horizontalAdvance(next) > lineLimit) {
          lines.append(line);
          line.clear();
        }
        line += ch;
      }
    }
    lines.append(line);
  }
  int width = 1;
  for (const QString &line : lines)
    width = std::max(width, metrics.horizontalAdvance(line));
  return {lines.join('\n'),
          QSizeF(std::max(width + inset * 2, edit.textBox.width() * source.width()),
                 std::max(metrics.lineSpacing() * std::max(1, int(lines.size())) + inset * 2,
                          edit.textBox.height() * source.height()))};
}

QRectF annotationBounds(const Edit &edit, const QImage &source) {
  if (source.isNull())
    return {};
  const double unit = annotationUnit(source);
  const double markSize = std::clamp(edit.size, 0.5, 8.0);
  const QPointF anchor(edit.from.x() * source.width(),
                       edit.from.y() * source.height());
  QRectF pixels;
  if (edit.type == "text") {
    QFont font("sans-serif");
    font.setPixelSize(textPixelSize(edit, source));
    font.setWeight(QFont::DemiBold);
    pixels = QRectF(anchor, layoutText(edit, source, font).size);
  } else if (edit.type == "step") {
    pixels = QRectF(anchor - QPointF(unit * 5 * markSize, unit * 5 * markSize),
                    QSizeF(unit * 10 * markSize, unit * 10 * markSize));
  } else if (edit.type == "pen" && !edit.points.isEmpty()) {
    double left = 1., right = 0., top = 1., bottom = 0.;
    for (const QPointF &point : edit.points) {
      left = std::min(left, point.x());
      right = std::max(right, point.x());
      top = std::min(top, point.y());
      bottom = std::max(bottom, point.y());
    }
    return QRectF(QPointF(left, top), QPointF(right, bottom)).normalized();
  } else {
    pixels = QRectF(anchor,
                    QPointF(edit.to.x() * source.width(),
                            edit.to.y() * source.height()))
                 .normalized();
  }
  return {pixels.x() / source.width(), pixels.y() / source.height(),
          pixels.width() / source.width(),
          pixels.height() / source.height()};
}

QRect cropPixels(QSize size, const QVector<Edit> &edits) {
  const QRect full(QPoint(), size);
  QRect region = full;
  for (const Edit &edit : edits)
    if (edit.type == "crop")
      region = pixelRect(size, edit.from, edit.to);
  return region.width() >= 2 && region.height() >= 2 ? region : full;
}

QImage cropImage(const QImage &image, const QVector<Edit> &edits) {
  if (image.isNull())
    return image;
  const QRect region = cropPixels(image.size(), edits);
  return region == image.rect() ? image : image.copy(region);
}

QImage applyEdits(const QImage &source, const QVector<Edit> &edits,
                  bool applyCrop, QString *error) {
  if (error) error->clear();
  QImage img = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  if (img.isNull() || !img.bits()) {
    if (error) *error = "Could not allocate the edited image. Try again with a smaller image.";
    return {};
  }
  img.setDevicePixelRatio(1);
  QVector<const Edit *> steps;
  for (const Edit &edit : edits)
    if (edit.type == "step") steps.append(&edit);
  std::stable_sort(steps.begin(), steps.end(), [](const Edit *a, const Edit *b) {
    return a->stepOrder < b->stepOrder;
  });
  for (const Edit &edit : edits) {
    if (edit.type == "crop")
      continue;
    QRect r = pixelRect(img.size(), edit.from, edit.to);
    if (edit.type == "blur" && r.width() > 1 && r.height() > 1) {
      const QImage region = img.copy(r);
      const int radius = blurRadius(edit, img.size());
      QImage horizontal(region.size(), region.format());
      QImage softened(region.size(), region.format());
      if (region.isNull() || horizontal.isNull() || softened.isNull()) {
        QPainter fallback(&img);
        fallback.setCompositionMode(QPainter::CompositionMode_Source);
        fallback.fillRect(r, QColor("#151a20"));
        if (error) *error = "Could not allocate blur buffers. The area was redacted instead. Try again with a smaller image.";
        continue;
      }
      for (int y = 0; y < region.height(); ++y) {
        const QRgb *src = reinterpret_cast<const QRgb *>(region.constScanLine(y));
        QRgb *dst = reinterpret_cast<QRgb *>(horizontal.scanLine(y));
        int red = 0, green = 0, blue = 0, alpha = 0;
        for (int x = -radius; x <= radius; ++x) {
          const QRgb pixel = src[std::clamp(x, 0, region.width() - 1)];
          red += qRed(pixel); green += qGreen(pixel);
          blue += qBlue(pixel); alpha += qAlpha(pixel);
        }
        const int count = radius * 2 + 1;
        for (int x = 0; x < region.width(); ++x) {
          dst[x] = qRgba(red / count, green / count, blue / count,
                         alpha / count);
          const QRgb remove = src[std::clamp(x - radius, 0, region.width() - 1)];
          const QRgb add = src[std::clamp(x + radius + 1, 0, region.width() - 1)];
          red += qRed(add) - qRed(remove);
          green += qGreen(add) - qGreen(remove);
          blue += qBlue(add) - qBlue(remove);
          alpha += qAlpha(add) - qAlpha(remove);
        }
      }
      for (int x = 0; x < region.width(); ++x) {
        int red = 0, green = 0, blue = 0, alpha = 0;
        for (int y = -radius; y <= radius; ++y) {
          const QRgb *row = reinterpret_cast<const QRgb *>(
              horizontal.constScanLine(std::clamp(y, 0, region.height() - 1)));
          const QRgb pixel = row[x];
          red += qRed(pixel); green += qGreen(pixel);
          blue += qBlue(pixel); alpha += qAlpha(pixel);
        }
        const int count = radius * 2 + 1;
        for (int y = 0; y < region.height(); ++y) {
          QRgb *row = reinterpret_cast<QRgb *>(softened.scanLine(y));
          row[x] = qRgba(red / count, green / count, blue / count,
                         alpha / count);
          const QRgb *removeRow = reinterpret_cast<const QRgb *>(
              horizontal.constScanLine(std::clamp(y - radius, 0, region.height() - 1)));
          const QRgb *addRow = reinterpret_cast<const QRgb *>(
              horizontal.constScanLine(std::clamp(y + radius + 1, 0, region.height() - 1)));
          red += qRed(addRow[x]) - qRed(removeRow[x]);
          green += qGreen(addRow[x]) - qGreen(removeRow[x]);
          blue += qBlue(addRow[x]) - qBlue(removeRow[x]);
          alpha += qAlpha(addRow[x]) - qAlpha(removeRow[x]);
        }
      }
      QPainter blurPainter(&img);
      blurPainter.setCompositionMode(QPainter::CompositionMode_Source);
      blurPainter.drawImage(r.topLeft(), softened);
      continue;
    }
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    const double unit = annotationUnit(img);
    const double markSize = std::clamp(edit.size, 0.5, 8.0);
    QPointF a(edit.from.x() * img.width(), edit.from.y() * img.height());
    QPointF b(edit.to.x() * img.width(), edit.to.y() * img.height());
    auto stroke = [&](const QPainterPath &path, bool fillHead = false) {
      const double width = unit * markSize;
      if (edit.outline && (edit.type == "arrow" || edit.type == "line" || edit.type == "pen")) {
        const QColor contrast = qGray(edit.color.rgb()) > 150 ? QColor("#151a20") : Qt::white;
        if (fillHead) p.fillPath(path, contrast);
        p.strokePath(path, QPen(contrast, width + std::max(2., width * .7),
                               Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      }
      if (fillHead) p.fillPath(path, edit.color);
      p.strokePath(path, QPen(edit.color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    };
    if (edit.type == "redact") {
      // Opaque fill replaces pixels. No blur, reversible filter, or source
      // metadata in export.
      p.setCompositionMode(QPainter::CompositionMode_Source);
      p.fillRect(r, QColor("#151a20"));
    } else if (edit.type == "highlight") {
      QColor fill = edit.color;
      fill.setAlphaF(std::clamp(edit.opacity, 0., 1.));
      p.fillRect(r, fill);
      if (edit.opacity > 0) {
        p.setPen(QPen(edit.color, unit * 0.5));
        p.drawRect(r);
      }
    } else if (edit.type == "line" || edit.type == "box" ||
               edit.type == "ellipse") {
      QPainterPath path;
      if (edit.type == "line") { path.moveTo(a); path.lineTo(b); }
      else if (edit.type == "box") path.addRect(QRectF(a, b).normalized());
      else path.addEllipse(QRectF(a, b).normalized());
      if (edit.filled && edit.type != "line") {
        QColor fill = edit.background;
        fill.setAlphaF(std::clamp(edit.opacity, 0., 1.));
        p.fillPath(path, fill);
      }
      stroke(path);
    } else if (edit.type == "pen" && edit.points.size() >= 2) {
      QPainterPath path;
      path.moveTo(edit.points.first().x() * img.width(),
                  edit.points.first().y() * img.height());
      for (qsizetype i = 1; i < edit.points.size(); ++i)
        path.lineTo(edit.points[i].x() * img.width(),
                    edit.points[i].y() * img.height());
      stroke(path);
    } else if (edit.type == "arrow") {
      const double angle = std::atan2(b.y() - a.y(), b.x() - a.x());
      const double head = unit * 5 * markSize;
      const QPointF left = b - QPointF(std::cos(angle - 0.55), std::sin(angle - 0.55)) * head;
      const QPointF right = b - QPointF(std::cos(angle + 0.55), std::sin(angle + 0.55)) * head;
      const bool filled = edit.arrowHead == "filled";
      QPainterPath path;
      path.moveTo(a);
      path.lineTo(filled ? (left + right) / 2 : b);
      path.moveTo(left);
      path.lineTo(b);
      path.lineTo(right);
      if (filled) path.closeSubpath();
      stroke(path, filled);
    } else if (edit.type == "step") {
      const int step = steps.indexOf(&edit) + 1;
      p.setPen(QPen(Qt::white, unit * 0.7));
      p.setBrush(edit.color);
      p.drawEllipse(a, unit * 5 * markSize, unit * 5 * markSize);
      QFont font("sans-serif");
      font.setPixelSize(qRound(unit * 5 * markSize));
      font.setWeight(QFont::DemiBold);
      p.setFont(font);
      p.setPen(edit.numberColor);
      p.drawText(
          QRectF(a - QPointF(unit * 5 * markSize, unit * 5 * markSize),
                 QSizeF(unit * 10 * markSize, unit * 10 * markSize)),
          Qt::AlignCenter, QString::number(edit.number > 0 ? edit.number : step));
    } else if (edit.type == "text") {
      QFont font("sans-serif");
      font.setPixelSize(textPixelSize(edit, img));
      font.setWeight(QFont::DemiBold);
      p.setFont(font);
      const QRectF normalized = annotationBounds(edit, img);
      const QString visibleText = layoutText(edit, img, font).visibleText;
      const QRectF box(normalized.x() * img.width(),
                       normalized.y() * img.height(),
                       normalized.width() * img.width(),
                       normalized.height() * img.height());
      const double inset = std::max(4., textPixelSize(edit, img) * 0.27);
      const QRectF content = box.adjusted(inset, 0, -inset, 0);
      const auto align = Qt::AlignVCenter |
          (edit.textAlign == "left" ? Qt::AlignLeft
           : edit.textAlign == "right" ? Qt::AlignRight : Qt::AlignHCenter);
      if (edit.textStyle == "box") {
        QColor fill = edit.background;
        fill.setAlphaF(std::clamp(edit.backgroundOpacity, 0.0, 1.0));
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawRoundedRect(box, unit * 1.3 * markSize,
                          unit * 1.3 * markSize);
      } else {
        p.setPen(QColor(0, 0, 0, 210));
        p.drawText(content.translated(std::max(1., unit * 0.6),
                                      std::max(1., unit * 0.6)),
                   align, visibleText);
      }
      p.setPen(edit.color);
      p.drawText(content, align, visibleText);
    }
  }
  return applyCrop ? cropImage(img, edits) : img;
}

QSize outputSize(QSize source, const Options &o, QMargins room) {
  if (source.isEmpty() || o.style == 8)
    return source;
  source = source.grownBy(room);
  const double rate = std::clamp(o.padding, 0.02, 0.22) *
                      (o.style == 5 ? 0.6 : 1.0);
  // Base the frame on the shorter edge so wide windows do not gain a huge
  // top and bottom matte. Outline deliberately uses a slimmer treatment.
  const int pad = std::clamp(qRound(std::min(source.width(), source.height()) * rate),
                             o.style == 5 ? 8 : 16, o.style == 5 ? 56 : 96);
  int w = source.width() + pad * 2, h = source.height() + pad * 2;
  double aspect = o.aspect == 1   ? 1.
                  : o.aspect == 2 ? 16. / 9.
                  : o.aspect == 3 ? 4. / 3.
                  : o.aspect == 4 ? 9. / 16.
                                  : 0;
  if (aspect > 0) {
    if (double(w) / h < aspect)
      w = std::ceil(h * aspect);
    else
      h = std::ceil(w / aspect);
  }
  return {w, h};
}

static QColor hueColor(const QImage &img) {
  const QImage small =
      img.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  std::array<double, 72> votes{};
  double opaquePixels = 0;
  for (int y = 0; y < small.height(); ++y)
    for (int x = 0; x < small.width(); ++x) {
      const QColor c = small.pixelColor(x, y);
      const double alpha = c.alphaF();
      opaquePixels += alpha;
      // Saturated highlights are useful evidence, including channels at 255.
      // Reject near-black noise and neutral chrome, not bright greens/reds.
      if (alpha < 0.1 || c.hsvSaturationF() < 0.12 || c.valueF() < 0.08)
        continue;
      const int bin = std::clamp(int(c.hsvHueF() * 72), 0, 71);
      votes[bin] += alpha * c.hsvSaturationF() * std::sqrt(c.valueF());
    }
  int dominant = 0;
  double best = 0;
  for (int bin = 0; bin < 72; ++bin) {
    double total = 0;
    for (int offset = -2; offset <= 2; ++offset)
      total += votes[(bin + offset + 72) % 72];
    if (total > best) {
      best = total;
      dominant = bin;
    }
  }
  if (best < std::max(2., opaquePixels * 0.008))
    return QColor("#888888"); // Neutral images get a neutral finish, not blue.
  double dx = 0, dy = 0;
  for (int offset = -2; offset <= 2; ++offset) {
    const int bin = (dominant + offset + 72) % 72;
    const double angle = (bin + 0.5) / 72 * 2 * M_PI;
    dx += std::cos(angle) * votes[bin];
    dy += std::sin(angle) * votes[bin];
  }
  double hue = std::atan2(dy, dx) / (2 * M_PI);
  if (hue < 0)
    hue += 1;
  return QColor::fromHsvF(hue, 0.6, 0.8);
}

// Three separable box passes approximate a Gaussian shadow at quarter
// resolution.
static QImage shadowMask(QSize size, QRectF rect, double radius, int blur) {
  QImage img(size, QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::transparent);
  {
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 70));
    p.drawRoundedRect(rect, radius, radius);
  }
  for (int pass = 0; pass < 3; ++pass) {
    for (int axis = 0; axis < 2; ++axis) {
      QImage out(size, QImage::Format_ARGB32_Premultiplied);
      out.fill(Qt::transparent);
      const int length = axis == 0 ? size.width() : size.height(),
                lines = axis == 0 ? size.height() : size.width();
      auto alpha = [&](int pos, int line) {
        return pos < 0 || pos >= length
                   ? 0
                   : qAlpha(img.pixel(axis == 0 ? pos : line,
                                      axis == 0 ? line : pos));
      };
      for (int line = 0; line < lines; ++line) {
        int sum = 0;
        for (int i = -blur; i <= blur; ++i)
          sum += alpha(i, line);
        for (int i = 0; i < length; ++i) {
          out.setPixel(axis == 0 ? i : line, axis == 0 ? line : i,
                       qRgba(0, 0, 0, sum / (blur * 2 + 1)));
          sum += alpha(i + blur + 1, line) - alpha(i - blur, line);
        }
      }
      img = out;
    }
  }
  return img;
}


// One line of pixels along an edge, `depth` lines in from it. Corners are
// left out, since a window's rounded corner shows what is behind it.
static QVector<QRgb> edgeLine(const QImage &image, int edge, int depth) {
  const bool across = edge == 0 || edge == 2; // top or bottom: a row
  const int length = across ? image.width() : image.height();
  const int corner = std::min(16, length / 10);
  QVector<QRgb> line;
  line.reserve(length - 2 * corner);
  for (int i = corner; i < length - corner; ++i) {
    const int x = across ? i : edge == 3 ? depth : image.width() - 1 - depth;
    const int y = across ? (edge == 0 ? depth : image.height() - 1 - depth) : i;
    line << image.pixel(x, y);
  }
  return line;
}
static QRgb median(const QVector<QRgb> &pixels) {
  std::array<std::array<int, 256>, 3> counts{};
  for (QRgb pixel : pixels) {
    ++counts[0][qRed(pixel)];
    ++counts[1][qGreen(pixel)];
    ++counts[2][qBlue(pixel)];
  }
  int channel[3];
  for (int c = 0; c < 3; ++c) {
    int seen = 0, value = 0;
    while (value < 255 && (seen += counts[c][value]) * 2 < pixels.size())
      ++value;
    channel[c] = value;
  }
  return qRgb(channel[0], channel[1], channel[2]);
}
// Every inspected pixel must be close to `color`. A short header or footer
// still matters on a tall scrollshot: ignoring it can extend a scrollbar.
static bool flat(const QVector<QRgb> &line, QRgb color) {
  if (line.isEmpty())
    return false;
  for (QRgb pixel : line)
    if (qAlpha(pixel) != 255 ||
        std::max({std::abs(qRed(pixel) - qRed(color)),
                  std::abs(qGreen(pixel) - qGreen(color)),
                  std::abs(qBlue(pixel) - qBlue(color))}) > 8)
      return false;
  return true;
}
static QRgb edgeColor(const QImage &image, int edge) {
  return median(edgeLine(image, edge, 0));
}

QMargins edgeRoom(const QImage &source) {
  if (source.isNull())
    return {};
  const QImage image = source.format() == QImage::Format_ARGB32 ||
                               source.format() == QImage::Format_RGB32
                           ? source
                           : source.convertToFormat(QImage::Format_ARGB32);
  // About 4 percent of the short side, at least 12 pixels: enough to read
  // as breathing room without changing the shape of the capture.
  const int shortSide = std::min(image.width(), image.height());
  if (shortSide < 120)
    return {};
  const int room = std::clamp(qRound(shortSide * 0.04), 12, 48);
  int add[4] = {0, 0, 0, 0}; // top, right, bottom, left
  for (int edge = 0; edge < 4; ++edge) {
    const QRgb color = edgeColor(image, edge);
    // A strip a few pixels wide has to be flat before it counts.
    int depth = 0;
    while (depth < room && flat(edgeLine(image, edge, depth), color))
      ++depth;
    if (depth >= 3)
      add[edge] = room - depth;
  }
  return {add[3], add[0], add[1], add[2]};
}
// The capture with its flat edges carried outward in their own color.
static QImage withEdgeRoom(const QImage &source, QMargins room) {
  if (room.isNull())
    return source;
  QImage image = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  QImage result(image.size().grownBy(room), image.format());
  QPainter p(&result);
  const QRect inside(room.left(), room.top(), image.width(), image.height());
  // Sides first, then top and bottom across the full width, so corners
  // take the top or bottom color.
  if (room.left())
    p.fillRect(0, 0, room.left(), result.height(), QColor(edgeColor(image, 3)));
  if (room.right())
    p.fillRect(inside.right() + 1, 0, room.right(), result.height(),
               QColor(edgeColor(image, 1)));
  if (room.top())
    p.fillRect(0, 0, result.width(), room.top(), QColor(edgeColor(image, 0)));
  if (room.bottom())
    p.fillRect(0, inside.bottom() + 1, result.width(), room.bottom(),
               QColor(edgeColor(image, 2)));
  p.setCompositionMode(QPainter::CompositionMode_Source);
  p.drawImage(inside.topLeft(), image);
  return result;
}

QImage compose(const QImage &capture, const Options &o, int maxEdge,
               qint64 maxPixels) {
  if (capture.isNull())
    return {};
  const auto bounded = [maxEdge, maxPixels](QSize size) {
    if (maxEdge > 0 && std::max(size.width(), size.height()) > maxEdge)
      size.scale(maxEdge, maxEdge, Qt::KeepAspectRatio);
    const qint64 pixels = qint64(size.width()) * size.height();
    if (maxPixels > 0 && pixels > maxPixels) {
      const double ratio = std::sqrt(double(maxPixels) / pixels);
      size = QSize(std::max(1, int(std::floor(size.width() * ratio))),
                   std::max(1, int(std::floor(size.height() * ratio))));
    }
    return size;
  };
  if (o.style == 8) {
    const QSize target = bounded(capture.size());
    return target == capture.size()
               ? capture
               : capture.scaled(target, Qt::IgnoreAspectRatio,
                                Qt::SmoothTransformation);
  }
  const QImage source = withEdgeRoom(capture, edgeRoom(capture));
  const QSize full = outputSize(source.size(), o), target = bounded(full);
  const double scale = double(target.width()) / full.width();
  const double w = source.width() * scale, h = source.height() * scale;
  const QRectF card((target.width() - w) / 2, (target.height() - h) / 2, w, h);
  QImage result(target, QImage::Format_ARGB32_Premultiplied);
  if (result.isNull())
    return {};
  result.fill(Qt::transparent);
  QPainter p(&result);
  p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
  QColor first, second;
  switch (o.style) {
  case 0:
    first = QColor("#f2efe9");
    second = QColor("#d9d3c8");
    break;
  case 1:
    first = QColor("#4d5364");
    second = QColor("#252a37");
    break;
  case 2:
    first = QColor("#203d46");
    second = QColor("#0b1727");
    break;
  case 3:
    first = QColor("#fad1bd");
    second = QColor("#8c80c1");
    break;
  case 4: {
    QColor c = hueColor(source);
    if (c.hsvSaturationF() < 0.01) {
      first = QColor("#dededb");
      second = QColor("#a6a6a2");
    } else {
      // Keep both stops in the captured hue family. Only lightness and
      // saturation change; adding a hue offset turned green captures cyan.
      first = QColor::fromHsvF(c.hsvHueF(), 0.38, 0.9);
      second = QColor::fromHsvF(c.hsvHueF(), 0.58, 0.6);
    }
    break;
  }
  case 5:
    first = QColor("#f9faf7");
    second = first;
    break;
  case 6:
    first = QColor("#c6b7a3");
    second = QColor("#f2e8d9");
    break;
  default:
    first = QColor("#8595ed");
    second = QColor("#e8b6d3");
    break;
  }
  QLinearGradient gradient(0, 0, target.width(), target.height());
  gradient.setColorAt(0, first);
  gradient.setColorAt(1, second);
  p.fillRect(result.rect(), gradient);
  if (o.style == 3 || o.style == 7 || o.style == 2) {
    QRadialGradient glow(target.width() * 0.8, target.height() * 0.05,
                         target.width() * 0.9);
    glow.setColorAt(0, o.style == 2 ? QColor(65, 133, 131, 100)
                                    : QColor(255, 238, 202, 180));
    glow.setColorAt(1, Qt::transparent);
    p.fillRect(result.rect(), glow);
  }
  if (o.style == 6) {
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 35));
    QPainterPath shape;
    shape.moveTo(0, target.height());
    shape.lineTo(target.width(), target.height() * 0.24);
    shape.lineTo(target.width(), target.height());
    p.drawPath(shape);
  }
  const double radius = std::min(w, h) * 0.016;
  if (o.style != 5) {
    const int divisor = 4;
    QSize sz((target.width() + divisor - 1) / divisor,
             (target.height() + divisor - 1) / divisor);
    QRectF sr(card.x() / divisor, (card.y() + h * 0.025) / divisor, w / divisor,
              h / divisor);
    p.drawImage(result.rect(),
                shadowMask(sz, sr, radius / divisor,
                           std::max(1, int(std::min(w, h) * 0.028 / divisor))));
  }
  QPainterPath clip;
  clip.addRoundedRect(card, radius, radius);
  p.save();
  p.setClipPath(clip);
  p.drawImage(card, source);
  p.restore();
  p.setBrush(Qt::NoBrush);
  p.setPen(
      QPen(o.style == 5 ? QColor(35, 45, 40, 80) : QColor(255, 255, 255, 80),
           std::max(0.7, scale)));
  p.drawRoundedRect(card, radius, radius);
  return result;
}

QImage demoImage(int variant) {
  QImage img(1280, 800, QImage::Format_ARGB32_Premultiplied);
  img.fill(QColor("#f8f9f5"));
  QPainter p(&img);
  p.setRenderHint(QPainter::Antialiasing);
  auto text = [&](int x, int y, QString t, int size,
                  QColor color = QColor("#252d2a"), bool bold = false) {
    QFont f("sans-serif");
    f.setPixelSize(size);
    f.setWeight(bold ? QFont::DemiBold : QFont::Normal);
    p.setFont(f);
    p.setPen(color);
    p.drawText(x, y, t);
  };
  auto box = [&](QRectF r, QColor c, int radius = 12) {
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawRoundedRect(r, radius, radius);
  };
  if (variant == 1) {
    img.fill(QColor("#19212b"));
    text(42, 52, "~/projects/atlas", 18, QColor("#9faab7"));
    text(1170, 52, "zsh", 17, QColor("#738193"));
    p.setPen(QColor("#303a47"));
    p.drawLine(0, 80, 1280, 80);
    text(46, 143, "❯  git status", 23, QColor("#d0eaae"));
    text(46, 193, "On branch main", 22, QColor("#d6dce4"));
    text(46, 235, "Your branch is up to date with 'origin/main'.", 22,
         QColor("#9faab7"));
    text(46, 315, "❯  pnpm run build", 23, QColor("#d0eaae"));
    text(46, 382, "vite v6.2.0 building for production...", 22,
         QColor("#9faab7"));
    text(46, 438, "✓  142 modules transformed.", 22, QColor("#afdab7"));
    text(46, 488, "dist/index.html                    0.62 kB", 22,
         QColor("#d6dce4"));
    text(46, 532, "dist/assets/index.css             12.48 kB", 22,
         QColor("#d6dce4"));
    text(46, 576, "dist/assets/index.js              48.21 kB", 22,
         QColor("#d6dce4"));
    text(46, 652, "✓  built in 384ms", 22, QColor("#afdab7"));
    text(46, 735, "❯", 24, QColor("#d0eaae"));
    return img;
  }
  box(QRectF(0, 0, 224, 800), QColor("#eef0e9"), 0);
  box(QRectF(28, 27, 29, 29), QColor("#476b4e"), 9);
  text(70, 51, "noon", 28, QColor("#263d2c"), true);
  text(31, 116, "WORKSPACE", 11, QColor("#81897c"), true);
  box(QRectF(16, 134, 190, 43), QColor("#dde5d6"), 8);
  text(36, 162, "Overview", 15, QColor("#35553a"), true);
  text(36, 213, "Projects", 15, QColor("#687062"));
  text(36, 261, "Activity", 15, QColor("#687062"));
  text(36, 309, "Team", 15, QColor("#687062"));
  text(31, 742, "Made room for good work.", 12, QColor("#81897c"));
  text(265, 49, "Workspace  /  Overview", 13, QColor("#8a9285"));
  box(QRectF(1185, 23, 36, 36), QColor("#e0e6d6"), 18);
  text(1193, 47, "JS", 13, QColor("#42613d"), true);
  text(269, 129, "A little progress, every day.", 32, QColor("#26362a"), true);
  text(270, 164, "Your team's work, with a little more breathing room.", 16,
       QColor("#838d7c"));
  box(QRectF(1081, 104, 153, 40), QColor("#3f6044"), 8);
  text(1101, 130, "+  New project", 14, Qt::white, true);
  const QStringList labels = {"Projects in motion", "Tasks completed",
                              "Focus time"},
                    values = {"12", "148", "32.5 h"},
                    deltas = {"3 ready to ship", "↑  18% this week",
                              "↑  6.2 h this week"};
  for (int i = 0; i < 3; ++i) {
    int x = 269 + i * 327;
    box(QRectF(x, 207, 310, 159), Qt::white);
    text(x + 23, 243, labels[i], 14, QColor("#858d7d"));
    text(x + 23, 300, values[i], 39, QColor("#2c3f2d"), true);
    text(x + 23, 340, deltas[i], 12, QColor("#6c8760"));
  }
  box(QRectF(269, 389, 637, 357), Qt::white);
  text(293, 426, "Steady momentum", 18, QColor("#354735"), true);
  text(293, 451, "Completed tasks over the last seven days", 12,
       QColor("#919888"));
  int bars[] = {92, 140, 117, 190, 170, 227, 252};
  for (int i = 0; i < 7; ++i) {
    int x = 314 + i * 80;
    box(QRectF(x, 699 - bars[i], 37, bars[i]),
        QColor(i == 6 ? "#456b46" : "#dae6cc"), 6);
    text(x + 7, 723, QStringList{"M", "T", "W", "T", "F", "S", "S"}[i], 11,
         QColor("#8a9381"));
  }
  box(QRectF(930, 389, 303, 357), QColor("#e6eddd"));
  text(953, 427, "Up next", 18, QColor("#354735"), true);
  QStringList titles = {"A calmer homepage", "Summer collection",
                        "The small details"};
  for (int i = 0; i < 3; ++i) {
    int y = 476 + i * 83;
    box(QRectF(953, y - 13, 18, 18), QColor("#d4dec7"), 5);
    text(983, y + 1, titles[i], 13, QColor("#4b6044"), true);
    text(983, y + 23, i == 0 ? "Design  ·  Today" : "In progress  ·  This week",
         11, QColor("#8a967e"));
  }
  return img;
}
} // namespace Frame
