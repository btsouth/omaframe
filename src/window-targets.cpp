#include "window-targets.hpp"
#include <QJsonObject>
#include <QRectF>
#include <QVariantMap>
#include <algorithm>

namespace {
QRectF rect(const QJsonArray &point, const QJsonArray &size) {
  if (point.size() != 2 || size.size() != 2)
    return {};
  return {point[0].toDouble(), point[1].toDouble(), size[0].toDouble(),
          size[1].toDouble()};
}
}

QVariantList WindowTargets::fromHyprland(const QJsonArray &monitors,
                                         const QJsonArray &clients,
                                         const QStringList &requested) {
  struct Monitor { QString name; int id; QRectF bounds; int workspace; };
  QList<Monitor> outputs;
  for (const auto &value : monitors) {
    const auto m = value.toObject();
    const QString name = m.value("name").toString();
    if (!requested.contains(name))
      continue;
    // Hyprland reports output width/height in physical pixels, while x/y and
    // client at/size use logical coordinates. Match the selection overlay.
    const double scale = m.value("scale").toDouble(1);
    int width = m.value("width").toInt();
    int height = m.value("height").toInt();
    if (m.value("transform").toInt() % 2)
      std::swap(width, height);
    if (scale <= 0)
      continue;
    const QRectF bounds(m.value("x").toDouble(), m.value("y").toDouble(),
                        qRound(width / scale), qRound(height / scale));
    if (bounds.width() > 0 && bounds.height() > 0)
      outputs << Monitor{name, m.value("id").toInt(-1), bounds,
                         m.value("activeWorkspace").toObject().value("id").toInt()};
  }
  struct Ranked { int focus; QVariantMap target; };
  QList<Ranked> ranked;
  for (const auto &value : clients) {
    const auto c = value.toObject();
    if (!c.value("mapped").toBool() || c.value("hidden").toBool() ||
        !c.value("visible").toBool())
      continue;
    const QRectF window = rect(c.value("at").toArray(), c.value("size").toArray());
    if (window.width() < 24 || window.height() < 24)
      continue;
    for (const auto &m : outputs) {
      if (c.value("monitor").toInt(-1) != m.id ||
          (!c.value("pinned").toBool() &&
           c.value("workspace").toObject().value("id").toInt() != m.workspace))
        continue;
      const QRectF shown = window.intersected(m.bounds);
      if (shown.width() < 24 || shown.height() < 24)
        continue;
      ranked << Ranked{c.value("focusHistoryID").toInt(1000000),
                       {{"monitor", m.name},
                        {"x", (shown.x() - m.bounds.x()) / m.bounds.width()},
                        {"y", (shown.y() - m.bounds.y()) / m.bounds.height()},
                        {"w", shown.width() / m.bounds.width()},
                        {"h", shown.height() / m.bounds.height()}}};
    }
  }
  std::stable_sort(ranked.begin(), ranked.end(),
                   [](const Ranked &a, const Ranked &b) { return a.focus < b.focus; });
  QVariantList targets;
  for (const auto &item : ranked)
    targets << item.target;
  return targets;
}

QStringList WindowTargets::dark(const QJsonArray &monitors) {
  QStringList dark;
  for (const auto &value : monitors) {
    const auto m = value.toObject();
    if (!m.value("dpmsStatus").toBool(true))
      dark << m.value("name").toString();
  }
  return dark;
}

QStringList WindowTargets::awake(const QJsonArray &monitors,
                                 const QStringList &requested) {
  const QStringList dark = WindowTargets::dark(monitors);
  QStringList result;
  for (const auto &name : requested)
    if (!dark.contains(name))
      result << name;
  return result.isEmpty() ? requested : result;
}
