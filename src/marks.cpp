#include "marks.hpp"
#include "mark-constraints.hpp"
#include <QBuffer>
#include <QLineF>
#include <QPainter>
#include <QSettings>
#include <cmath>

void MarkDocument::reset(const QImage &base) {
  m_transform.reset();
  m_copied.reset();
  m_base = base;
  m_edits.clear();
  m_undoStates.clear();
  m_redoStates.clear();
  m_selected = m_hiddenEdit = -1;
  emit changed();
}
void MarkDocument::restore(const QImage &base, QVector<Frame::Edit> edits,
                           int selected) {
  m_transform.reset();
  m_copied.reset();
  m_base = base;
  m_edits = std::move(edits);
  QVector<Frame::Edit *> steps;
  for (auto &edit : m_edits)
    if (edit.type == "step") {
      if (edit.stepOrder <= 0) edit.stepOrder = steps.size() + 1;
      steps.append(&edit);
    }
  std::stable_sort(steps.begin(), steps.end(), [](const auto *a, const auto *b) {
    return a->stepOrder < b->stepOrder;
  });
  for (int i = 0; i < steps.size(); ++i) steps[i]->stepOrder = i + 1;
  m_undoStates.clear();
  m_redoStates.clear();
  m_selected = std::clamp(selected, -1, int(m_edits.size()) - 1);
  m_hiddenEdit = -1;
  emit changed();
}
QVector<Frame::Edit> MarkDocument::visibleEdits() const {
  QVector<Frame::Edit> edits = m_edits;
  if (m_hiddenEdit >= 0 && m_hiddenEdit < edits.size())
    edits.removeAt(m_hiddenEdit);
  return edits;
}
int MarkDocument::newTextPixels() const {
  if (m_base.isNull())
    return 32;
  return std::clamp(
      qRound(std::min(m_base.width(), m_base.height()) * 0.06), 20, 64);
}
QVariantMap MarkDocument::labelDefaults() const {
  QSettings settings;
  settings.beginGroup("labels");
  auto color = [&settings](const QString &key, const QString &fallback) {
    const QColor value(settings.value(key, fallback).toString());
    return value.isValid() ? value.name() : fallback;
  };
  const QString style = settings.value("textStyle", "box").toString();
  const QString align = settings.value("textAlign", "center").toString();
  bool validOpacity = false;
  double opacity = settings.value("backgroundOpacity", 1.).toDouble(&validOpacity);
  if (!validOpacity || !std::isfinite(opacity)) opacity = 1.;
  const int pixels = settings.value("fontPx", 0).toInt();
  return {{"color", color("color", "#ffffff")},
          {"background", color("background", "#151a20")},
          {"backgroundOpacity", std::clamp(opacity, 0., 1.)},
          {"textStyle", style == "shadow" ? "shadow" : "box"},
          {"textAlign", QStringList{"left", "center", "right"}.contains(align) ? align : "center"},
          {"fontPx", pixels >= 8 && pixels <= 4096 ? pixels : newTextPixels()}};
}
QVariantMap MarkDocument::labelStyle() const {
  const auto selected = selectedAnnotation();
  return selected.value("type") == "text" ? selected : labelDefaults();
}
static QVariantMap factoryToolStyle(const QString &type) {
  if (type == "blur") return {{"size", 1.}};
  if (type == "highlight")
    return {{"color", "#eab841"}, {"opacity", 95. / 255.}};
  if (type == "step")
    return {{"color", "#e75439"}, {"numberColor", "#ffffff"}, {"size", 1.}};
  if (!QStringList{"arrow", "line", "box", "ellipse", "pen"}.contains(type))
    return {};
  QVariantMap fields{{"color", "#e75439"}, {"size", 1.}};
  if (type == "arrow" || type == "line" || type == "pen")
    fields["outline"] = false;
  if (type == "arrow") fields["arrowHead"] = "open";
  if (type == "box" || type == "ellipse") {
    fields["filled"] = false;
    fields["background"] = "#e75439";
    fields["opacity"] = .2;
  }
  return fields;
}
static std::optional<QVariantMap> validToolFields(const QString &type,
                                                 const QVariantMap &style) {
  const auto factory = factoryToolStyle(type);
  if (factory.isEmpty()) return std::nullopt;
  QVariantMap fields;
  for (auto it = style.begin(); it != style.end(); ++it) {
    const QString key = it.key();
    if (!factory.contains(key)) return std::nullopt;
    if (key == "color" || key == "background" || key == "numberColor") {
      const QColor color(it.value().toString());
      if (!color.isValid()) return std::nullopt;
      fields[key] = color.name();
    } else if (key == "size" || key == "opacity") {
      bool valid = false;
      const double value = it.value().toDouble(&valid);
      if (!valid || !std::isfinite(value) ||
          (key == "size" ? value < .5 || value > 8 : value < 0 || value > 1))
        return std::nullopt;
      fields[key] = value;
    } else if (key == "arrowHead") {
      if (!QStringList{"open", "filled"}.contains(it.value().toString()))
        return std::nullopt;
      fields[key] = it.value().toString();
    } else {
      if (it.value().metaType().id() != QMetaType::Bool &&
          it.value().toString() != "true" && it.value().toString() != "false")
        return std::nullopt;
      fields[key] = it.value().toBool();
    }
  }
  return fields;
}
static void applyToolFields(Frame::Edit &edit, const QVariantMap &fields) {
  if (fields.contains("color")) edit.color = QColor(fields["color"].toString());
  if (fields.contains("background")) edit.background = QColor(fields["background"].toString());
  if (fields.contains("numberColor")) edit.numberColor = QColor(fields["numberColor"].toString());
  if (fields.contains("size")) edit.size = fields["size"].toDouble();
  if (fields.contains("opacity")) edit.opacity = fields["opacity"].toDouble();
  if (fields.contains("outline")) edit.outline = fields["outline"].toBool();
  if (fields.contains("filled")) edit.filled = fields["filled"].toBool();
  if (fields.contains("arrowHead")) edit.arrowHead = fields["arrowHead"].toString();
}
QVariantMap MarkDocument::defaultToolStyle(const QString &type) const {
  auto fields = factoryToolStyle(type);
  QSettings settings;
  settings.beginGroup("tools/" + type);
  for (auto it = fields.begin(); it != fields.end(); ++it) {
    const auto valid = validToolFields(type, {{it.key(), settings.value(it.key(), it.value())}});
    if (valid) it.value() = valid->value(it.key());
  }
  return fields;
}
QVariantMap MarkDocument::toolDefaults() const {
  QVariantMap defaults;
  for (const auto *type : {"arrow", "line", "box", "ellipse", "pen", "step", "highlight", "blur"})
    defaults[type] = defaultToolStyle(type);
  return defaults;
}
void MarkDocument::applyToolDefaults(Frame::Edit &edit) const {
  applyToolFields(edit, defaultToolStyle(edit.type));
}
void MarkDocument::setToolStyle(const QString &type, const QVariantMap &style) {
  if (locked() || style.isEmpty()) return;
  const auto fields = validToolFields(type, style);
  if (!fields) return;
  endTransform(false);
  bool modified = false;
  if (m_selected >= 0 && m_selected < m_edits.size() && m_edits[m_selected].type == type) {
    Frame::Edit updated = m_edits[m_selected];
    applyToolFields(updated, *fields);
    if (updated != m_edits[m_selected]) {
      saveHistory();
      m_edits[m_selected] = updated;
      modified = true;
    }
  }
  QSettings settings;
  settings.beginGroup("tools/" + type);
  for (auto it = fields->begin(); it != fields->end(); ++it)
    settings.setValue(it.key(), it.value());
  if (modified) commit();
  else emit changed();
}
void MarkDocument::resetToolStyle(const QString &type) {
  setToolStyle(type, factoryToolStyle(type));
}
QString MarkDocument::stylePreview(const QString &type, const QVariantMap &style) const {
  if (type != "text" && factoryToolStyle(type).isEmpty()) return {};
  QImage image(560, 160, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  Frame::Edit edit{type, {.2, .25}, {.8, .75}};
  applyToolFields(edit, style);
  // Enlarge fine strokes and step numbers so their colors can be judged in
  // the small sample. Keep the largest sizes inside its bounds.
  edit.size = std::min(edit.size * (type == "step" ? 3.5 : type == "blur" ? 1. : 2.), 6.);
  if (type == "arrow" || type == "line") {
    edit.from = {.18, .65}; edit.to = {.82, .35};
  } else if (type == "text") {
    edit.color = QColor(style.value("color", "#ffffff").toString());
    edit.background = QColor(style.value("background", "#151a20").toString());
    edit.backgroundOpacity = style.value("backgroundOpacity", 1.).toDouble();
    edit.textStyle = style.value("textStyle", "box").toString();
    edit.textAlign = style.value("textAlign", "center").toString();
    edit.text = "Your label";
    edit.size = Frame::textSizeForPixels(std::clamp(style.value("fontPx", 32).toInt(), 24, 60), image);
    const QRectF bounds = Frame::annotationBounds(edit, image);
    edit.from = {(1-bounds.width())/2, (1-bounds.height())/2};
    edit.to = edit.from;
  } else if (type == "step") edit.from = {.5, .5};
  else if (type == "pen")
    edit.points = {{.2,.6}, {.3,.35}, {.4,.65}, {.5,.35}, {.6,.65}, {.8,.4}};
  else if (type == "blur") {
    QPainter painter(&image);
    painter.fillRect(image.rect(), QColor("#e5e9ed"));
    QFont font("sans-serif");
    font.setPixelSize(36);
    painter.setFont(font);
    painter.setPen(QColor("#151a20"));
    painter.drawText(image.rect(), Qt::AlignCenter, "Private details");
    edit.from = {.08,.08}; edit.to = {.92,.92};
  }
  const QImage rendered = Frame::applyEdits(image, {edit}, false);
  QByteArray png;
  QBuffer buffer(&png);
  buffer.open(QIODevice::WriteOnly);
  rendered.save(&buffer, "PNG");
  return "data:image/png;base64," + QString::fromLatin1(png.toBase64());
}
void MarkDocument::setDuration(double seconds) {
  seconds = std::isfinite(seconds) ? std::max(0., seconds) : 0.;
  if (qFuzzyCompare(seconds + 1, m_duration + 1))
    return;
  m_duration = seconds;
  emit changed();
}
void MarkDocument::setPlayhead(double seconds) {
  seconds = std::isfinite(seconds) ? std::max(0., seconds) : 0.;
  if (seconds == m_playhead)
    return;
  m_playhead = seconds;
  emit playheadChanged();
}
void MarkDocument::timeNewMark(Frame::Edit &edit) const {
  if (m_duration <= 0)
    return;
  // Hiding something covers the whole clip: a secret on screen for a moment
  // is easy to miss if the mark starts where it was drawn. Everything else
  // starts at the playhead. Both run to the end.
  // A mark drawn on the last frame still lasts the shortest allowed time.
  const bool hides = edit.type == "blur" || edit.type == "redact";
  edit.start = hides ? 0. : std::clamp(m_playhead, 0., std::max(0., m_duration - 0.1));
  edit.end = m_duration;
}
bool MarkDocument::showing(const Frame::Edit &edit) const {
  if (m_duration <= 0)
    return true;
  // A mark that runs to the end of the clip is still there on its last frame.
  const double end = edit.end < 0 ? m_duration : edit.end;
  return m_playhead >= edit.start && (m_playhead < end || end >= m_duration);
}
QVariantList MarkDocument::annotations() const {
  QVariantList list;
  for (int i = 0; i < m_edits.size(); ++i) {
    const auto &edit = m_edits[i];
    if (edit.type == "crop")
      continue;
    list.append(QVariantMap{{"index", i},
                            {"type", edit.type},
                            {"x1", std::min(edit.from.x(), edit.to.x())},
                            {"y1", std::min(edit.from.y(), edit.to.y())},
                            {"x2", std::max(edit.from.x(), edit.to.x())},
                            {"y2", std::max(edit.from.y(), edit.to.y())},
                            {"start", edit.start},
                            {"end", edit.end < 0 ? m_duration : edit.end},
                            // A video's saved check compares these, so a
                            // reworded label counts as a change.
                            {"text", edit.text},
                            {"color", edit.color.name(QColor::HexArgb)},
                            {"size", edit.size},
                            {"textStyle", edit.textStyle},
                            {"textAlign", edit.textAlign},
                            {"background", edit.background.name(QColor::HexArgb)},
                            {"backgroundOpacity", edit.backgroundOpacity},
                            {"outline", edit.outline}, {"arrowHead", edit.arrowHead},
                            {"filled", edit.filled}, {"opacity", edit.opacity},
                            {"numberColor", edit.numberColor.name(QColor::HexArgb)},
                            {"textBoxWidth", edit.textBox.width()},
                            {"textBoxHeight", edit.textBox.height()}});
  }
  return list;
}
void MarkDocument::setSelectedTimes(double start, double end) {
  if (locked() || m_duration <= 0 || m_selected < 0 ||
      m_selected >= m_edits.size() || !std::isfinite(start) ||
      !std::isfinite(end))
    return;
  start = std::clamp(start, 0., std::max(0., m_duration - 0.1));
  end = std::clamp(end, start + 0.1, m_duration);
  const auto &edit = m_edits.at(m_selected);
  const double currentEnd = edit.end < 0 ? m_duration : edit.end;
  if (qAbs(edit.start - start) < 0.0005 && qAbs(currentEnd - end) < 0.0005)
    return;
  saveHistory();
  m_edits[m_selected].start = start;
  m_edits[m_selected].end = end;
  commit();
}
void MarkDocument::commit(bool modified) {
  emit changed();
  emit edited(modified && !m_previewing);
}
static bool fitTextToImage(Frame::Edit &edit, const QImage &source,
                           QRectF room = {0, 0, 1, 1}) {
  if (edit.type != "text" || source.isNull())
    return false;
  const int requested = Frame::textPixelSize(edit, source);
  auto fits = [&source, &edit, room](int pixels) {
    Frame::Edit candidate = edit;
    candidate.size = Frame::textSizeForPixels(pixels, source);
    const QRectF bounds = Frame::annotationBounds(candidate, source);
    return bounds.width() <= room.width() && bounds.height() <= room.height();
  };
  if (!fits(requested)) {
    int low = 8, high = requested;
    while (low < high) {
      const int middle = (low + high + 1) / 2;
      if (fits(middle)) low = middle;
      else high = middle - 1;
    }
    edit.size = Frame::textSizeForPixels(low, source);
  }
  const QRectF bounds = Frame::annotationBounds(edit, source);
  double dx = 0., dy = 0.;
  if (bounds.width() <= room.width())
    dx = std::clamp(0., room.left() - bounds.left(), room.right() - bounds.right());
  else
    dx = room.left() - bounds.left();
  if (bounds.height() <= room.height())
    dy = std::clamp(0., room.top() - bounds.top(), room.bottom() - bounds.bottom());
  else
    dy = room.top() - bounds.top();
  edit.from += QPointF(dx, dy);
  edit.to = edit.from;
  return Frame::textPixelSize(edit, source) < requested;
}

void MarkDocument::edit(const QString &type, double x1, double y1,
                        double x2, double y2, const QString &text) {
  if (locked())
    return;
  if (!QStringList{"crop", "arrow", "line", "box", "ellipse", "highlight",
                   "redact", "blur", "text", "step"}.contains(type))
    return;
  QPointF a(std::clamp(x1, 0., 1.), std::clamp(y1, 0., 1.)),
      b(std::clamp(x2, 0., 1.), std::clamp(y2, 0., 1.));
  if (type == "text" && text.trimmed().isEmpty())
    return;
  if (type != "step" && type != "text" && QLineF(a, b).length() < 0.006)
    return;
  if (m_edits.size() >= MaxEdits && !(type == "crop" && hasCrop())) {
    emit message("This image has reached the 100-edit limit.");
    return;
  }
  if (type != "crop") {
    a = sourcePoint(a.x(), a.y());
    b = sourcePoint(b.x(), b.y());
  }
  saveHistory();
  if (type == "crop") {
    m_edits.removeIf([](const Frame::Edit &edit) { return edit.type == "crop"; });
    m_selected = -1;
  }
  Frame::Edit edit{type, a, b, text.left(240)};
  if (type == "text") {
    const auto style = labelDefaults();
    edit.color = QColor(style.value("color").toString());
    edit.background = QColor(style.value("background").toString());
    edit.backgroundOpacity = style.value("backgroundOpacity").toDouble();
    edit.textStyle = style.value("textStyle").toString();
    edit.textAlign = style.value("textAlign").toString();
    edit.size = Frame::textSizeForPixels(style.value("fontPx").toInt(), m_base);
    const QRectF room = cropBounds();
    if (hasCrop())
      edit.textBox.setWidth(std::min(room.width() * .85,
                                    Frame::annotationBounds(edit, m_base).width()));
    fitTextToImage(edit, m_base, room);
  } else applyToolDefaults(edit);
  timeNewMark(edit);
  orderNewStep(edit);
  m_edits.append(edit);
  if (type != "crop")
    m_selected = m_edits.size() - 1;
  emit message(type == "redact"
                   ? "Redaction applied. Exported pixels are fully replaced."
                   : "Edit applied. Undo is always available.");
  commit();
}
bool MarkDocument::cropCurrentView(double x1, double y1, double x2, double y2) {
  if (locked() || m_base.isNull() || !std::isfinite(x1) || !std::isfinite(y1) ||
      !std::isfinite(x2) || !std::isfinite(y2))
    return false;
  const QPointF a = sourcePoint(x1, y1), b = sourcePoint(x2, y2);
  if (a.x() == b.x() || a.y() == b.y())
    return false;
  const QRect current = Frame::cropPixels(m_base.size(), m_edits);
  const QRect pixels = Frame::cropPixels(m_base.size(), {{"crop", a, b}});
  // Invalid sub-two-pixel crops render the full source; reject that fallback
  // and pixel-equivalent crops without creating an undo step.
  if (!current.contains(pixels) || pixels == current)
    return false;
  const QRectF next(double(pixels.x()) / m_base.width(),
                    double(pixels.y()) / m_base.height(),
                    double(pixels.width()) / m_base.width(),
                    double(pixels.height()) / m_base.height());
  if (m_edits.size() >= MaxEdits && !hasCrop()) {
    emit message("This image has reached the 100-edit limit.");
    return false;
  }
  saveHistory();
  m_edits.removeIf([](const Frame::Edit &edit) { return edit.type == "crop"; });
  m_edits.append({"crop", next.topLeft(), next.bottomRight()});
  m_selected = -1;
  emit message("Crop applied. Undo restores the previous crop.");
  commit();
  return true;
}

void MarkDocument::redactAreas(const QVector<QRectF> &areas) {
  if (locked() || areas.isEmpty())
    return;
  const qsizetype room = MaxEdits - m_edits.size();
  if (room < areas.size()) {
    emit message("This image has reached the 100-edit limit.");
    return;
  }
  saveHistory();
  for (const QRectF &area : areas) {
    const QRectF inside = area.normalized().intersected(QRectF(0, 0, 1, 1));
    Frame::Edit edit{"redact", inside.topLeft(), inside.bottomRight()};
    timeNewMark(edit);
    m_edits.append(edit);
  }
  m_selected = -1;
  commit();
}
void MarkDocument::addStroke(const QVariantList &points) {
  if (locked() || points.size() < 2 || m_edits.size() >= MaxEdits)
    return;
  QVector<QPointF> path;
  path.reserve(points.size());
  double length = 0.;
  for (const QVariant &item : points) {
    const QVariantMap point = item.toMap();
    if (!point.contains("x") || !point.contains("y"))
      continue;
    const QPointF mapped = sourcePoint(point.value("x").toDouble(),
                                      point.value("y").toDouble());
    if (!path.isEmpty())
      length += QLineF(path.last(), mapped).length();
    path.append(mapped);
  }
  if (path.size() < 2 || length < 0.006)
    return;
  if (path.size() > 2048) {
    QVector<QPointF> sampled;
    sampled.reserve(2048);
    for (int i = 0; i < 2048; ++i)
      sampled.append(path[qRound64(double(i) * (path.size() - 1) / 2047)]);
    path = std::move(sampled);
  }
  double left = 1., right = 0., top = 1., bottom = 0.;
  for (const QPointF &point : path) {
    left = std::min(left, point.x()); right = std::max(right, point.x());
    top = std::min(top, point.y()); bottom = std::max(bottom, point.y());
  }
  saveHistory();
  Frame::Edit stroke{"pen", {left, top}, {right, bottom}};
  stroke.points = std::move(path);
  applyToolDefaults(stroke);
  timeNewMark(stroke);
  m_edits.append(stroke);
  m_selected = m_edits.size() - 1;
  emit message("Stroke added. Select it to move, resize, or change color.");
  commit();
}
void MarkDocument::saveHistory() {
  if (m_previewing)
    return;
  if (m_transform)
    endTransform(false);
  if (m_undoStates.size() >= 100)
    m_undoStates.removeFirst();
  m_undoStates.append({m_edits, m_selected});
  m_redoStates.clear();
}
QRectF MarkDocument::cropBounds() const {
  if (m_base.isNull()) return Frame::cropBounds(m_edits);
  // Match the actual screenshot pixels or even-pixel video viewport.
  const QRect pixels = m_duration > 0
      ? MarkConstraints::videoCropPixels(m_base.size(), Frame::cropBounds(m_edits))
      : Frame::cropPixels(m_base.size(), m_edits);
  return {double(pixels.x()) / m_base.width(),
          double(pixels.y()) / m_base.height(),
          double(pixels.width()) / m_base.width(),
          double(pixels.height()) / m_base.height()};
}
QPointF MarkDocument::sourcePoint(double x, double y) const {
  const QRectF crop = cropBounds();
  return {std::clamp(crop.x() + std::clamp(x, 0., 1.) * crop.width(), 0., 1.),
          std::clamp(crop.y() + std::clamp(y, 0., 1.) * crop.height(), 0., 1.)};
}
bool MarkDocument::hasCrop() const {
  return std::any_of(m_edits.begin(), m_edits.end(),
                     [](const Frame::Edit &edit) { return edit.type == "crop"; });
}
QVariantMap MarkDocument::selectedAnnotation() const {
  if (m_selected < 0 || m_selected >= m_edits.size())
    return {};
  const auto &edit = m_edits[m_selected];
  if (edit.type == "crop")
    return {};
  const QRectF crop = cropBounds();
  const QRectF bounds = Frame::annotationBounds(edit, m_base);
  int layer = 0, layers = 0;
  for (int i = 0; i < m_edits.size(); ++i) {
    if (m_edits[i].type == "crop")
      continue;
    ++layers;
    if (i <= m_selected)
      ++layer;
  }
  return {{"index", m_selected},
          {"type", edit.type},
          {"layer", layer},
          {"layers", layers},
          {"text", edit.text},
          {"color", edit.color.name()},
          {"size", edit.size},
          {"textBoxWidth", edit.textBox.width() / crop.width()},
          {"textBoxHeight", edit.textBox.height() / crop.height()},
          {"fontPx", edit.type == "text" ? Frame::textPixelSize(edit, m_base) : 0},
          {"textStyle", edit.textStyle},
          {"textAlign", edit.textAlign},
          {"background", edit.background.name()},
          {"backgroundOpacity", edit.backgroundOpacity},
          {"outline", edit.outline}, {"arrowHead", edit.arrowHead},
          {"filled", edit.filled}, {"opacity", edit.opacity},
          {"numberColor", edit.numberColor.name()},
          {"x1", (edit.from.x() - crop.x()) / crop.width()},
          {"y1", (edit.from.y() - crop.y()) / crop.height()},
          {"x2", (edit.to.x() - crop.x()) / crop.width()},
          {"y2", (edit.to.y() - crop.y()) / crop.height()},
          {"boundX", (bounds.x() - crop.x()) / crop.width()},
          {"boundY", (bounds.y() - crop.y()) / crop.height()},
          {"boundW", bounds.width() / crop.width()},
          {"boundH", bounds.height() / crop.height()},
          {"start", edit.start},
          {"end", edit.end < 0 ? m_duration : edit.end}};
}
int MarkDocument::hitIndex(double x, double y, bool edgesOnly) const {
  const QRectF crop = cropBounds();
  const QPointF point(std::clamp(x, 0., 1.), std::clamp(y, 0., 1.));
  const double tolerance = 0.018;
  for (int i = m_edits.size() - 1; i >= 0; --i) {
    const auto &edit = m_edits[i];
    if (edit.type == "crop" || !showing(edit))
      continue;
    const QPointF a((edit.from.x() - crop.x()) / crop.width(),
                    (edit.from.y() - crop.y()) / crop.height());
    const QPointF b((edit.to.x() - crop.x()) / crop.width(),
                    (edit.to.y() - crop.y()) / crop.height());
    bool hit = false;
    if (edit.type == "pen" && edit.points.size() >= 2) {
      for (qsizetype j = 1; j < edit.points.size() && !hit; ++j) {
        const QPointF first((edit.points[j - 1].x() - crop.x()) / crop.width(),
                            (edit.points[j - 1].y() - crop.y()) / crop.height());
        const QPointF second((edit.points[j].x() - crop.x()) / crop.width(),
                             (edit.points[j].y() - crop.y()) / crop.height());
        const QPointF segment = second - first;
        const double length2 = QPointF::dotProduct(segment, segment);
        const double t = length2 > 0
                             ? std::clamp(QPointF::dotProduct(point - first, segment) /
                                              length2,
                                          0., 1.)
                             : 0.;
        hit = QLineF(point, first + segment * t).length() <= tolerance;
      }
    } else if (edit.type == "line" || edit.type == "arrow") {
      const QPointF ab = b - a;
      const double length2 = QPointF::dotProduct(ab, ab);
      const double t = length2 > 0
                           ? std::clamp(QPointF::dotProduct(point - a, ab) /
                                            length2, 0., 1.)
                           : 0.;
      hit = QLineF(point, a + ab * t).length() <= tolerance;
    } else if (edit.type == "step" || edit.type == "text") {
      const QRectF bounds = Frame::annotationBounds(edit, m_base);
      const QRectF visible((bounds.x() - crop.x()) / crop.width(),
                           (bounds.y() - crop.y()) / crop.height(),
                           bounds.width() / crop.width(),
                           bounds.height() / crop.height());
      hit = visible.adjusted(-tolerance, -tolerance, tolerance, tolerance)
                .contains(point);
    } else {
      const QRectF area = QRectF(a, b).normalized();
      hit = area.adjusted(-tolerance, -tolerance, tolerance, tolerance)
                .contains(point) &&
            !(edgesOnly && area.width() > tolerance * 4 &&
              area.height() > tolerance * 4 &&
              area.adjusted(tolerance, tolerance, -tolerance, -tolerance)
                  .contains(point));
    }
    if (hit)
      return i;
  }
  return -1;
}
int MarkDocument::selectAt(double x, double y) {
  endTransform(false);
  const int found = hitIndex(x, y);
  if (found != m_selected) {
    m_selected = found;
    emit changed();
  }
  return m_selected;
}
void MarkDocument::select(int index) {
  endTransform(false);
  if (index < -1 || index >= m_edits.size() ||
      (index >= 0 && m_edits[index].type == "crop") || index == m_selected)
    return;
  m_selected = index;
  emit changed();
}
QVariantMap MarkDocument::hitAt(double x, double y, bool edgesOnly) const {
  const int found = hitIndex(x, y, edgesOnly);
  if (found < 0)
    return {};
  const QRectF crop = cropBounds();
  const QRectF bounds = Frame::annotationBounds(m_edits[found], m_base);
  return {{"index", found},
          {"type", m_edits[found].type},
          {"x", (bounds.x() - crop.x()) / crop.width()},
          {"y", (bounds.y() - crop.y()) / crop.height()},
          {"w", bounds.width() / crop.width()},
          {"h", bounds.height() / crop.height()}};
}
void MarkDocument::clearSelection() {
  endTransform(false);
  if (m_selected < 0)
    return;
  m_selected = -1;
  emit changed();
}
void MarkDocument::beginTextEdit() {
  endTransform(false);
  if (locked() || m_selected < 0 || m_selected >= m_edits.size() ||
      m_edits[m_selected].type != "text" || m_hiddenEdit == m_selected)
    return;
  m_hiddenEdit = m_selected;
  commit(false);
}
void MarkDocument::endTextEdit(const QString &text, bool apply) {
  if (m_hiddenEdit < 0)
    return;
  const int index = m_hiddenEdit;
  m_hiddenEdit = -1;
  // Typed text is applied even while a preview is still rendering, so a
  // quick click away never loses it.
  bool modified = false;
  if (apply && index < m_edits.size() && m_edits[index].type == "text") {
    if (text.trimmed().isEmpty()) {
      saveHistory();
      m_edits.removeAt(index);
      m_selected = -1;
      modified = true;
      emit message("Empty label removed.");
    } else if (m_edits[index].text != text.left(240)) {
      saveHistory();
      m_edits[index].text = text.left(240);
      const bool fitted = fitTextToImage(m_edits[index], m_base, cropBounds());
      modified = true;
      emit message(fitted ? "Text updated. Font size limited so the full label fits."
                          : "Text updated.");
    }
  }
  commit(modified);
}
void MarkDocument::beginTransform() {
  endTransform(false);
  if (!locked() && m_selected >= 0 && m_selected < m_edits.size())
    m_transform = EditState{m_edits, m_selected};
}
void MarkDocument::previewTransform(int handle, double x, double y) {
  if (!m_transform || locked() || m_selected != m_transform->selected)
    return;
  const auto previous = m_edits;
  m_edits = m_transform->edits;
  m_previewing = true;
  if (handle < 0) moveSelected(x, y);
  else resizeSelected(handle, x, y);
  // Returning exactly to the starting point must restore the rendered mark too.
  if (m_edits == m_transform->edits && previous != m_edits)
    commit(false);
  m_previewing = false;
}
namespace {
QVariantMap feedback(const MarkConstraints::Result &result) {
  return {{"valid", result.valid},
          {"label", result.valid ? result.label : QString{}},
          {"x1", result.anchor.x()},
          {"y1", result.anchor.y()},
          {"x2", result.point.x()},
          {"y2", result.point.y()}};
}
} // namespace
QVariantMap MarkDocument::creationPreview(const QString &type, double x1,
                                          double y1, double x2, double y2,
                                          double width, double height,
                                          bool constrain) const {
  return feedback(MarkConstraints::resolve(type, {x1, y1}, {x2, y2},
                                           {width, height}, constrain));
}
QVariantMap MarkDocument::previewConstrainedTransform(int handle, double x,
                                                      double y, double width,
                                                      double height,
                                                      bool constrain) {
  if (!m_transform || locked() || m_selected != m_transform->selected)
    return {};
  const auto &original = m_transform->edits[m_selected];
  const bool vector = original.type == "line" || original.type == "arrow";
  if (!constrain || handle < 0 ||
      (vector ? handle > 1
              : !MarkConstraints::shape(original.type) || handle > 3)) {
    previewTransform(handle, x, y);
    return {};
  }
  const QRectF crop = cropBounds();
  auto view = [crop](QPointF p) {
    return QPointF((p.x() - crop.x()) / crop.width(),
                   (p.y() - crop.y()) / crop.height());
  };
  const QRectF r = QRectF(view(original.from), view(original.to)).normalized();
  const QPointF anchor = vector
                             ? view(handle == 0 ? original.to : original.from)
                         : handle == 0 ? r.bottomRight()
                         : handle == 1 ? r.bottomLeft()
                         : handle == 2 ? r.topLeft()
                                       : r.topRight();
  // A degenerate box has no proportion to keep; fall back to a square.
  const double ratio = (r.width() * width) / (r.height() * height);
  const double aspect =
      vector || !std::isfinite(ratio) || ratio <= 0. ? 1. : ratio;
  const auto result = MarkConstraints::resolve(original.type, anchor, {x, y},
                                               {width, height}, true, aspect);
  if (result.valid)
    previewTransform(handle, result.point.x(), result.point.y());
  return feedback(result);
}
void MarkDocument::endTransform(bool apply) {
  if (!m_transform)
    return;
  const auto updated = m_edits;
  const auto original = *m_transform;
  m_transform.reset();
  m_edits = original.edits;
  m_selected = original.selected;
  if (apply && updated != m_edits) {
    saveHistory();
    m_edits = updated;
    commit();
  } else if (updated != m_edits) {
    commit(false);
  }
}
void MarkDocument::moveSelected(double dx, double dy) {
  if (locked() || m_selected < 0 || m_selected >= m_edits.size())
    return;
  const auto edit = m_edits.at(m_selected);
  const QRectF crop = cropBounds();
  const QRectF bounds = Frame::annotationBounds(edit, m_base);
  const bool fitBounds =
      (edit.type == "text" || edit.type == "step") &&
      bounds.width() <= 1. && bounds.height() <= 1.;
  const double left = fitBounds ? bounds.left()
                                : std::min(edit.from.x(), edit.to.x());
  const double right = fitBounds ? bounds.right()
                                 : std::max(edit.from.x(), edit.to.x());
  const double top = fitBounds ? bounds.top()
                               : std::min(edit.from.y(), edit.to.y());
  const double bottom = fitBounds ? bounds.bottom()
                                  : std::max(edit.from.y(), edit.to.y());
  dx = std::clamp(dx * crop.width(), -left, 1. - right);
  dy = std::clamp(dy * crop.height(), -top, 1. - bottom);
  if (qFuzzyIsNull(dx) && qFuzzyIsNull(dy))
    return;
  saveHistory();
  m_edits[m_selected].from += QPointF(dx, dy);
  m_edits[m_selected].to += QPointF(dx, dy);
  for (QPointF &point : m_edits[m_selected].points)
    point += QPointF(dx, dy);
  commit();
}
void MarkDocument::nudgeSelected(int dx, int dy) {
  if (m_base.isNull() || (dx == 0 && dy == 0))
    return;
  const QRectF crop = cropBounds();
  moveSelected(double(dx) / (m_base.width() * crop.width()),
               double(dy) / (m_base.height() * crop.height()));
}
void MarkDocument::resizeSelected(int handle, double x, double y) {
  if (locked() || m_selected < 0 || m_selected >= m_edits.size())
    return;
  const auto edit = m_edits.at(m_selected);
  if (edit.type == "crop")
    return;
  const QPointF point = sourcePoint(x, y);
  if (edit.type == "text" || edit.type == "step") {
    if (handle < 0 || handle > 7)
      return;
    const QRectF bounds = Frame::annotationBounds(edit, m_base);
    if (edit.type == "text" && handle >= 4) {
      Frame::Edit updated = edit;
      QRectF box = bounds;
      const double minWidth = std::min(1., (Frame::textPixelSize(edit, m_base) * 2. +
                                          std::max(8., Frame::textPixelSize(edit, m_base) * 0.54)) / m_base.width());
      if (handle == 4) box.setTop(std::min(point.y(), box.bottom() - 8. / m_base.height()));
      if (handle == 5) box.setRight(std::max(point.x(), box.left() + minWidth));
      if (handle == 6) box.setBottom(std::max(point.y(), box.top() + 8. / m_base.height()));
      if (handle == 7) box.setLeft(std::min(point.x(), box.right() - minWidth));
      box = box.intersected(QRectF(0, 0, 1, 1));
      updated.textBox = box.size();
      // Stop narrowing before wrapping would force the font to shrink.
      if (Frame::annotationBounds(updated, m_base).height() > 1.) {
        double low = updated.textBox.width(), high = 1.;
        for (int i = 0; i < 16; ++i) {
          const double middle = (low + high) / 2.;
          updated.textBox.setWidth(middle);
          if (Frame::annotationBounds(updated, m_base).height() <= 1.) high = middle;
          else low = middle;
        }
        updated.textBox.setWidth(high);
        if (handle == 7) box.moveLeft(std::max(0., bounds.right() - high));
      }
      updated.from = box.topLeft();
      updated.to = updated.from;
      // Keep every word visible: height cannot shrink below the laid-out text.
      const QRectF laidOut = Frame::annotationBounds(updated, m_base);
      if (handle == 4)
        updated.from.setY(std::max(0., bounds.bottom() - laidOut.height()));
      updated.to = updated.from;
      fitTextToImage(updated, m_base, cropBounds());
      if (updated.textBox == edit.textBox && updated.from == edit.from)
        return;
      saveHistory();
      m_edits[m_selected] = updated;
      commit();
      return;
    }
    const QPointF opposite = edit.type == "step" ? edit.from
        : handle == 0 ? bounds.bottomRight()
        : handle == 1 ? bounds.bottomLeft()
        : handle == 2 ? bounds.topLeft() : bounds.topRight();
    const QPointF corner = handle == 0 ? bounds.topLeft()
        : handle == 1 ? bounds.topRight()
        : handle == 2 ? bounds.bottomRight()
        : handle == 3 ? bounds.bottomLeft()
        : handle == 4 ? QPointF(bounds.center().x(), bounds.top())
        : handle == 5 ? QPointF(bounds.right(), bounds.center().y())
        : handle == 6 ? QPointF(bounds.center().x(), bounds.bottom())
                      : QPointF(bounds.left(), bounds.center().y());
    const QPointF scale(m_base.width(), m_base.height());
    const QPointF oldVector((corner.x() - opposite.x()) * scale.x(),
                            (corner.y() - opposite.y()) * scale.y());
    const QPointF newVector((point.x() - opposite.x()) * scale.x(),
                            (point.y() - opposite.y()) * scale.y());
    const double oldLength2 = QPointF::dotProduct(oldVector, oldVector);
    if (oldLength2 <= 0)
      return;
    const double minimum = edit.type == "text"
                               ? Frame::textSizeForPixels(8, m_base)
                               : 0.5;
    const double maximum = edit.type == "text"
                               ? Frame::textSizeForPixels(4096, m_base)
                               : 8.0;
    const double next = std::clamp(edit.size *
                                       QPointF::dotProduct(oldVector, newVector) /
                                       oldLength2,
                                   minimum, maximum);
    if (qAbs(next - edit.size) < 0.01)
      return;
    Frame::Edit updated = edit;
    updated.size = next;
    if (edit.type == "text") {
      const double ratio = next / edit.size;
      updated.textBox = {std::min(1., edit.textBox.width() * ratio),
                         std::min(1., edit.textBox.height() * ratio)};
    }
    if (edit.type == "text" && handle != 2) {
      const QRectF resized = Frame::annotationBounds(updated, m_base);
      QPointF anchor = handle == 0 ? opposite - QPointF(resized.width(), resized.height())
                       : handle == 1 ? opposite - QPointF(0, resized.height())
                                     : opposite - QPointF(resized.width(), 0);
      updated.from = QPointF(std::clamp(anchor.x(), 0., 1.),
                             std::clamp(anchor.y(), 0., 1.));
      updated.to = updated.from;
    }
    const bool fitted = fitTextToImage(updated, m_base, cropBounds());
    saveHistory();
    m_edits[m_selected] = updated;
    if (fitted)
      emit message("Font size limited so the full label fits.");
    commit();
    return;
  }
  QPointF a = edit.from, b = edit.to;
  if (edit.type == "line" || edit.type == "arrow") {
    if (handle == 0) a = point;
    else if (handle == 1) b = point;
    else return;
  } else {
    const QRectF rect(a, b);
    const QRectF r = rect.normalized();
    if (handle == 0) { a = point; b = r.bottomRight(); }
    else if (handle == 1) { a = {r.left(), point.y()}; b = {point.x(), r.bottom()}; }
    else if (handle == 2) { a = r.topLeft(); b = point; }
    else if (handle == 3) { a = {point.x(), r.top()}; b = {r.right(), point.y()}; }
    else if (handle == 4) { a = {r.left(), std::min(point.y(), r.bottom() - 1. / m_base.height())}; b = r.bottomRight(); }
    else if (handle == 5) { a = r.topLeft(); b = {std::max(point.x(), r.left() + 1. / m_base.width()), r.bottom()}; }
    else if (handle == 6) { a = r.topLeft(); b = {r.right(), std::max(point.y(), r.top() + 1. / m_base.height())}; }
    else if (handle == 7) { a = {std::min(point.x(), r.right() - 1. / m_base.width()), r.top()}; b = r.bottomRight(); }
    else return;
  }
  if (QLineF(a, b).length() < 0.006 || (a == edit.from && b == edit.to))
    return;
  saveHistory();
  if (edit.type == "pen") {
    const QRectF sourceBounds = QRectF(edit.from, edit.to).normalized();
    const QRectF targetBounds = QRectF(a, b).normalized();
    for (QPointF &pathPoint : m_edits[m_selected].points) {
      const double nx = sourceBounds.width() > 1e-8
                            ? (pathPoint.x() - sourceBounds.left()) / sourceBounds.width()
                            : 0.5;
      const double ny = sourceBounds.height() > 1e-8
                            ? (pathPoint.y() - sourceBounds.top()) / sourceBounds.height()
                            : 0.5;
      pathPoint = {targetBounds.left() + nx * targetBounds.width(),
                   targetBounds.top() + ny * targetBounds.height()};
    }
  }
  m_edits[m_selected].from = a;
  m_edits[m_selected].to = b;
  commit();
}
void MarkDocument::deleteSelected() {
  if (locked() || m_selected < 0 || m_selected >= m_edits.size())
    return;
  m_hiddenEdit = -1;
  saveHistory();
  m_edits.removeAt(m_selected);
  m_selected = -1;
  commit();
}
static void translate(Frame::Edit &edit, QPointF by) {
  edit.from += by;
  edit.to += by;
  for (QPointF &point : edit.points)
    point += by;
}
void MarkDocument::orderNewStep(Frame::Edit &edit) const {
  if (edit.type != "step") return;
  int order = 0;
  for (const auto &existing : m_edits)
    if (existing.type == "step") order = std::max(order, existing.stepOrder);
  edit.stepOrder = order + 1;
}
static double offsetInside(double wanted, double first, double last,
                            double roomFirst, double roomLast) {
  if (last - first > roomLast - roomFirst) return 0.;
  return std::clamp(wanted, roomFirst - first, roomLast - last);
}
void MarkDocument::appendOffset(Frame::Edit edit) {
  const QRectF bounds = Frame::annotationBounds(edit, m_base);
  const double stepX = 12. / std::max(1, m_base.width());
  const double stepY = 12. / std::max(1, m_base.height());
  const QRectF room = cropBounds();
  const double dx = offsetInside(bounds.right() + stepX <= room.right() ? stepX : -stepX,
                                 bounds.left(), bounds.right(), room.left(), room.right());
  const double dy = offsetInside(bounds.bottom() + stepY <= room.bottom() ? stepY : -stepY,
                                 bounds.top(), bounds.bottom(), room.top(), room.bottom());
  translate(edit, {dx, dy});
  orderNewStep(edit);
  saveHistory();
  m_edits.append(edit);
  m_selected = m_edits.size() - 1;
}
void MarkDocument::duplicateSelected() {
  if (locked() || m_selected < 0 || m_selected >= m_edits.size() ||
      m_edits.size() >= MaxEdits || m_edits[m_selected].type == "crop")
    return;
  appendOffset(m_edits[m_selected]);
  emit message("Annotation duplicated. Drag it to place it.");
  commit();
}
bool MarkDocument::copySelected() {
  if (m_selected < 0 || m_selected >= m_edits.size() ||
      m_edits[m_selected].type == "crop")
    return false;
  m_copied = m_edits[m_selected];
  // Pastes step toward the side with more room, so a row of them does not
  // fold back onto the copy at an edge.
  const QRectF bounds = Frame::annotationBounds(*m_copied, m_base);
  m_pasteStep = {(bounds.center().x() <= cropBounds().center().x() ? 12. : -12.) / std::max(1, m_base.width()),
                 (bounds.center().y() <= cropBounds().center().y() ? 12. : -12.) / std::max(1, m_base.height())};
  m_pastes = 0;
  emit message("Annotation copied. Press Ctrl+V to paste it.");
  emit changed();
  return true;
}
void MarkDocument::cutSelected() {
  if (locked() || !copySelected())
    return;
  deleteSelected();
  emit message("Annotation cut. Press Ctrl+V to paste it.");
}
void MarkDocument::paste() {
  if (locked() || !m_copied)
    return;
  if (m_edits.size() >= MaxEdits) {
    emit message("This image has reached the 100-edit limit.");
    return;
  }
  Frame::Edit edit = *m_copied;
  // Each paste is a step further from the copy, stopping at the crop's edge.
  const QRectF bounds = Frame::annotationBounds(edit, m_base);
  const QPointF wanted = m_pasteStep * (m_pastes + 1);
  const QRectF room = cropBounds();
  const QPointF shift(offsetInside(wanted.x(), bounds.left(), bounds.right(),
                                    room.left(), room.right()),
                      offsetInside(wanted.y(), bounds.top(), bounds.bottom(),
                                    room.top(), room.bottom()));
  translate(edit, shift);
  if (m_duration > 0) {
    const double end = edit.end < 0 ? m_duration : edit.end;
    if (edit.type == "blur" || edit.type == "redact") {
      // A cover keeps its times so it never starts after what it hides,
      // widened to the playhead so the paste can be seen and placed.
      edit.start = std::min(edit.start, std::clamp(m_playhead, 0., std::max(0., m_duration - 0.1)));
      edit.end = std::max(end, std::min(m_duration, m_playhead + 0.1));
    } else {
      // Anything else shows for as long as the copy did, from the playhead,
      // and one that ran to the end still does.
      const double length = end - edit.start;
      edit.start = std::clamp(m_playhead, 0., std::max(0., m_duration - 0.1));
      edit.end = end >= m_duration ? m_duration
                                   : std::clamp(edit.start + length, edit.start + 0.1, m_duration);
    }
  }
  ++m_pastes;
  orderNewStep(edit);
  saveHistory();
  m_edits.append(edit);
  m_selected = m_edits.size() - 1;
  emit message("Annotation pasted. Drag it to place it.");
  commit();
}
void MarkDocument::moveSelectedLayer(int direction) {
  if (locked() || m_selected < 0 || m_selected >= m_edits.size() ||
      m_edits[m_selected].type == "crop" || (direction != -1 && direction != 1))
    return;
  int target = m_selected + direction;
  while (target >= 0 && target < m_edits.size() && m_edits[target].type == "crop")
    target += direction;
  if (target < 0 || target >= m_edits.size())
    return;
  saveHistory();
  std::swap(m_edits[m_selected], m_edits[target]);
  m_selected = target;
  emit message(direction > 0 ? "Annotation moved forward." : "Annotation moved back.");
  commit();
}
void MarkDocument::updateSelectedText(const QString &text) {
  if (locked() || m_selected < 0 || m_selected >= m_edits.size() ||
      m_edits.at(m_selected).type != "text" || text.trimmed().isEmpty())
    return;
  const QString updated = text.left(240);
  if (m_edits.at(m_selected).text == updated)
    return;
  saveHistory();
  m_edits[m_selected].text = updated;
  const bool fitted = fitTextToImage(m_edits[m_selected], m_base, cropBounds());
  emit message(fitted ? "Text updated. Font size limited so the full label fits."
                      : "Text updated.");
  commit();
}
void MarkDocument::setSelectedColor(const QString &color) {
  if (locked() || m_selected < 0 || m_selected >= m_edits.size())
    return;
  const QString type = m_edits.at(m_selected).type;
  if (!QStringList{"arrow", "line", "box", "ellipse", "step", "text", "pen"}
           .contains(type))
    return;
  const QColor parsed(color);
  if (!parsed.isValid() || m_edits.at(m_selected).color == parsed)
    return;
  saveHistory();
  m_edits[m_selected].color = parsed;
  commit();
}
void MarkDocument::setLabelStyle(const QVariantMap &style) {
  if (locked() || style.isEmpty())
    return;
  QVariantMap fields;
  for (auto it = style.begin(); it != style.end(); ++it) {
    const QString key = it.key();
    if (key == "color" || key == "background") {
      const QColor color(it.value().toString());
      if (!color.isValid()) return;
      fields[key] = color.name();
    } else if (key == "backgroundOpacity") {
      bool valid = false;
      const double opacity = it.value().toDouble(&valid);
      if (!valid || !std::isfinite(opacity) || opacity < 0 || opacity > 1) return;
      fields[key] = opacity;
    } else if (key == "fontPx") {
      bool valid = false;
      const int pixels = it.value().toInt(&valid);
      if (!valid || (pixels != 0 && (pixels < 8 || pixels > 4096))) return;
      fields[key] = pixels;
    } else if (key == "textStyle") {
      if (!QStringList{"box", "shadow"}.contains(it.value().toString())) return;
      fields[key] = it.value();
    } else if (key == "textAlign") {
      if (!QStringList{"left", "center", "right"}.contains(it.value().toString())) return;
      fields[key] = it.value();
    }
  }
  if (fields.isEmpty()) return;
  endTransform(false);
  const bool selected = m_selected >= 0 && m_selected < m_edits.size() &&
                        m_edits[m_selected].type == "text";
  bool modified = false;
  if (selected) {
    Frame::Edit updated = m_edits[m_selected];
    if (fields.contains("color")) updated.color = QColor(fields["color"].toString());
    if (fields.contains("background")) updated.background = QColor(fields["background"].toString());
    if (fields.contains("backgroundOpacity")) updated.backgroundOpacity = fields["backgroundOpacity"].toDouble();
    if (fields.contains("textStyle")) updated.textStyle = fields["textStyle"].toString();
    if (fields.contains("textAlign")) updated.textAlign = fields["textAlign"].toString();
    if (fields.contains("fontPx")) {
      const int pixels = fields["fontPx"].toInt();
      updated.size = Frame::textSizeForPixels(pixels ? pixels : newTextPixels(), m_base);
      fitTextToImage(updated, m_base, cropBounds());
    }
    if (updated != m_edits[m_selected]) {
      saveHistory();
      m_edits[m_selected] = updated;
      modified = true;
    }
  }
  QSettings settings;
  settings.beginGroup("labels");
  for (auto it = fields.begin(); it != fields.end(); ++it)
    settings.setValue(it.key(), it.value());
  // Default-only choices never dirty the current image or enter its undo history.
  if (modified) commit();
  else emit changed();
}
void MarkDocument::resetLabelStyle() {
  setLabelStyle({{"color", "#ffffff"}, {"background", "#151a20"},
                 {"backgroundOpacity", 1.}, {"textStyle", "box"},
                 {"textAlign", "center"}, {"fontPx", 0}});
}
void MarkDocument::setSelectedSize(double size) {
  if (locked() || m_selected < 0 || m_selected >= m_edits.size())
    return;
  const QString type = m_edits.at(m_selected).type;
  if (!QStringList{"arrow", "line", "box", "ellipse", "step", "text", "pen", "blur"}
           .contains(type) || !std::isfinite(size))
    return;
  const double next = type == "text"
                          ? std::clamp(size,
                                       Frame::textSizeForPixels(8, m_base),
                                       Frame::textSizeForPixels(4096, m_base))
                          : std::clamp(size, 0.5, 8.0);
  if (qAbs(m_edits.at(m_selected).size - next) < 0.01)
    return;
  saveHistory();
  m_edits[m_selected].size = next;
  const bool fitted = fitTextToImage(m_edits[m_selected], m_base, cropBounds());
  if (fitted)
    emit message("Font size limited so the full label fits.");
  commit();
}
void MarkDocument::setSelectedFontPixels(int pixels) {
  if (m_selected < 0 || m_selected >= m_edits.size() ||
      m_edits.at(m_selected).type != "text")
    return;
  setSelectedSize(Frame::textSizeForPixels(pixels, m_base));
}
void MarkDocument::setSelectedTextStyle(const QString &style) {
  if (locked() || m_selected < 0 || m_selected >= m_edits.size() ||
      m_edits.at(m_selected).type != "text" ||
      (style != "box" && style != "shadow") ||
      m_edits.at(m_selected).textStyle == style)
    return;
  saveHistory();
  m_edits[m_selected].textStyle = style;
  commit();
}
void MarkDocument::setSelectedTextAlignment(const QString &alignment) {
  if (locked() || m_selected < 0 || m_selected >= m_edits.size() ||
      m_edits.at(m_selected).type != "text" ||
      !QStringList{"left", "center", "right"}.contains(alignment) ||
      m_edits.at(m_selected).textAlign == alignment)
    return;
  saveHistory();
  m_edits[m_selected].textAlign = alignment;
  commit();
}
void MarkDocument::setSelectedBackground(const QString &color) {
  if (locked() || m_selected < 0 || m_selected >= m_edits.size() ||
      m_edits.at(m_selected).type != "text")
    return;
  const QColor parsed(color);
  if (!parsed.isValid() || parsed == m_edits.at(m_selected).background)
    return;
  saveHistory();
  m_edits[m_selected].background = parsed;
  commit();
}
void MarkDocument::setSelectedBackgroundOpacity(double opacity) {
  if (locked() || m_selected < 0 || m_selected >= m_edits.size() ||
      m_edits.at(m_selected).type != "text" || !std::isfinite(opacity))
    return;
  const double next = std::clamp(opacity, 0.0, 1.0);
  if (qAbs(next - m_edits.at(m_selected).backgroundOpacity) < 0.01)
    return;
  saveHistory();
  m_edits[m_selected].backgroundOpacity = next;
  commit();
}
void MarkDocument::clearCrop() {
  if (locked() || !hasCrop())
    return;
  saveHistory();
  m_edits.removeIf([](const Frame::Edit &edit) { return edit.type == "crop"; });
  m_selected = -1;
  commit();
}
void MarkDocument::undo() {
  endTransform(false);
  if (locked() || m_undoStates.isEmpty())
    return;
  m_hiddenEdit = -1;
  m_redoStates.append({m_edits, m_selected});
  const EditState previous = m_undoStates.takeLast();
  m_edits = previous.edits;
  m_selected = previous.selected;
  commit();
}
void MarkDocument::redo() {
  endTransform(false);
  if (locked() || m_redoStates.isEmpty())
    return;
  m_hiddenEdit = -1;
  m_undoStates.append({m_edits, m_selected});
  const EditState next = m_redoStates.takeLast();
  m_edits = next.edits;
  m_selected = next.selected;
  commit();
}
void MarkDocument::resetEdits() {
  if (locked() || m_edits.isEmpty())
    return;
  saveHistory();
  m_edits.clear();
  m_selected = -1;
  commit();
}
