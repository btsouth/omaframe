#include "studio.hpp"
#include "capture-session.hpp"
#include "capture.hpp"
#include "displays.hpp"
#include "window-targets.hpp"
#include <QBuffer>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineF>
#include <QLocale>
#include <QMutexLocker>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QWindow>
#include <QtConcurrent>
#include <cmath>
#include <optional>

static QString originalsDirectory() {
  return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
         "/originals";
}
static QString draftsDirectory() {
  return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
         "/drafts";
}
static constexpr qint64 maxDraftMetadataBytes = 32 * 1024 * 1024;
static bool validDraftId(const QString &id) {
  static const QRegularExpression format("^[0-9a-f]{32}$");
  return format.match(id).hasMatch();
}
static QJsonObject editToJson(const Frame::Edit &edit) {
  QJsonArray points;
  for (const auto &point : edit.points)
    points.append(QJsonObject{{"x", point.x()}, {"y", point.y()}});
  return {{"type", edit.type},
          {"x1", edit.from.x()}, {"y1", edit.from.y()},
          {"x2", edit.to.x()}, {"y2", edit.to.y()},
          {"text", edit.text}, {"color", edit.color.name(QColor::HexArgb)},
          {"size", edit.size}, {"textStyle", edit.textStyle},
          {"textAlign", edit.textAlign},
          {"background", edit.background.name(QColor::HexArgb)},
          {"backgroundOpacity", edit.backgroundOpacity}, {"points", points}};
}
static std::optional<Frame::Edit> editFromJson(const QJsonObject &item) {
  const QString type = item.value("type").toString();
  if (!QStringList{"crop", "arrow", "line", "box", "ellipse", "highlight",
                   "redact", "blur", "pen", "step", "text"}.contains(type))
    return std::nullopt;
  const double x1 = item.value("x1").toDouble(), y1 = item.value("y1").toDouble();
  const double x2 = item.value("x2").toDouble(), y2 = item.value("y2").toDouble();
  const double size = item.value("size").toDouble(1);
  const double opacity = item.value("backgroundOpacity").toDouble(1);
  for (double value : {x1, y1, x2, y2})
    if (!std::isfinite(value) || qAbs(value) > 10)
      return std::nullopt;
  if (!std::isfinite(size) || size <= 0 || size > 10000 ||
      !std::isfinite(opacity) || opacity < 0 || opacity > 1)
    return std::nullopt;
  Frame::Edit edit{type, {x1, y1}, {x2, y2}, item.value("text").toString().left(240)};
  edit.color = QColor(item.value("color").toString());
  edit.background = QColor(item.value("background").toString());
  if (!edit.color.isValid() || !edit.background.isValid())
    return std::nullopt;
  edit.size = size;
  edit.textStyle = item.value("textStyle").toString("box");
  edit.textAlign = item.value("textAlign").toString("center");
  edit.backgroundOpacity = opacity;
  const QJsonArray points = item.value("points").toArray();
  if (points.size() > 2048)
    return std::nullopt;
  for (const auto &value : points) {
    const auto point = value.toObject();
    const double x = point.value("x").toDouble(), y = point.value("y").toDouble();
    if (!std::isfinite(x) || !std::isfinite(y) || qAbs(x) > 10 || qAbs(y) > 10)
      return std::nullopt;
    edit.points.append({x, y});
  }
  return edit;
}
static QString summarizeOriginals(int *count) {
  const auto files = QDir(originalsDirectory()).entryInfoList(
      {"*.png"}, QDir::Files | QDir::NoSymLinks);
  *count = files.size();
  if (files.isEmpty())
    return "No private originals saved.";
  qint64 bytes = 0;
  for (const auto &file : files)
    bytes += file.size();
  return QString("%1 private originals · %2 · kept until cleared")
      .arg(files.size())
      .arg(QLocale().formattedDataSize(bytes));
}

QImage ImageStore::requestImage(const QString &id, QSize *size,
                                const QSize &requested) {
  QMutexLocker lock(&mutex);
  QImage image = images.value(id.section('?', 0, 0));
  if (size)
    *size = image.size();
  return requested.isValid() ? image.scaled(requested, Qt::KeepAspectRatio,
                                            Qt::SmoothTransformation)
                             : image;
}
void ImageStore::put(const QString &name, const QImage &image) {
  QMutexLocker lock(&mutex);
  images.insert(name, image);
}

Studio::Studio(ImageStore *store, bool withDemo) : m_store(store) {
  QSettings settings;
  m_options.style = std::clamp(settings.value("style", 0).toInt(), 0, 8);
  double savedPadding = settings.value("padding", 0.05).toDouble();
  if (settings.value("paddingVersion", 1).toInt() < 2) {
    // Style changes also saved the old default. Keep other saved values.
    if (qFuzzyCompare(savedPadding, 0.09)) {
      savedPadding = 0.05;
      settings.setValue("padding", savedPadding);
    }
    settings.setValue("paddingVersion", 2);
  }
  m_options.padding = std::clamp(savedPadding, 0.02, 0.22);
  m_options.aspect = std::clamp(settings.value("aspect", 0).toInt(), 0, 4);
  m_lastAreaMonitor = settings.value("lastArea/monitor").toString();
  m_lastArea = settings.value("lastArea/rect").toRectF();
  m_lastAreaPixels = settings.value("lastArea/pixels").toSize();
  m_directory =
      settings
          .value("outputDirectory", QStandardPaths::writableLocation(
                                        QStandardPaths::PicturesLocation) +
                                        "/Omaframe")
      .toString();
  m_originalsSummary = summarizeOriginals(&m_originalsCount);
  m_draftTimer.setSingleShot(true);
  m_draftTimer.setInterval(250);
  connect(&m_draftTimer, &QTimer::timeout, this, &Studio::saveDraftNow);
  refreshDrafts();
  connect(qGuiApp, &QGuiApplication::screenAdded, this,
          [this](QScreen *) { emit changed(); });
  connect(qGuiApp, &QGuiApplication::screenRemoved, this,
          [this](QScreen *) { emit changed(); });
  if (withDemo)
    loadDemo();
}
QStringList Studio::monitors() const {
  QStringList result{"All displays"};
  for (const auto &names : Displays::describe(Displays::fromScreens()))
    result << names.label;
  return result;
}
bool Studio::hasLastArea() const {
  if (m_lastAreaMonitor.isEmpty() || !m_lastAreaPixels.isValid() ||
      m_lastArea.width() <= 0 || m_lastArea.height() <= 0 ||
      !QRectF(0, 0, 1, 1).contains(m_lastArea))
    return false;
  const auto screens = QGuiApplication::screens();
  return std::any_of(screens.cbegin(), screens.cend(), [this](QScreen *screen) {
                       return screen->name() == m_lastAreaMonitor;
                     });
}
QString Studio::dimensions() const {
  return QString("%1 × %2")
      .arg(m_workingSize.width())
      .arg(m_workingSize.height());
}
QString Studio::outputDimensions() const {
  const QSize s = Frame::outputSize(m_workingSize, m_options);
  return QString("%1 × %2").arg(s.width()).arg(s.height());
}
QString Studio::recoveryAction() const {
  if (m_savedPath.isEmpty() || (!m_copyPending && !m_backupPending))
    return {};
  if (m_copyPending && m_backupPending)
    return "Retry copy and backup";
  return m_copyPending ? "Retry copy" : "Retry backup";
}
void Studio::persistOptions() {
  QSettings s;
  s.setValue("style", m_options.style);
  s.setValue("padding", m_options.padding);
  s.setValue("paddingVersion", 2);
  s.setValue("aspect", m_options.aspect);
}
void Studio::invalidateSaved() {
  m_savedPath.clear();
  m_backupPath.clear();
  m_copyPending = m_backupPending = false;
  if (!m_demo && !m_original.isNull())
    m_draftDirty = true;
}
void Studio::setStyle(int value) {
  if (m_busy || value == style() || value < 0 || value > 8)
    return;
  m_options.style = value;
  invalidateSaved();
  persistOptions();
  scheduleRender();
}
void Studio::setPadding(double value) {
  value = std::clamp(value, 0.02, 0.22);
  if (m_busy || qFuzzyCompare(value, padding()))
    return;
  m_options.padding = value;
  invalidateSaved();
  persistOptions();
  scheduleRender();
}
void Studio::setAspect(int value) {
  if (m_busy || value == aspect() || value < 0 || value > 4)
    return;
  m_options.aspect = value;
  invalidateSaved();
  persistOptions();
  scheduleRender();
}

