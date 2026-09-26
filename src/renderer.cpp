#include "renderer.hpp"
#include <QFont>
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
  QRectF r(QPointF(from.x() * size.width(), from.y() * size.height()),
           QPointF(to.x() * size.width(), to.y() * size.height()));
  return r.normalized().toAlignedRect().intersected(QRect(QPoint(), size));
}

QImage applyEdits(const QImage &source, const QVector<Edit> &edits) {
  QImage img = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  img.setDevicePixelRatio(1);
  int step = 0;
  for (const Edit &edit : edits) {
    QRect r = pixelRect(img.size(), edit.from, edit.to);
    if (edit.type == "crop") {
      if (r.width() >= 2 && r.height() >= 2)
        img = img.copy(r);
      continue;
    }
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    const double unit =
        std::max(2., std::min(img.width(), img.height()) / 240.);
    QPointF a(edit.from.x() * img.width(), edit.from.y() * img.height());
    QPointF b(edit.to.x() * img.width(), edit.to.y() * img.height());
    if (edit.type == "redact") {
      // Opaque fill replaces pixels. No blur, reversible filter, or source
      // metadata in export.
      p.setCompositionMode(QPainter::CompositionMode_Source);
      p.fillRect(r, QColor("#151a20"));
    } else if (edit.type == "highlight") {
      p.fillRect(r, QColor(250, 210, 70, 95));
      p.setPen(QPen(QColor("#eab841"), unit * 0.5));
      p.drawRect(r);
    } else if (edit.type == "arrow") {
      const double angle = std::atan2(b.y() - a.y(), b.x() - a.x());
      const double head = unit * 5;
      p.setPen(
          QPen(edit.color, unit, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      p.drawLine(a, b);
      p.drawLine(b,
                 b - QPointF(std::cos(angle - 0.55), std::sin(angle - 0.55)) *
                         head);
      p.drawLine(b,
                 b - QPointF(std::cos(angle + 0.55), std::sin(angle + 0.55)) *
                         head);
    } else if (edit.type == "step") {
      ++step;
      p.setPen(QPen(Qt::white, unit * 0.7));
      p.setBrush(edit.color);
      p.drawEllipse(a, unit * 5, unit * 5);
      QFont font("sans-serif");
      font.setPixelSize(qRound(unit * 5));
      font.setWeight(QFont::DemiBold);
      p.setFont(font);
      p.drawText(
          QRectF(a - QPointF(unit * 5, unit * 5), QSizeF(unit * 10, unit * 10)),
          Qt::AlignCenter, QString::number(step));
    } else if (edit.type == "text") {
      QFont font("sans-serif");
      font.setPixelSize(qRound(unit * 6));
      font.setWeight(QFont::DemiBold);
      p.setFont(font);
      const QRectF bounds = p.fontMetrics().boundingRect(edit.text).adjusted(
          -unit * 2, -unit, unit * 2, unit);
      const QRectF box(a, bounds.size());
      p.setPen(Qt::NoPen);
      p.setBrush(QColor("#151a20"));
      p.drawRoundedRect(box, unit * 1.3, unit * 1.3);
      p.setPen(Qt::white);
      p.drawText(box, Qt::AlignCenter, edit.text);
    }
  }
  return img;
}

QSize outputSize(QSize source, const Options &o) {
  if (source.isEmpty() || o.style == 8)
    return source;
  const int pad = qRound(std::max(source.width(), source.height()) *
                         std::clamp(o.padding, 0.02, 0.22));
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

QImage compose(const QImage &source, const Options &o, int maxEdge) {
  if (source.isNull())
    return {};
  if (o.style == 8)
    return maxEdge > 0 && std::max(source.width(), source.height()) > maxEdge
               ? source.scaled(QSize(maxEdge, maxEdge), Qt::KeepAspectRatio,
                               Qt::SmoothTransformation)
               : source;
  QSize full = outputSize(source.size(), o), target = full;
  if (maxEdge > 0 && std::max(full.width(), full.height()) > maxEdge)
    target.scale(maxEdge, maxEdge, Qt::KeepAspectRatio);
  const double scale = double(target.width()) / full.width();
  const double w = source.width() * scale, h = source.height() * scale;
  const QRectF card((target.width() - w) / 2, (target.height() - h) / 2, w, h);
  QImage result(target, QImage::Format_ARGB32_Premultiplied);
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
