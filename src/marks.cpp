#include "marks.hpp"
#include <QLineF>
#include <cmath>

void MarkDocument::reset(const QImage &base) {
  m_base = base;
  m_edits.clear();
  m_undoStates.clear();
  m_redoStates.clear();
  m_selected = m_hiddenEdit = -1;
  emit changed();
}
void MarkDocument::restore(const QImage &base, QVector<Frame::Edit> edits,
                           int selected) {
  m_base = base;
  m_edits = std::move(edits);
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
void MarkDocument::commit(bool modified) {
  emit changed();
  emit edited(modified);
}
static bool fitTextToImage(Frame::Edit &edit, const QImage &source) {
  if (edit.type != "text" || source.isNull())
    return false;
  const int requested = Frame::textPixelSize(edit, source);
  auto fits = [&source, &edit](int pixels) {
    Frame::Edit candidate = edit;
    candidate.size = Frame::textSizeForPixels(pixels, source);
    const QRectF bounds = Frame::annotationBounds(candidate, source);
    return bounds.width() <= 1. && bounds.height() <= 1.;
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
  if (bounds.width() <= 1.)
    dx = std::clamp(1. - bounds.right(), -bounds.left(), 0.);
  else
    dx = -bounds.left();
  if (bounds.height() <= 1.)
    dy = std::clamp(1. - bounds.bottom(), -bounds.top(), 0.);
  else
    dy = -bounds.top();
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
    edit.color = Qt::white;
    edit.size = Frame::textSizeForPixels(newTextPixels(), m_base);
    fitTextToImage(edit, m_base);
  }
  m_edits.append(edit);
  if (type != "crop")
    m_selected = m_edits.size() - 1;
  emit message(type == "redact"
                   ? "Redaction applied. Exported pixels are fully replaced."
                   : "Edit applied. Undo is always available.");
  commit();
}
void MarkDocument::addStroke(const QVariantList &points) {
  if (locked() || points.size() < 2 || m_edits.size() >= MaxEdits)
    return;
  QVector<QPointF> path;
  path.reserve(std::min<qsizetype>(points.size(), 2048));
  double length = 0.;
  for (const QVariant &item : points) {
    if (path.size() >= 2048)
      break;
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
  double left = 1., right = 0., top = 1., bottom = 0.;
  for (const QPointF &point : path) {
    left = std::min(left, point.x()); right = std::max(right, point.x());
    top = std::min(top, point.y()); bottom = std::max(bottom, point.y());
  }
  saveHistory();
  Frame::Edit stroke{"pen", {left, top}, {right, bottom}};
  stroke.points = std::move(path);
  m_edits.append(stroke);
  m_selected = m_edits.size() - 1;
  emit message("Stroke added. Select it to move, resize, or change color.");
  commit();
}
void MarkDocument::saveHistory() {
  if (m_undoStates.size() >= 100)
    m_undoStates.removeFirst();
  m_undoStates.append({m_edits, m_selected});
  m_redoStates.clear();
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
  return {{"type", edit.type},
          {"layer", layer},
          {"layers", layers},
          {"text", edit.text},
          {"color", edit.color.name()},
          {"size", edit.size},
          {"fontPx", edit.type == "text" ? Frame::textPixelSize(edit, m_base) : 0},
          {"textStyle", edit.textStyle},
          {"textAlign", edit.textAlign},
          {"background", edit.background.name()},
          {"backgroundOpacity", edit.backgroundOpacity},
          {"x1", (edit.from.x() - crop.x()) / crop.width()},
          {"y1", (edit.from.y() - crop.y()) / crop.height()},
          {"x2", (edit.to.x() - crop.x()) / crop.width()},
          {"y2", (edit.to.y() - crop.y()) / crop.height()},
          {"boundX", (bounds.x() - crop.x()) / crop.width()},
          {"boundY", (bounds.y() - crop.y()) / crop.height()},
          {"boundW", bounds.width() / crop.width()},
          {"boundH", bounds.height() / crop.height()}};
}
int MarkDocument::hitIndex(double x, double y, bool edgesOnly) const {
  const QRectF crop = cropBounds();
  const QPointF point(std::clamp(x, 0., 1.), std::clamp(y, 0., 1.));
  const double tolerance = 0.018;
  for (int i = m_edits.size() - 1; i >= 0; --i) {
    const auto &edit = m_edits[i];
    if (edit.type == "crop")
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
  const int found = hitIndex(x, y);
  if (found != m_selected) {
    m_selected = found;
    emit changed();
  }
  return m_selected;
}
void MarkDocument::select(int index) {
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
  if (m_selected < 0)
    return;
  m_selected = -1;
  emit changed();
}
void MarkDocument::beginTextEdit() {
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
      const bool fitted = fitTextToImage(m_edits[index], m_base);
      modified = true;
      emit message(fitted ? "Text updated. Font size limited so the full label fits."
                          : "Text updated.");
    }
  }
  commit(modified);
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
    if (handle < 0 || handle > 3)
      return;
    const QRectF bounds = Frame::annotationBounds(edit, m_base);
    const QPointF opposite = edit.type == "step" ? edit.from
        : handle == 0 ? bounds.bottomRight()
        : handle == 1 ? bounds.bottomLeft()
        : handle == 2 ? bounds.topLeft() : bounds.topRight();
    const QPointF corner = handle == 0 ? bounds.topLeft()
        : handle == 1 ? bounds.topRight()
        : handle == 2 ? bounds.bottomRight() : bounds.bottomLeft();
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
    if (edit.type == "text" && handle != 2) {
      const QRectF resized = Frame::annotationBounds(updated, m_base);
      QPointF anchor = handle == 0 ? opposite - QPointF(resized.width(), resized.height())
                       : handle == 1 ? opposite - QPointF(0, resized.height())
                                     : opposite - QPointF(resized.width(), 0);
      updated.from = QPointF(std::clamp(anchor.x(), 0., 1.),
                             std::clamp(anchor.y(), 0., 1.));
      updated.to = updated.from;
    }
    const bool fitted = fitTextToImage(updated, m_base);
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
void MarkDocument::duplicateSelected() {
  if (locked() || m_selected < 0 || m_selected >= m_edits.size() ||
      m_edits.size() >= MaxEdits || m_edits[m_selected].type == "crop")
    return;
  Frame::Edit copy = m_edits[m_selected];
  const QRectF bounds = Frame::annotationBounds(copy, m_base);
  const double stepX = 12. / std::max(1, m_base.width());
  const double stepY = 12. / std::max(1, m_base.height());
  const double dx = bounds.right() + stepX <= 1. ? stepX
                      : bounds.left() - stepX >= 0. ? -stepX : 0.;
  const double dy = bounds.bottom() + stepY <= 1. ? stepY
                      : bounds.top() - stepY >= 0. ? -stepY : 0.;
  copy.from += QPointF(dx, dy);
  copy.to += QPointF(dx, dy);
  for (QPointF &point : copy.points)
    point += QPointF(dx, dy);
  saveHistory();
  m_edits.append(copy);
  m_selected = m_edits.size() - 1;
  emit message("Annotation duplicated. Drag it to place it.");
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
  const bool fitted = fitTextToImage(m_edits[m_selected], m_base);
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
  const bool fitted = fitTextToImage(m_edits[m_selected], m_base);
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
  if (style == "shadow" && m_edits[m_selected].color == QColor(Qt::white))
    m_edits[m_selected].color = QColor("#e75439");
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