void Studio::setEditing(bool value) {
  if (m_editing == value)
    return;
  m_editing = value;
  emit changed();
  if (!m_editing && m_thumbnailsStale && !m_original.isNull())
    scheduleRender();
}
QString Studio::originalsFolder() const { return originalsDirectory(); }
bool Studio::notifications() const {
  return QSettings().value("notifications", true).toBool();
}
void Studio::setNotifications(bool value) {
  if (value == notifications())
    return;
  QSettings().setValue("notifications", value);
  emit changed();
}
bool Studio::welcomed() const {
  return QSettings().value("welcomed", false).toBool();
}
void Studio::setWelcomed(bool value) {
  if (value == welcomed())
    return;
  QSettings().setValue("welcomed", value);
  emit changed();
}
bool Studio::keepOriginals() const {
  return QSettings().value("privacy/keepOriginals", false).toBool();
}
void Studio::setKeepOriginals(bool value) {
  if (value == keepOriginals())
    return;
  QSettings().setValue("privacy/keepOriginals", value);
  emit changed();
}
int Studio::newTextPixels() const {
  if (m_original.isNull())
    return 32;
  return std::clamp(
      qRound(std::min(m_original.width(), m_original.height()) * 0.06), 20, 64);
}

struct PreviewResult {
  QImage source, uncropped, preview;
  QVector<QImage> thumbnails;
  QSize workingSize;
};
void Studio::scheduleRender() {
  if (m_draftDirty)
    m_draftTimer.start();
  const int generation = ++m_generation;
  m_rendering = true;
  emit changed();
  auto *watcher = new QFutureWatcher<PreviewResult>(this);
  connect(watcher, &QFutureWatcher<PreviewResult>::finished, this,
          [this, watcher, generation] {
            const auto result = watcher->result();
            watcher->deleteLater();
            if (generation != m_generation)
              return;
            m_store->put("source", result.source);
            m_store->put("uncropped", result.uncropped);
            m_store->put("preview", result.preview);
            for (int i = 0; i < result.thumbnails.size(); ++i)
              m_store->put(QString("style%1").arg(i), result.thumbnails[i]);
            m_workingSize = result.workingSize;
            m_rendering = false;
            ++m_revision;
            emit changed();
            if (m_pendingFinish >= 0) {
              m_pendingFinish = -1;
              accept();
            }
          });
  // Each request owns immutable implicitly-shared input. Obsolete results never
  // reach the UI.
  QVector<Frame::Edit> edits = m_edits;
  if (m_hiddenEdit >= 0 && m_hiddenEdit < edits.size())
    edits.removeAt(m_hiddenEdit);
  const bool thumbnails = !m_editing;
  m_thumbnailsStale = !thumbnails;
  watcher->setFuture(QtConcurrent::run(
      [source = m_original, edits, options = m_options, thumbnails] {
        PreviewResult result;
        const QImage uncropped = Frame::applyEdits(source, edits, false);
        const QImage working = Frame::cropImage(uncropped, edits);
        result.workingSize = working.size();
        result.uncropped = uncropped.scaled(1800, 1800, Qt::KeepAspectRatio,
                                            Qt::SmoothTransformation);
        result.source = working.scaled(1800, 1800, Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation);
        result.preview = Frame::compose(working, options, 1600);
        for (int i = 0; thumbnails && i < 9; ++i) {
          auto opt = options;
          opt.style = i;
          result.thumbnails.append(Frame::compose(working, opt, 640));
        }
        return result;
      }));
}
void Studio::loadImage(QImage image, QString name, bool demo) {
  if (image.isNull())
    return;
  saveDraftNow();
  image.setDevicePixelRatio(1);
  m_original = std::move(image);
  m_workingSize = m_original.size();
  m_name = std::move(name);
  m_demo = demo;
  m_edits.clear();
  m_undoStates.clear();
  m_redoStates.clear();
  m_selected = -1;
  m_hiddenEdit = -1;
  invalidateSaved();
  m_draftTimer.stop();
  m_draftDirty = false;
  m_draftId.clear();
  m_status = demo ? "Sample image. Try a finish or the editor."
                  : "Ready when you are.";
  scheduleRender();
  emit sourceChanged();
}
void Studio::closeImage() {
  if (m_busy || m_original.isNull())
    return;
  saveDraftNow();
  ++m_generation;
  m_original = {};
  m_workingSize = {};
  m_name.clear();
  m_edits.clear();
  m_undoStates.clear();
  m_redoStates.clear();
  m_selected = m_hiddenEdit = -1;
  m_draftId.clear();
  m_draftDirty = false;
  m_rendering = false;
  invalidateSaved();
  m_status = "Ready when you are.";
  emit changed();
  emit sourceChanged();
}
void Studio::loadDemo(int variant) {
  if (!m_busy)
    loadImage(Frame::demoImage(variant),
              variant == 1 ? "Terminal sample" : "Noon workspace", true);
}
void Studio::open(const QUrl &url) {
  if (m_busy || !url.isLocalFile())
    return;
  if (QStringList{"mp4", "webm", "mkv", "mov", "m4v", "avi"}.contains(
          QFileInfo(url.toLocalFile()).suffix().toLower())) {
    emit videoRequested(url);
    return;
  }
  m_busy = true;
  m_status = "Opening image…";
  emit changed();
  struct Result {
    QImage image;
    QString error;
  };
  auto *watcher = new QFutureWatcher<Result>(this);
  connect(
      watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, url] {
        auto r = watcher->result();
        watcher->deleteLater();
        m_busy = false;
        if (r.image.isNull()) {
          m_status = r.error;
          emit changed();
        } else
          loadImage(r.image, QFileInfo(url.toLocalFile()).fileName(), false);
      });
  watcher->setFuture(QtConcurrent::run([url] {
    QImageReader reader(url.toLocalFile());
    reader.setAutoTransform(true);
    const QSize s = reader.size();
    if (s.isValid() && (qint64(s.width()) * s.height() > 40000000))
      return Result{{},
                    "This image is too large. The first version supports up to "
                    "40 megapixels."};
    QImage image = reader.read();
    return Result{image, image.isNull()
                             ? "Could not open image: " + reader.errorString()
                             : QString()};
  }));
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

