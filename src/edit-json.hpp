#pragma once
#include "renderer.hpp"
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <cmath>
#include <optional>

namespace Frame {
inline QJsonObject editToJson(const Frame::Edit &edit) {
  QJsonArray points;
  for (const auto &point : edit.points)
    points.append(QJsonObject{{"x", point.x()}, {"y", point.y()}});
  return {{"type", edit.type},
          {"x1", edit.from.x()},
          {"y1", edit.from.y()},
          {"x2", edit.to.x()},
          {"y2", edit.to.y()},
          {"text", edit.text},
          {"color", edit.color.name(QColor::HexArgb)},
          {"size", edit.size},
          {"textStyle", edit.textStyle},
          {"textAlign", edit.textAlign},
          {"background", edit.background.name(QColor::HexArgb)},
          {"backgroundOpacity", edit.backgroundOpacity},
          {"points", points},
          {"start", edit.start},
          {"end", edit.end},
          {"number", edit.number},
          // Steps count in the order they were placed, not stacked.
          {"stepOrder", edit.stepOrder},
          {"textBoxWidth", edit.textBox.width()},
          {"textBoxHeight", edit.textBox.height()},
          {"outline", edit.outline}, {"arrowHead", edit.arrowHead},
          {"filled", edit.filled}, {"opacity", edit.opacity},
          {"numberColor", edit.numberColor.name(QColor::HexArgb)}};
}
inline std::optional<Frame::Edit> editFromJson(const QJsonObject &item) {
  const QString type = item.value("type").toString();
  if (!QStringList{"crop", "arrow", "line", "box", "ellipse", "highlight",
                   "redact", "blur", "pen", "step", "text"}
           .contains(type))
    return std::nullopt;
  const double x1 = item.value("x1").toDouble(),
               y1 = item.value("y1").toDouble();
  const double x2 = item.value("x2").toDouble(),
               y2 = item.value("y2").toDouble();
  const double size = item.value("size").toDouble(1);
  const double opacity = item.value("backgroundOpacity").toDouble(1);
  for (double value : {x1, y1, x2, y2})
    if (!std::isfinite(value) || qAbs(value) > 10)
      return std::nullopt;
  if (!std::isfinite(size) || size <= 0 || size > 10000 ||
      !std::isfinite(opacity) || opacity < 0 || opacity > 1)
    return std::nullopt;
  Frame::Edit edit{
      type, {x1, y1}, {x2, y2}, item.value("text").toString().left(240)};
  edit.color = QColor(item.value("color").toString());
  edit.background = QColor(item.value("background").toString());
  if (!edit.color.isValid() || !edit.background.isValid())
    return std::nullopt;
  const double boxWidth = item.value("textBoxWidth").toDouble(0);
  const double boxHeight = item.value("textBoxHeight").toDouble(0);
  if (!std::isfinite(boxWidth) || !std::isfinite(boxHeight) ||
      boxWidth < 0 || boxWidth > 1 || boxHeight < 0 || boxHeight > 1)
    return std::nullopt;
  edit.textBox = {boxWidth, boxHeight};
  edit.size = size;
  edit.textStyle = item.value("textStyle").toString("box");
  edit.textAlign = item.value("textAlign").toString("center");
  edit.backgroundOpacity = opacity;
  edit.outline = item.value("outline").toBool(false);
  edit.filled = item.value("filled").toBool(false);
  edit.arrowHead = item.value("arrowHead").toString("open");
  edit.opacity = item.value("opacity").toDouble(95. / 255.);
  edit.numberColor = QColor(item.value("numberColor").toString("#ffffff"));
  if (!QStringList{"open", "filled"}.contains(edit.arrowHead) ||
      !std::isfinite(edit.opacity) || edit.opacity < 0 || edit.opacity > 1 ||
      !edit.numberColor.isValid())
    return std::nullopt;
  // Older highlights stored an unused mark color; keep their gold appearance.
  if (type == "highlight" && !item.contains("opacity"))
    edit.color = QColor("#eab841");
  const QJsonArray points = item.value("points").toArray();
  if (points.size() > 2048)
    return std::nullopt;
  for (const auto &value : points) {
    const auto point = value.toObject();
    const double x = point.value("x").toDouble(),
                 y = point.value("y").toDouble();
    if (!std::isfinite(x) || !std::isfinite(y) || qAbs(x) > 10 || qAbs(y) > 10)
      return std::nullopt;
    edit.points.append({x, y});
  }
  edit.start = item.value("start").toDouble(0);
  edit.end = item.value("end").toDouble(-1);
  edit.number = item.value("number").toInt(0);
  edit.stepOrder = std::max(0, item.value("stepOrder").toInt(0));
  if (!std::isfinite(edit.start) || edit.start < 0 ||
      !std::isfinite(edit.end) || (edit.end != -1 && edit.end < edit.start) ||
      edit.number < 0)
    return std::nullopt;
  return edit;
}
} // namespace Frame
