#include "displays.hpp"
#include <QGuiApplication>
#include <QScreen>
#include <algorithm>
#include <climits>
#include <numeric>

namespace {
QString product(const Displays::Info &d) {
  if (d.name.startsWith("eDP") || d.name.startsWith("LVDS") ||
      d.name.startsWith("DSI"))
    return "Built-in display";
  auto known = [](QString s) {
    s = s.simplified();
    return s.compare("Unknown", Qt::CaseInsensitive) == 0 ? QString() : s;
  };
  // "Dell Inc." -> "Dell"; "DELL S2721DGF" -> "S2721DGF".
  const QString make = known(d.make).section(' ', 0, 0).remove(',');
  QString model = known(d.model);
  if (!make.isEmpty() && model.startsWith(make + ' ', Qt::CaseInsensitive))
    model = model.mid(make.size() + 1);
  const QString result = (make + ' ' + model).trimmed();
  return result.isEmpty() ? d.name : result;
}
} // namespace

QList<Displays::Names> Displays::describe(const QList<Info> &displays) {
  const int n = displays.size();
  QStringList positions(n);
  if (n > 1) {
    auto center = [&](int i) { return displays[i].bounds.center(); };
    int minX = INT_MAX, maxX = INT_MIN, minY = INT_MAX, maxY = INT_MIN;
    for (int i = 0; i < n; ++i) {
      minX = std::min(minX, center(i).x());
      maxX = std::max(maxX, center(i).x());
      minY = std::min(minY, center(i).y());
      maxY = std::max(maxY, center(i).y());
    }
    const bool horizontal = maxX - minX >= maxY - minY;
    QList<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
      return horizontal ? center(a).x() < center(b).x()
                        : center(a).y() < center(b).y();
    });
    const QStringList words =
        n == 2   ? (horizontal ? QStringList{"Left", "Right"}
                               : QStringList{"Top", "Bottom"})
        : n == 3 ? (horizontal ? QStringList{"Left", "Center", "Right"}
                               : QStringList{"Top", "Middle", "Bottom"})
                 : QStringList{};
    for (int rank = 0; rank < n; ++rank)
      positions[order[rank]] =
          words.isEmpty() ? QString("Display %1").arg(rank + 1) : words[rank];
  }
  QList<Names> result;
  for (int i = 0; i < n; ++i) {
    const auto &d = displays[i];
    QStringList parts;
    if (!positions[i].isEmpty())
      parts << positions[i];
    parts << product(d);
    if (!d.pixels.isEmpty())
      parts << QString("%1 × %2").arg(d.pixels.width()).arg(d.pixels.height());
    const QString place =
        positions[i].isEmpty() ? (product(d).startsWith("Built-in")
                                      ? product(d).toLower()
                                      : product(d))
        : positions[i].startsWith("Display") ? positions[i].toLower()
                                             : positions[i].toLower() + " display";
    result.append({parts.join(" · "), place});
  }
  return result;
}

QList<Displays::Info> Displays::fromScreens() {
  QList<Info> result;
  for (auto *screen : QGuiApplication::screens())
    result.append({screen->name(), screen->manufacturer(), screen->model(),
                   screen->geometry(),
                   (QSizeF(screen->size()) * screen->devicePixelRatio())
                       .toSize()});
  return result;
}