void Studio::edit(const QString &type, double x1, double y1, double x2,
                  double y2, const QString &text) {
  if (m_busy)
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
  if (m_edits.size() >= 100 && !(type == "crop" && hasCrop())) {
    m_status = "This image has reached the 100-edit limit.";
    emit changed();
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
    edit.size = Frame::textSizeForPixels(newTextPixels(), m_original);
    fitTextToImage(edit, m_original);
  }
  m_edits.append(edit);
  if (type != "crop")
    m_selected = m_edits.size() - 1;
  invalidateSaved();
  m_status = type == "redact"
                 ? "Redaction applied. Exported pixels are fully replaced."
                 : "Edit applied. Undo is always available.";
  scheduleRender();
}
void Studio::addStroke(const QVariantList &points) {
  if (m_busy || points.size() < 2 || m_edits.size() >= 100)
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
  invalidateSaved();
  m_status = "Stroke added. Select it to move, resize, or change color.";
  scheduleRender();
}
void Studio::saveHistory() {
  if (m_undoStates.size() >= 100)
    m_undoStates.removeFirst();
  m_undoStates.append({m_edits, m_selected});
  m_redoStates.clear();
}
QPointF Studio::sourcePoint(double x, double y) const {
  const QRectF crop = cropBounds();
  return {std::clamp(crop.x() + std::clamp(x, 0., 1.) * crop.width(), 0., 1.),
          std::clamp(crop.y() + std::clamp(y, 0., 1.) * crop.height(), 0., 1.)};
}
bool Studio::hasCrop() const {
  return std::any_of(m_edits.begin(), m_edits.end(),
                     [](const Frame::Edit &edit) { return edit.type == "crop"; });
}
QVariantMap Studio::selectedAnnotation() const {
  if (m_selected < 0 || m_selected >= m_edits.size())
    return {};
  const auto &edit = m_edits[m_selected];
  if (edit.type == "crop")
    return {};
  const QRectF crop = cropBounds();
  const QRectF bounds = Frame::annotationBounds(edit, m_original);
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
          {"fontPx", edit.type == "text" ? Frame::textPixelSize(edit, m_original) : 0},
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
int Studio::hitIndex(double x, double y, bool edgesOnly) const {
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
      const QRectF bounds = Frame::annotationBounds(edit, m_original);
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
int Studio::selectAt(double x, double y) {
  const int found = hitIndex(x, y);
  if (found != m_selected) {
    m_selected = found;
    emit changed();
  }
  return m_selected;
}
void Studio::select(int index) {
  if (index < -1 || index >= m_edits.size() ||
      (index >= 0 && m_edits[index].type == "crop") || index == m_selected)
    return;
  m_selected = index;
  emit changed();
}
QVariantMap Studio::hitAt(double x, double y, bool edgesOnly) const {
  const int found = hitIndex(x, y, edgesOnly);
  if (found < 0)
    return {};
  const QRectF crop = cropBounds();
  const QRectF bounds = Frame::annotationBounds(m_edits[found], m_original);
  return {{"index", found},
          {"type", m_edits[found].type},
          {"x", (bounds.x() - crop.x()) / crop.width()},
          {"y", (bounds.y() - crop.y()) / crop.height()},
          {"w", bounds.width() / crop.width()},
          {"h", bounds.height() / crop.height()}};
}
void Studio::clearSelection() {
  if (m_selected < 0)
    return;
  m_selected = -1;
  emit changed();
}
void Studio::beginTextEdit() {
  if (m_busy || m_selected < 0 || m_selected >= m_edits.size() ||
      m_edits[m_selected].type != "text" || m_hiddenEdit == m_selected)
    return;
  m_hiddenEdit = m_selected;
  scheduleRender();
}
void Studio::endTextEdit(const QString &text, bool commit) {
  if (m_hiddenEdit < 0)
    return;
  const int index = m_hiddenEdit;
  m_hiddenEdit = -1;
  // Typed text is applied even while a preview is still rendering, so a
  // quick click away never loses it.
  if (commit && index < m_edits.size() && m_edits[index].type == "text") {
    if (text.trimmed().isEmpty()) {
      saveHistory();
      m_edits.removeAt(index);
      m_selected = -1;
      invalidateSaved();
      m_status = "Empty label removed.";
    } else if (m_edits[index].text != text.left(240)) {
      saveHistory();
      m_edits[index].text = text.left(240);
      const bool fitted = fitTextToImage(m_edits[index], m_original);
      invalidateSaved();
      m_status = fitted ? "Text updated. Font size limited so the full label fits."
                        : "Text updated.";
    }
  }
  scheduleRender();
}
void Studio::moveSelected(double dx, double dy) {
  if (m_busy || m_selected < 0 || m_selected >= m_edits.size())
    return;
  const auto edit = m_edits.at(m_selected);
  const QRectF crop = cropBounds();
  const QRectF bounds = Frame::annotationBounds(edit, m_original);
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
  invalidateSaved();
  scheduleRender();
}
void Studio::nudgeSelected(int dx, int dy) {
  if (m_original.isNull() || (dx == 0 && dy == 0))
    return;
  const QRectF crop = cropBounds();
  moveSelected(double(dx) / (m_original.width() * crop.width()),
               double(dy) / (m_original.height() * crop.height()));
}
void Studio::resizeSelected(int handle, double x, double y) {
  if (m_busy || m_selected < 0 || m_selected >= m_edits.size())
    return;
  const auto edit = m_edits.at(m_selected);
  if (edit.type == "crop")
    return;
  const QPointF point = sourcePoint(x, y);
  if (edit.type == "text" || edit.type == "step") {
    if (handle < 0 || handle > 3)
      return;
    const QRectF bounds = Frame::annotationBounds(edit, m_original);
    const QPointF opposite = edit.type == "step" ? edit.from
        : handle == 0 ? bounds.bottomRight()
        : handle == 1 ? bounds.bottomLeft()
        : handle == 2 ? bounds.topLeft() : bounds.topRight();
    const QPointF corner = handle == 0 ? bounds.topLeft()
        : handle == 1 ? bounds.topRight()
        : handle == 2 ? bounds.bottomRight() : bounds.bottomLeft();
    const QPointF scale(m_original.width(), m_original.height());
    const QPointF oldVector((corner.x() - opposite.x()) * scale.x(),
                            (corner.y() - opposite.y()) * scale.y());
    const QPointF newVector((point.x() - opposite.x()) * scale.x(),
                            (point.y() - opposite.y()) * scale.y());
    const double oldLength2 = QPointF::dotProduct(oldVector, oldVector);
    if (oldLength2 <= 0)
      return;
    const double minimum = edit.type == "text"
                               ? Frame::textSizeForPixels(8, m_original)
                               : 0.5;
    const double maximum = edit.type == "text"
                               ? Frame::textSizeForPixels(4096, m_original)
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
      const QRectF resized = Frame::annotationBounds(updated, m_original);
      QPointF anchor = handle == 0 ? opposite - QPointF(resized.width(), resized.height())
                       : handle == 1 ? opposite - QPointF(0, resized.height())
                                     : opposite - QPointF(resized.width(), 0);
      updated.from = QPointF(std::clamp(anchor.x(), 0., 1.),
                             std::clamp(anchor.y(), 0., 1.));
      updated.to = updated.from;
    }
    const bool fitted = fitTextToImage(updated, m_original);
    saveHistory();
    m_edits[m_selected] = updated;
    invalidateSaved();
    if (fitted)
      m_status = "Font size limited so the full label fits.";
    scheduleRender();
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
  invalidateSaved();
  scheduleRender();
}
void Studio::deleteSelected() {
  if (m_busy || m_selected < 0 || m_selected >= m_edits.size())
    return;
  m_hiddenEdit = -1;
  saveHistory();
  m_edits.removeAt(m_selected);
  m_selected = -1;
  invalidateSaved();
  scheduleRender();
}
void Studio::duplicateSelected() {
  if (m_busy || m_selected < 0 || m_selected >= m_edits.size() ||
      m_edits.size() >= 100 || m_edits[m_selected].type == "crop")
    return;
  Frame::Edit copy = m_edits[m_selected];
  const QRectF bounds = Frame::annotationBounds(copy, m_original);
  const double stepX = 12. / std::max(1, m_original.width());
  const double stepY = 12. / std::max(1, m_original.height());
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
  invalidateSaved();
  m_status = "Annotation duplicated. Drag it to place it.";
  scheduleRender();
}
void Studio::moveSelectedLayer(int direction) {
  if (m_busy || m_selected < 0 || m_selected >= m_edits.size() ||
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
  invalidateSaved();
  m_status = direction > 0 ? "Annotation moved forward." : "Annotation moved back.";
  scheduleRender();
}
void Studio::updateSelectedText(const QString &text) {
  if (m_busy || m_selected < 0 || m_selected >= m_edits.size() ||
      m_edits.at(m_selected).type != "text" || text.trimmed().isEmpty())
    return;
  const QString updated = text.left(240);
  if (m_edits.at(m_selected).text == updated)
    return;
  saveHistory();
  m_edits[m_selected].text = updated;
  const bool fitted = fitTextToImage(m_edits[m_selected], m_original);
  invalidateSaved();
  m_status = fitted ? "Text updated. Font size limited so the full label fits."
                    : "Text updated.";
  scheduleRender();
}
void Studio::setSelectedColor(const QString &color) {
  if (m_busy || m_selected < 0 || m_selected >= m_edits.size())
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
  invalidateSaved();
  scheduleRender();
}
void Studio::setSelectedSize(double size) {
  if (m_busy || m_selected < 0 || m_selected >= m_edits.size())
    return;
  const QString type = m_edits.at(m_selected).type;
  if (!QStringList{"arrow", "line", "box", "ellipse", "step", "text", "pen", "blur"}
           .contains(type) || !std::isfinite(size))
    return;
  const double next = type == "text"
                          ? std::clamp(size,
                                       Frame::textSizeForPixels(8, m_original),
                                       Frame::textSizeForPixels(4096, m_original))
                          : std::clamp(size, 0.5, 8.0);
  if (qAbs(m_edits.at(m_selected).size - next) < 0.01)
    return;
  saveHistory();
  m_edits[m_selected].size = next;
  const bool fitted = fitTextToImage(m_edits[m_selected], m_original);
  invalidateSaved();
  if (fitted)
    m_status = "Font size limited so the full label fits.";
  scheduleRender();
}
void Studio::setSelectedFontPixels(int pixels) {
  if (m_selected < 0 || m_selected >= m_edits.size() ||
      m_edits.at(m_selected).type != "text")
    return;
  setSelectedSize(Frame::textSizeForPixels(pixels, m_original));
}
void Studio::setSelectedTextStyle(const QString &style) {
  if (m_busy || m_selected < 0 || m_selected >= m_edits.size() ||
      m_edits.at(m_selected).type != "text" ||
      (style != "box" && style != "shadow") ||
      m_edits.at(m_selected).textStyle == style)
    return;
  saveHistory();
  m_edits[m_selected].textStyle = style;
  if (style == "shadow" && m_edits[m_selected].color == QColor(Qt::white))
    m_edits[m_selected].color = QColor("#e75439");
  invalidateSaved();
  scheduleRender();
}
void Studio::setSelectedTextAlignment(const QString &alignment) {
  if (m_busy || m_selected < 0 || m_selected >= m_edits.size() ||
      m_edits.at(m_selected).type != "text" ||
      !QStringList{"left", "center", "right"}.contains(alignment) ||
      m_edits.at(m_selected).textAlign == alignment)
    return;
  saveHistory();
  m_edits[m_selected].textAlign = alignment;
  invalidateSaved();
  scheduleRender();
}
void Studio::setSelectedBackground(const QString &color) {
  if (m_busy || m_selected < 0 || m_selected >= m_edits.size() ||
      m_edits.at(m_selected).type != "text")
    return;
  const QColor parsed(color);
  if (!parsed.isValid() || parsed == m_edits.at(m_selected).background)
    return;
  saveHistory();
  m_edits[m_selected].background = parsed;
  invalidateSaved();
  scheduleRender();
}
void Studio::setSelectedBackgroundOpacity(double opacity) {
  if (m_busy || m_selected < 0 || m_selected >= m_edits.size() ||
      m_edits.at(m_selected).type != "text" || !std::isfinite(opacity))
    return;
  const double next = std::clamp(opacity, 0.0, 1.0);
  if (qAbs(next - m_edits.at(m_selected).backgroundOpacity) < 0.01)
    return;
  saveHistory();
  m_edits[m_selected].backgroundOpacity = next;
  invalidateSaved();
  scheduleRender();
}
void Studio::clearCrop() {
  if (m_busy || !hasCrop())
    return;
  saveHistory();
  m_edits.removeIf([](const Frame::Edit &edit) { return edit.type == "crop"; });
  m_selected = -1;
  invalidateSaved();
  scheduleRender();
}
void Studio::undo() {
  if (m_busy || m_undoStates.isEmpty())
    return;
  m_hiddenEdit = -1;
  m_redoStates.append({m_edits, m_selected});
  const EditState previous = m_undoStates.takeLast();
  m_edits = previous.edits;
  m_selected = previous.selected;
  invalidateSaved();
  scheduleRender();
}
void Studio::redo() {
  if (m_busy || m_redoStates.isEmpty())
    return;
  m_hiddenEdit = -1;
  m_undoStates.append({m_edits, m_selected});
  const EditState next = m_redoStates.takeLast();
  m_edits = next.edits;
  m_selected = next.selected;
  invalidateSaved();
  scheduleRender();
}
void Studio::resetEdits() {
  if (m_busy || m_edits.isEmpty())
    return;
  saveHistory();
  m_edits.clear();
  m_selected = -1;
  invalidateSaved();
  scheduleRender();
}
void Studio::setOutputDirectory(const QUrl &url) {
  if (!url.isLocalFile() || m_busy)
    return;
  m_directory = url.toLocalFile();
  QSettings().setValue("outputDirectory", m_directory);
  emit changed();
}
void Studio::revealSaved() {
  if (!m_savedPath.isEmpty())
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(QFileInfo(m_savedPath).absolutePath()));
}
void Studio::refreshDrafts() {
  QVariantList drafts;
  const QDir directory(draftsDirectory());
  const auto files = directory.entryInfoList({"*.json"}, QDir::Files | QDir::NoSymLinks,
                                             QDir::Time);
  for (const auto &file : files) {
    const QString id = file.completeBaseName();
    if (!validDraftId(id) || file.size() > maxDraftMetadataBytes ||
        !QFileInfo::exists(directory.filePath(id + ".png")))
      continue;
    QFile input(file.absoluteFilePath());
    if (!input.open(QIODevice::ReadOnly))
      continue;
    const auto document = QJsonDocument::fromJson(input.readAll()).object();
    if (document.value("version").toInt() != 1)
      continue;
    drafts.append(QVariantMap{
        {"id", id},
        {"name", document.value("name").toString("Screenshot")},
        {"when", file.lastModified().toString("MMM d, h:mm AP")},
        {"edits", document.value("edits").toArray().size()},
        {"image", QUrl::fromLocalFile(directory.filePath(id + ".png")).toString()},
        {"exported", QFileInfo::exists(document.value("savedPath").toString())}});
  }
  m_drafts = drafts;
  emit changed();
}
void Studio::saveDraftNow() {
  m_draftTimer.stop();
  if (!m_draftDirty || m_demo || m_original.isNull())
    return;
  if (m_draftId.isEmpty() && m_edits.isEmpty()) {
    m_draftDirty = false;
    return;
  }
  const QString directory = draftsDirectory();
  if (!QDir().mkpath(directory)) {
    m_status = "Could not save editable draft. Check the application data folder.";
    emit changed();
    return;
  }
  QFile::setPermissions(directory, QFile::ReadOwner | QFile::WriteOwner |
                                      QFile::ExeOwner);
  if (m_draftId.isEmpty())
    m_draftId = QUuid::createUuid().toString(QUuid::Id128);
  const QString sourcePath = directory + "/" + m_draftId + ".png";
  if (!QFileInfo::exists(sourcePath)) {
    QSaveFile source(sourcePath);
    if (!source.open(QIODevice::WriteOnly) ||
        !source.setPermissions(QFile::ReadOwner | QFile::WriteOwner)) {
      m_status = "Could not save the private draft image.";
      emit changed();
      return;
    }
    QImageWriter writer(&source, "png");
    if (!writer.write(m_original) || !source.commit()) {
      m_status = "Could not finish saving the private draft image.";
      emit changed();
      return;
    }
  }
  QJsonArray edits;
  for (const auto &edit : m_edits)
    edits.append(editToJson(edit));
  const QJsonObject document{{"version", 1}, {"name", m_name},
                             {"style", m_options.style},
                             {"padding", m_options.padding},
                             {"aspect", m_options.aspect},
                             {"selected", m_selected},
                             {"savedPath", m_savedPath}, {"edits", edits}};
  QSaveFile metadata(directory + "/" + m_draftId + ".json");
  const QByteArray serialized = QJsonDocument(document).toJson(QJsonDocument::Compact);
  if (!metadata.open(QIODevice::WriteOnly) ||
      !metadata.setPermissions(QFile::ReadOwner | QFile::WriteOwner) ||
      metadata.write(serialized) != serialized.size() ||
      !metadata.commit()) {
    m_status = "Could not finish saving the editable draft.";
    emit changed();
    return;
  }
  m_draftDirty = false;
  refreshDrafts();
}
void Studio::resumeDraft(const QString &id) {
  if (m_busy || !validDraftId(id))
    return;
  saveDraftNow();
  const QString directory = draftsDirectory();
  QFile metadata(directory + "/" + id + ".json");
  if (!metadata.open(QIODevice::ReadOnly) ||
      metadata.size() > maxDraftMetadataBytes) {
    m_status = "Could not read that editable draft.";
    emit changed();
    return;
  }
  const QJsonObject document = QJsonDocument::fromJson(metadata.readAll()).object();
  const QJsonArray savedEdits = document.value("edits").toArray();
  if (document.value("version").toInt() != 1 || savedEdits.size() > 100) {
    m_status = "This editable draft is damaged or unsupported.";
    emit changed();
    return;
  }
  QVector<Frame::Edit> edits;
  for (const auto &value : savedEdits) {
    const auto edit = editFromJson(value.toObject());
    if (!edit) {
      m_status = "This editable draft contains a damaged annotation.";
      emit changed();
      return;
    }
    edits.append(*edit);
  }
  QImageReader reader(directory + "/" + id + ".png");
  const QSize size = reader.size();
  if (!size.isValid() || qint64(size.width()) * size.height() > 40000000) {
    m_status = "This editable draft has an invalid source image.";
    emit changed();
    return;
  }
  QImage image = reader.read();
  if (image.isNull()) {
    m_status = "Could not reopen the draft image.";
    emit changed();
    return;
  }
  m_original = std::move(image);
  m_workingSize = m_original.size();
  m_edits = std::move(edits);
  m_undoStates.clear();
  m_redoStates.clear();
  m_selected = std::clamp(document.value("selected").toInt(-1), -1,
                          int(m_edits.size()) - 1);
  m_options.style = std::clamp(document.value("style").toInt(), 0, 8);
  m_options.padding = std::clamp(document.value("padding").toDouble(0.05), 0.02, 0.22);
  m_options.aspect = std::clamp(document.value("aspect").toInt(), 0, 4);
  m_name = document.value("name").toString("Screenshot");
  m_demo = false;
  m_savedPath = document.value("savedPath").toString();
  if (!QFileInfo::exists(m_savedPath))
    m_savedPath.clear();
  m_backupPath.clear();
  m_copyPending = m_backupPending = false;
  m_draftId = id;
  m_draftDirty = false;
  m_draftTimer.stop();
  m_status = "Editable draft reopened.";
  scheduleRender();
  emit sourceChanged();
  emit editorRequested();
}
void Studio::deleteDraft(const QString &id) {
  if (m_busy || !validDraftId(id))
    return;
  const QString directory = draftsDirectory();
  bool removed = true;
  for (const QString &suffix : {".json", ".png"}) {
    const QString path = directory + "/" + id + suffix;
    if (QFileInfo::exists(path) && !QFile::remove(path))
      removed = false;
  }
  if (m_draftId == id) {
    m_draftTimer.stop();
    m_draftId.clear();
    m_draftDirty = false;
  }
  m_status = removed ? "Editable draft removed." : "Could not remove all draft files.";
  refreshDrafts();
}
void Studio::clearOriginals() {
  if (m_busy)
    return;
  QDir directory(originalsDirectory());
  int removed = 0, failed = 0;
  for (const auto &file : directory.entryList({"*.png"}, QDir::Files | QDir::NoSymLinks)) {
    if (directory.remove(file))
      ++removed;
    else
      ++failed;
  }
  m_originalsSummary = summarizeOriginals(&m_originalsCount);
  m_status = failed ? QString("Removed %1 originals; %2 could not be removed.")
                          .arg(removed).arg(failed)
                    : QString("Removed %1 private originals.").arg(removed);
  emit changed();
}

static bool writePng(const QString &path, const QImage &image, QString &error,
                     bool privateFile = false) {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    error = file.errorString();
    return false;
  }
  if (privateFile &&
      !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)) {
    error = "Could not set private permissions on the original backup.";
    return false;
  }
  QImageWriter writer(&file, "png");
  writer.setCompression(60);
  if (!writer.write(image)) {
    error = writer.errorString();
    return false;
  }
  if (!file.commit()) {
    error = file.errorString();
    return false;
  }
  return true;
}
static bool copyPng(const QString &path, QString &error) {
  QFile input(path);
  if (!input.open(QIODevice::ReadOnly)) {
    error = "Could not read the saved PNG for clipboard copy.";
    return false;
  }
  QProcess clipboard;
  clipboard.start("wl-copy", {"--type", "image/png"});
  if (!clipboard.waitForStarted(3000)) {
    error = "Could not start wl-copy. Check that it is installed.";
    return false;
  }
  clipboard.write(input.readAll());
  clipboard.closeWriteChannel();
  const bool copied = clipboard.waitForFinished(10000) &&
                      clipboard.exitStatus() == QProcess::NormalExit &&
                      clipboard.exitCode() == 0;
  if (clipboard.state() != QProcess::NotRunning) {
    clipboard.kill();
    clipboard.waitForFinished();
  }
  if (!copied)
    error = "wl-copy could not accept the saved PNG.";
  return copied;
}
struct ExportResult {
  QString path, backupPath, error, copyError, backupError;
  bool copied = false, backupSaved = false;
};
static QString exportStatus(const ExportResult &r) {
  if (r.path.isEmpty())
    return r.error;
  if (r.copied && r.backupSaved)
    return "Copied to the clipboard and saved as " + QFileInfo(r.path).fileName() + ".";
  QStringList problems;
  if (!r.copied)
    problems << "Clipboard copy failed: " + r.copyError;
  if (!r.backupSaved)
    problems << "Private original backup failed: " + r.backupError;
  return "Finished PNG saved. " + problems.join(" ");
}
void Studio::accept() {
  if (m_busy || m_rendering || m_original.isNull())
    return;
  saveDraftNow();
  const QSize output = Frame::outputSize(m_workingSize, m_options);
  if (qint64(output.width()) * output.height() > 80000000) {
    if (m_quickMode)
      m_quickState = "failed";
    m_status = "This canvas would exceed 80 megapixels. Use a smaller border "
               "or a different aspect ratio.";
    emit changed();
    return;
  }
  m_busy = true;
  m_copyPending = m_backupPending = false;
  m_backupPath.clear();
  if (m_quickMode)
    m_quickState = "saving";
  m_status = "Saving full-resolution PNG and copying…";
  emit changed();
  const QString originalDirectory = keepOriginals() ? originalsDirectory() : QString();
  auto *watcher = new QFutureWatcher<ExportResult>(this);
  connect(watcher, &QFutureWatcher<ExportResult>::finished, this,
          [this, watcher] {
            auto r = watcher->result();
            watcher->deleteLater();
            m_busy = false;
            m_savedPath = r.path;
            if (!r.path.isEmpty() && !m_draftId.isEmpty()) {
              m_draftDirty = true;
              m_draftTimer.start();
            }
            m_backupPath = r.backupPath;
            m_copyPending = !r.path.isEmpty() && !r.copied;
            m_backupPending = !r.path.isEmpty() && !r.backupSaved;
            m_status = exportStatus(r);
            m_originalsSummary = summarizeOriginals(&m_originalsCount);
            if (m_quickMode)
              m_quickState = r.path.isEmpty() || m_copyPending || m_backupPending
                                 ? "failed"
                                 : "done";
            emit changed();
            // Keep failures visible. Never dismiss a capture that was not both
            // saved and copied, including a failed original backup.
            if (m_quickMode && m_quickState == "done")
              emit dismissRequested();
          });
  watcher->setFuture(QtConcurrent::run([source = m_original, edits = m_edits,
                                        options = m_options,
                                        directory = m_directory,
                                        originalDirectory] {
    ExportResult r;
    if (!QDir().mkpath(directory)) {
      r.error = "Could not create the save folder. Choose another folder and try again.";
      return r;
    }
    const QString id =
        QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss-zzz") + "-" +
        QUuid::createUuid().toString(QUuid::Id128).left(6);
    const QString path = directory + "/Omaframe-" + id + ".png";
    QImage composed = Frame::compose(Frame::applyEdits(source, edits), options);
    // A fresh raster strips imported PNG text and other source metadata,
    // including in Raw mode.
    QImage flattened(composed.size(), QImage::Format_ARGB32_Premultiplied);
    flattened.fill(Qt::transparent);
    {
      QPainter painter(&flattened);
      painter.drawImage(0, 0, composed);
    }
    if (!writePng(path, flattened, r.error)) {
      r.error = "Could not save image: " + r.error;
      return r;
    }
    r.path = path;
    if (originalDirectory.isEmpty()) {
      // Keeping private originals is off: nothing else to write.
      r.backupSaved = true;
    } else {
      r.backupPath = originalDirectory + "/" + id + ".png";
      r.backupSaved = QDir().mkpath(originalDirectory) &&
                      writePng(r.backupPath, source, r.backupError, true);
      if (!r.backupSaved && r.backupError.isEmpty())
        r.backupError = "Could not create the original backup folder.";
    }
    r.copied = copyPng(path, r.copyError);
    return r;
  }));
}
void Studio::retryOutput() {
  if (m_busy || m_savedPath.isEmpty() || (!m_copyPending && !m_backupPending))
    return;
  m_busy = true;
  if (m_quickMode)
    m_quickState = "saving";
  m_status = "Retrying the saved screenshot…";
  emit changed();
  auto *watcher = new QFutureWatcher<ExportResult>(this);
  connect(watcher, &QFutureWatcher<ExportResult>::finished, this,
          [this, watcher] {
            const auto r = watcher->result();
            watcher->deleteLater();
            m_busy = false;
            m_copyPending = !r.copied;
            m_backupPending = !r.backupSaved;
            m_status = exportStatus(r);
            m_originalsSummary = summarizeOriginals(&m_originalsCount);
            if (m_quickMode)
              m_quickState = m_copyPending || m_backupPending ? "failed" : "done";
            emit changed();
            if (m_quickMode && m_quickState == "done")
              emit dismissRequested();
          });
  watcher->setFuture(QtConcurrent::run([path = m_savedPath,
                                        backupPath = m_backupPath,
                                        source = m_original,
                                        copyPending = m_copyPending,
                                        backupPending = m_backupPending] {
    ExportResult r;
    r.path = path;
    r.backupPath = backupPath;
    r.copied = !copyPending || copyPng(path, r.copyError);
    r.backupSaved = !backupPending ||
                    (QDir().mkpath(QFileInfo(backupPath).absolutePath()) &&
                     writePng(backupPath, source, r.backupError, true));
    if (!r.backupSaved && r.backupError.isEmpty())
      r.backupError = "Could not create the original backup folder.";
    return r;
  }));
}

void Studio::capture(bool region, int monitor) {
  captureImpl(region, monitor, false);
}
void Studio::repeatLastArea() {
  if (!hasLastArea()) {
    capture(true);
    return;
  }
  m_recordingSelection = false;
  captureImpl(false, 0, true);
}
void Studio::captureImpl(bool region, int monitor, bool repeat) {
  if (m_busy)
    return;
  const auto open = QGuiApplication::allWindows();
  // A capture started from the open studio window returns there afterwards.
  // Keep that until it is used: going through recording options hides the
  // window before the second selector opens.
  if (!m_quickMode && !m_returnToStudio)
    m_returnToStudio = std::any_of(open.cbegin(), open.cend(), [](QWindow *w) {
      return w->isVisible() && w->title() == "Omaframe";
    });
  m_busy = true;
  m_quickMode = true;
  m_quickState = "capturing";
  m_pendingFinish = -1;
  m_status = repeat ? "Capturing the last area…" : "Capturing…";
  m_frozen.clear();
  m_windowTargets.clear();
  m_pointerMonitor.clear();
  // Only wait for dismissal animations when this process has a visible UI.
  // A fresh shortcut launch has nothing of its own to hide.
  const auto windows = QGuiApplication::allWindows();
  const bool wasVisible =
      std::any_of(windows.cbegin(), windows.cend(),
                  [](QWindow *w) { return w->isVisible(); });
  emit changed();
  emit hideStudio();
  QStringList requested;
  // A region starts on whichever display the user chooses with the pointer.
  // Snapshot all displays before placing any selection overlays.
  if (repeat)
    requested << m_lastAreaMonitor;
  else if (region && monitor == 0) {
    for (QScreen *screen : QGuiApplication::screens())
      requested << screen->name();
  } else if (const auto screens = QGuiApplication::screens();
             monitor > 0 && monitor <= screens.size())
    requested << screens[monitor - 1]->name();
  QTimer::singleShot(
      wasVisible ? 220 : 0, this,
      [this, region, repeat, requested, lastArea = m_lastArea,
       lastPixels = m_lastAreaPixels]() mutable {
        struct SelectionCapture {
          Capture::Screens screens;
          QVariantList targets;
          QString pointer;
        };
        auto *watcher = new QFutureWatcher<SelectionCapture>(this);
        connect(watcher, &QFutureWatcher<SelectionCapture>::finished, this,
                [this, watcher, region, repeat, lastArea, lastPixels] {
                  auto result = watcher->result();
                  watcher->deleteLater();
                  if (!result.screens.error.isEmpty()) {
                    // Report it where the user is looking, not on the primary
                    // display, which may be off or out of sight.
                    if (!result.pointer.isEmpty())
                      m_captureMonitor = result.pointer;
                    m_busy = false;
                    m_quickState = "capture-error";
                    m_status = "Capture failed: " + result.screens.error;
                    emit changed();
                    emit captureFailed();
                    return;
                  }
                  QImage repeated;
                  if (repeat) {
                    const auto frame = result.screens.images.constBegin();
                    if (frame.value().size() == lastPixels)
                      repeated = Capture::crop(result.screens.images, frame.key(),
                                               lastArea.topLeft(),
                                               lastArea.bottomRight());
                  }
                  if (region || (repeat && repeated.isNull())) {
                    m_frozen = result.screens.images;
                    m_windowTargets = result.targets;
                    // F can only answer for a display that was frozen.
                    m_pointerMonitor = m_frozen.contains(result.pointer)
                                           ? result.pointer
                                           : QString();
                    for (auto it = m_frozen.cbegin(); it != m_frozen.cend();
                         ++it)
                      m_store->put("capture/" + it.key(), it.value());
                    m_quickState = "selecting";
                    if (repeat)
                      m_status = "The display changed. Select an area again.";
                    ++m_revision;
                    emit changed();
                    emit selectionReady(m_frozen.keys());
                  } else {
                    m_busy = false;
                    const auto frame = result.screens.images.constBegin();
                    m_captureMonitor = frame.key();
                    loadImage(repeat ? repeated : frame.value(),
                              repeat ? "Repeated area capture" : "Screen capture",
                              false);
                    m_quickState = "choosing";
                    emit changed();
                    emit chooserRequested();
                  }
                });
        watcher->setFuture(QtConcurrent::run([requested, region, repeat]() mutable {
          if (requested.isEmpty()) {
            QProcess process;
            process.start("hyprctl", {"-j", "monitors"});
            if (!process.waitForFinished(2000)) {
              process.kill();
              process.waitForFinished();
              return SelectionCapture{
                  {{}, "Could not identify the active display."}, {}};
            }
            const QJsonArray outputs =
                QJsonDocument::fromJson(process.readAllStandardOutput())
                    .array();
            for (const auto &value : outputs) {
              auto output = value.toObject();
              if (output.value("focused").toBool())
                requested << output.value("name").toString();
            }
            if (requested.isEmpty() && !outputs.isEmpty())
              requested << outputs.first().toObject().value("name").toString();
          }
          SelectionCapture result;
          if (region || repeat) {
            auto query = [](const QString &what) {
              QProcess p;
              p.start("hyprctl", {"-j", what});
              if (!p.waitForFinished(1000)) {
                p.kill();
                p.waitForFinished();
                return QJsonArray{};
              }
              return QJsonDocument::fromJson(p.readAllStandardOutput()).array();
            };
            const auto monitors = query("monitors");
            const auto clients = query("clients");
            requested = WindowTargets::awake(monitors, requested);
            result.targets =
                WindowTargets::fromHyprland(monitors, clients, requested);
            // The display under the pointer answers F before the pointer moves.
            QProcess cursor;
            cursor.start("hyprctl", {"-j", "cursorpos"});
            if (cursor.waitForFinished(1000)) {
              const auto at = QJsonDocument::fromJson(cursor.readAllStandardOutput()).object();
              const QPointF point(at.value("x").toDouble(), at.value("y").toDouble());
              for (const auto &value : monitors) {
                const auto m = value.toObject();
                const double scale = m.value("scale").toDouble(1);
                int width = m.value("width").toInt(), height = m.value("height").toInt();
                if (m.value("transform").toInt() % 2)
                  std::swap(width, height);
                if (scale > 0 && QRectF(m.value("x").toDouble(), m.value("y").toDouble(),
                                        width / scale, height / scale)
                                     .contains(point))
                  result.pointer = m.value("name").toString();
              }
            } else {
              cursor.kill();
              cursor.waitForFinished();
            }
          }
          result.screens = Capture::freeze(requested, [](const QString &name,
                                                         QImage &image,
                                                         QString &error) {
            MonitorInfo info;
            info.name = name;
            return captureOutputSurface(info, image, error);
          });
          return result;
        }));
      });
}
void Studio::finishSelection(const QString &monitor, double x1, double y1,
                             double x2, double y2) {
  if (m_quickState != "selecting")
    return;
  const QImage result = Capture::crop(m_frozen, monitor, {x1, y1}, {x2, y2});
  if (result.isNull())
    return;
  const QSize imagePixels = m_frozen.value(monitor).size();
  m_captureMonitor = monitor;
  for (const auto &name : m_frozen.keys())
    m_store->put("capture/" + name, {});
  m_frozen.clear();
  m_busy = false;
  emit selectionDone();
  if (m_recordingSelection) {
    leaveQuickMode();
    emit recordRegionSelected(monitor, QRectF(QPointF(x1, y1), QPointF(x2, y2))
                                           .normalized()
                                           .intersected(QRectF(0, 0, 1, 1)));
    return;
  }
  m_lastAreaMonitor = monitor;
  m_lastArea = QRectF(QPointF(x1, y1), QPointF(x2, y2))
                   .normalized()
                   .intersected(QRectF(0, 0, 1, 1));
  m_lastAreaPixels = imagePixels;
  QSettings settings;
  settings.setValue("lastArea/monitor", m_lastAreaMonitor);
  settings.setValue("lastArea/rect", m_lastArea);
  settings.setValue("lastArea/pixels", m_lastAreaPixels);
  const bool whole = m_lastArea.left() <= 0.001 && m_lastArea.top() <= 0.001 &&
                     m_lastArea.right() >= 0.999 && m_lastArea.bottom() >= 0.999;
  loadImage(result, whole ? "Display capture" : "Region capture", false);
  m_quickState = "choosing";
  emit changed();
  emit chooserRequested();
}
void Studio::cancelSelection() {
  if (m_quickState != "selecting")
    return;
  for (const auto &name : m_frozen.keys())
    m_store->put("capture/" + name, {});
  m_frozen.clear();
  m_busy = false;
  m_quickState = "cancelled";
  m_status = "Capture cancelled.";
  emit selectionDone();
  emit changed();
  emit dismissRequested();
}
void Studio::chooseFinish(int value) {
  if (!m_quickMode || (m_quickState != "choosing" && m_quickState != "failed"))
    return;
  if (m_busy || m_pendingFinish >= 0 || value < 0 || value > 8)
    return;
  if (!recoveryAction().isEmpty() && value == style()) {
    retryOutput();
    return;
  }
  setStyle(value);
  if (m_rendering)
    m_pendingFinish = value;
  else
    accept();
}
void Studio::openEditor() {
  if (m_busy || m_pendingFinish >= 0)
    return;
  m_quickState = "editing";
  emit changed();
  emit editorRequested();
}
void Studio::showFinishes() {
  if (!m_quickMode || m_busy)
    return;
  m_quickState = "choosing";
  emit changed();
  emit chooserRequested();
}
void Studio::dismissQuick() {
  if (m_busy || m_pendingFinish >= 0)
    return;
  m_quickState = "cancelled";
  emit changed();
  emit dismissRequested();
}

void Studio::recordInstead(const QString &monitor) {
  if (m_quickState != "selecting")
    return;
  if (!monitor.isEmpty())
    m_captureMonitor = monitor;
  // Stay in the selector: the same drag or click now starts a recording.
  m_recordingSelection = true;
  emit changed();
  emit recordModeEntered();
}
void Studio::recordingOptions(const QString &monitor) {
  if (m_quickState != "selecting")
    return;
  if (!monitor.isEmpty())
    m_captureMonitor = monitor;
  for (const auto &name : m_frozen.keys())
    m_store->put("capture/" + name, {});
  m_frozen.clear();
  m_busy = false;
  emit selectionDone();
  leaveQuickMode();
  emit recordOptionsRequested();
}
void Studio::captureVideo() {
  if (m_busy)
    return;
  m_recordingSelection = true;
  captureImpl(true, 0, false);
  emit recordModeEntered();
}
