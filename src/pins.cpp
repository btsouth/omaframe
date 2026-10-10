#include "pins.hpp"
#include "studio.hpp"
#include <LayerShellQt/Window>
#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QImageWriter>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScreen>
#include <QUuid>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>

namespace Pins {
static QSizeF limits(QSizeF size, QSizeF natural, double *factor) {
  const double shortEdge = std::min(size.width(), size.height());
  const double smallest = std::min(minEdge, std::min(natural.width(), natural.height()));
  double scale = 1;
  if (shortEdge < smallest)
    scale = smallest / shortEdge;
  if (size.width() * scale > natural.width() * maxZoom)
    scale = natural.width() * maxZoom / size.width();
  if (factor)
    *factor = scale;
  return size * scale;
}
QRectF place(QSizeF display, QSizeF image, const QRectF &area) {
  if (display.isEmpty() || image.isEmpty())
    return {};
  const double room = std::max(1.0, display.width() - 2 * margin);
  const double scale = std::min({1.0, display.width() * 0.4 / image.width(),
                                 display.height() * 0.45 / image.height(),
                                 room / image.width()});
  QRectF rect(QPointF(), image * scale);
  // The corner farthest from the capture. Ties (a whole display) and no
  // capture at all go bottom right.
  const QRectF source = origin(display, area);
  const QPointF centre = source.isEmpty() ? QPointF() : source.center();
  const bool right = centre.x() <= display.width() / 2;
  const bool bottom = centre.y() <= display.height() / 2;
  rect.moveLeft(right ? display.width() - margin - rect.width() : margin);
  rect.moveTop(bottom ? display.height() - margin - rect.height() : topMargin);
  rect.moveTop(std::max(0.0, std::min(rect.top(), display.height() - rect.height())));
  return rect;
}
QRectF cascade(QRectF rect, QSizeF display, const QList<QRectF> &others) {
  const auto close = [](QPointF a, QPointF b) {
    return std::abs(a.x() - b.x()) < 8 && std::abs(a.y() - b.y()) < 8;
  };
  // Towards the middle of the display, from whichever corner it is in.
  const QPointF step(rect.center().x() > display.width() / 2 ? -28 : 28,
                     rect.center().y() > display.height() / 2 ? -28 : 28);
  for (int tries = 0; tries < 12; ++tries) {
    const bool taken = std::any_of(others.cbegin(), others.cend(), [&](const QRectF &o) {
      return close(o.topLeft(), rect.topLeft()) || close(o.topRight(), rect.topRight()) ||
             close(o.bottomLeft(), rect.bottomLeft()) ||
             close(o.bottomRight(), rect.bottomRight());
    });
    if (!taken)
      break;
    rect.translate(step);
  }
  return rect;
}
QRectF origin(QSizeF display, const QRectF &area) {
  if (!area.isValid() || area.isEmpty())
    return {};
  return QRectF(area.x() * display.width(), area.y() * display.height(),
                area.width() * display.width(), area.height() * display.height());
}
QRectF zoom(const QRectF &rect, double factor, QPointF anchor, QSizeF natural) {
  if (rect.isEmpty() || natural.isEmpty() || !(factor > 0))
    return rect;
  double extra = 1;
  const QSizeF size = limits(rect.size() * factor, natural, &extra);
  const double applied = size.width() / rect.width();
  return QRectF(anchor - (anchor - rect.topLeft()) * applied, size);
}
QRectF resize(const QRectF &rect, QPointF fixed, QPointF pointer, QSizeF natural) {
  if (rect.isEmpty() || natural.isEmpty())
    return rect;
  const double aspect = rect.width() / rect.height();
  const double width = std::max(std::abs(pointer.x() - fixed.x()),
                                std::abs(pointer.y() - fixed.y()) * aspect);
  const QSizeF size = limits(QSizeF(std::max(width, 1.0), std::max(width, 1.0) / aspect),
                             natural, nullptr);
  return QRectF(pointer.x() < fixed.x() ? fixed.x() - size.width() : fixed.x(),
                pointer.y() < fixed.y() ? fixed.y() - size.height() : fixed.y(),
                size.width(), size.height());
}
QRectF keepReachable(const QRectF &rect, QSizeF display, double keep) {
  QRectF r = rect;
  const double kx = std::min(keep, r.width()), ky = std::min(keep, r.height());
  r.moveLeft(std::clamp(r.left(), kx - r.width(), display.width() - kx));
  // The top edge stays on screen: that is where a pin is usually grabbed.
  r.moveTop(std::clamp(r.top(), 0.0, std::max(0.0, display.height() - ky)));
  return r;
}
QRectF badge(const QRectF &rect) {
  return QRectF(rect.right() - 34, rect.top() + 6, 28, 28);
}
} // namespace Pins

QImage PinImages::requestImage(const QString &id, QSize *size, const QSize &) {
  QMutexLocker lock(&mutex);
  const QImage image = images.value(id.section('?', 0, 0).toInt());
  if (size)
    *size = image.size();
  return image;
}
void PinImages::put(int id, const QImage &image) {
  QMutexLocker lock(&mutex);
  images.insert(id, image);
}
void PinImages::remove(int id) {
  QMutexLocker lock(&mutex);
  images.remove(id);
}

PinBoard::PinBoard(PinImages *images, std::function<QString()> directory,
                   QObject *parent)
    : QAbstractListModel(parent), m_images(images),
      m_directory(std::move(directory)) {
  // Masks follow the pins, at most once per event loop pass.
  m_sync.setSingleShot(true);
  m_sync.setInterval(0);
  connect(&m_sync, &QTimer::timeout, this, &PinBoard::syncOverlays);
  connect(qGuiApp, &QGuiApplication::screenRemoved, this, &PinBoard::screenRemoved);
  connect(qGuiApp, &QGuiApplication::screenAdded, this, &PinBoard::screenAdded);
}
PinBoard::~PinBoard() {
  for (auto *overlay : std::as_const(m_overlays))
    delete overlay;
}
int PinBoard::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : m_pins.size();
}
QHash<int, QByteArray> PinBoard::roleNames() const {
  return {{PinId, "pinId"},         {Screen, "screen"},
          {PinX, "pinX"},           {PinY, "pinY"},
          {PinWidth, "pinWidth"},   {PinHeight, "pinHeight"},
          {PinOpacity, "pinOpacity"}, {ClickThrough, "clickThrough"},
          {Stack, "stack"},         {Source, "source"},
          {ZoomPercent, "zoomPercent"}, {Created, "created"},
          {Origin, "origin"}};
}
QVariant PinBoard::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() >= m_pins.size())
    return {};
  const Pin &pin = m_pins[index.row()];
  switch (role) {
  case PinId: return pin.id;
  case Screen: return pin.screen;
  case PinX: return pin.rect.x();
  case PinY: return pin.rect.y();
  case PinWidth: return pin.rect.width();
  case PinHeight: return pin.rect.height();
  case PinOpacity: return pin.opacity;
  case ClickThrough: return pin.clickThrough;
  case Stack: return pin.stack;
  case Source: return QString("image://pins/%1").arg(pin.id);
  case Created: return double(pin.created);
  case Origin: return pin.origin;
  case ZoomPercent: {
    const double natural = pin.image.width() / pin.scale;
    return natural > 0 ? qRound(pin.rect.width() / natural * 100) : 100;
  }
  }
  return {};
}
int PinBoard::row(int id) const {
  for (int i = 0; i < m_pins.size(); ++i)
    if (m_pins[i].id == id)
      return i;
  return -1;
}
void PinBoard::changedAt(int row, const QList<int> &roles) {
  emit dataChanged(index(row), index(row), roles);
  m_sync.start();
}
QScreen *PinBoard::screen(const QString &name) const {
  for (auto *s : QGuiApplication::screens())
    if (s->name() == name)
      return s;
  return nullptr;
}
static QSizeF natural(const QImage &image, double scale) {
  return QSizeF(image.size()) / scale;
}
static bool usable(QScreen *screen) {
  return screen && !screen->geometry().isEmpty();
}
void PinBoard::add(QImage image, const QString &monitor, const QRectF &area,
                   double scale) {
  if (image.isNull())
    return;
  QScreen *target = screen(monitor);
  if (!usable(target))
    target = QGuiApplication::primaryScreen();
  if (!usable(target))
    return;
  image.setDevicePixelRatio(1);
  Pin pin;
  pin.id = m_nextId++;
  pin.stack = m_nextStack++;
  pin.screen = target->name();
  // Qt reports a fractional display scale rounded up, so the caller's
  // measured scale wins when it has one.
  pin.scale = scale > 0 ? scale : std::max(1.0, target->devicePixelRatio());
  QList<QRectF> others;
  for (const auto &other : std::as_const(m_pins))
    others << other.rect.translated(-target->geometry().topLeft());
  pin.rect = Pins::cascade(Pins::place(target->geometry().size(), natural(image, pin.scale), area),
                           target->geometry().size(), others)
                 .translated(target->geometry().topLeft());
  pin.origin = Pins::origin(target->geometry().size(), area)
                   .translated(target->geometry().topLeft());
  pin.created = QDateTime::currentMSecsSinceEpoch();
  pin.image = std::move(image);
  m_images->put(pin.id, pin.image);
  beginInsertRows({}, m_pins.size(), m_pins.size());
  m_pins.append(std::move(pin));
  endInsertRows();
  emit countChanged();
  if (m_engine && !showOverlay(target->name())) {
    // Without an overlay it could never be seen or closed, and would keep
    // Omaframe running.
    close(m_pins.last().id);
    return;
  }
  m_sync.start();
}
void PinBoard::move(int id, double x, double y) {
  const int r = row(id);
  if (r < 0)
    return;
  m_pins[r].rect.moveTo(x, y);
  changedAt(r, {PinX, PinY});
}
void PinBoard::zoomBy(int id, double factor, double anchorX, double anchorY) {
  const int r = row(id);
  if (r < 0)
    return;
  Pin &pin = m_pins[r];
  pin.rect = Pins::zoom(pin.rect, factor, {anchorX, anchorY},
                        natural(pin.image, pin.scale));
  changedAt(r, {PinX, PinY, PinWidth, PinHeight, ZoomPercent});
}
void PinBoard::resizeTo(int id, double fixedX, double fixedY, double pointerX,
                        double pointerY) {
  const int r = row(id);
  if (r < 0)
    return;
  Pin &pin = m_pins[r];
  pin.rect = Pins::resize(pin.rect, {fixedX, fixedY}, {pointerX, pointerY},
                          natural(pin.image, pin.scale));
  changedAt(r, {PinX, PinY, PinWidth, PinHeight, ZoomPercent});
}
void PinBoard::actualSize(int id) {
  const int r = row(id);
  if (r < 0)
    return;
  Pin &pin = m_pins[r];
  const QPointF centre = pin.rect.center();
  pin.rect.setSize(natural(pin.image, pin.scale));
  pin.rect.moveCenter(centre);
  keepReachable(pin);
  changedAt(r, {PinX, PinY, PinWidth, PinHeight, ZoomPercent});
}
void PinBoard::setOpacity(int id, double opacity) {
  const int r = row(id);
  if (r < 0)
    return;
  m_pins[r].opacity = std::clamp(opacity, 0.15, 1.0);
  changedAt(r, {PinOpacity});
}
void PinBoard::setClickThrough(int id, bool on) {
  const int r = row(id);
  if (r < 0 || m_pins[r].clickThrough == on)
    return;
  m_pins[r].clickThrough = on;
  changedAt(r, {ClickThrough});
}
void PinBoard::raise(int id) {
  const int r = row(id);
  if (r < 0 || m_pins[r].stack == m_nextStack - 1)
    return;
  m_pins[r].stack = m_nextStack++;
  changedAt(r, {Stack});
}
void PinBoard::keepReachable(Pin &pin) {
  if (auto *s = screen(pin.screen); usable(s)) {
    const QPointF origin = s->geometry().topLeft();
    pin.rect = Pins::keepReachable(pin.rect.translated(-origin), s->geometry().size())
                   .translated(origin);
  }
}
void PinBoard::hold(const QString &screen) {
  m_held = screen;
}
void PinBoard::dropped(int id) {
  m_held.clear();
  m_sync.start();
  const int r = row(id);
  if (r < 0)
    return;
  Pin &pin = m_pins[r];
  QScreen *from = screen(pin.screen), *to = from;
  for (auto *s : QGuiApplication::screens())
    if (usable(s) && QRectF(s->geometry()).contains(pin.rect.center()))
      to = s;
  if (to && to != from) {
    // Its 100% follows the display it is on now. Qt's whole-number scales
    // are only good for the ratio between two displays.
    if (from)
      pin.scale = std::max(0.25, pin.scale * to->devicePixelRatio() / from->devicePixelRatio());
    pin.screen = to->name();
  }
  keepReachable(pin);
  changedAt(r, {Screen, PinX, PinY, ZoomPercent});
}
static QByteArray encodePng(const QImage &image, QString &error) {
  QByteArray png;
  QBuffer buffer(&png);
  buffer.open(QIODevice::WriteOnly);
  QImageWriter writer(&buffer, "png");
  writer.setCompression(60);
  if (!writer.write(image)) {
    error = writer.errorString();
    return {};
  }
  return png;
}
void PinBoard::copy(int id) {
  const int r = row(id);
  if (r < 0 || m_pins[r].busy)
    return;
  m_pins[r].busy = true;
  auto *watcher = new QFutureWatcher<QString>(this);
  connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, id] {
    const QString error = watcher->result();
    watcher->deleteLater();
    if (const int r = row(id); r >= 0)
      m_pins[r].busy = false;
    emit notice(id, error.isEmpty() ? "Copied" : "Could not copy: " + error);
  });
  watcher->setFuture(QtConcurrent::run([image = m_pins[r].image] {
    QString error;
    const QByteArray png = encodePng(image, error);
    if (!png.isEmpty())
      copyPngBytes(png, error);
    return error;
  }));
}
void PinBoard::save(int id) {
  const int r = row(id);
  if (r < 0 || m_pins[r].busy)
    return;
  m_pins[r].busy = true;
  struct Saved {
    QString path, error;
  };
  auto *watcher = new QFutureWatcher<Saved>(this);
  connect(watcher, &QFutureWatcher<Saved>::finished, this, [this, watcher, id] {
    const Saved result = watcher->result();
    watcher->deleteLater();
    if (const int r = row(id); r >= 0)
      m_pins[r].busy = false;
    if (result.path.isEmpty()) {
      emit notice(id, "Could not save: " + result.error);
      return;
    }
    emit notice(id, "Saved in " + QFileInfo(result.path).absolutePath().replace(QDir::homePath(), "~"));
    emit saved(result.path);
  });
  watcher->setFuture(QtConcurrent::run([image = m_pins[r].image,
                                        directory = m_directory()] {
    Saved result;
    if (!QDir().mkpath(directory)) {
      result.error = "the screenshot folder could not be created.";
      return result;
    }
    // Named like every other saved screenshot, so History lists it.
    const QString path =
        directory + "/Omaframe-" +
        QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss-zzz") + "-" +
        QUuid::createUuid().toString(QUuid::Id128).left(6) + ".png";
    if (writePng(path, image, result.error))
      result.path = path;
    return result;
  }));
}
void PinBoard::close(int id) {
  const int r = row(id);
  if (r < 0)
    return;
  beginRemoveRows({}, r, r);
  m_pins.removeAt(r);
  endRemoveRows();
  m_images->remove(id);
  emit countChanged();
  m_sync.start();
  if (m_pins.isEmpty())
    emit emptied();
}
void PinBoard::closeAll() {
  if (m_pins.isEmpty())
    return;
  beginResetModel();
  for (const auto &pin : std::as_const(m_pins))
    m_images->remove(pin.id);
  m_pins.clear();
  endResetModel();
  m_menus.clear();
  emit countChanged();
  m_sync.start();
  emit emptied();
}
void PinBoard::setMenuOpen(const QString &screen, bool open) {
  m_menus.removeAll(screen);
  if (open)
    m_menus << screen;
  syncOverlays();
}
void PinBoard::setKeyboard(const QString &screen, bool on) {
  if (auto *overlay = m_overlays.value(screen))
    LayerShellQt::Window::get(overlay)->setKeyboardInteractivity(
        on ? LayerShellQt::Window::KeyboardInteractivityOnDemand
           : LayerShellQt::Window::KeyboardInteractivityNone);
}
bool PinBoard::showOverlay(const QString &name) {
  if (m_overlays.contains(name))
    return true;
  QScreen *target = screen(name);
  if (!m_engine || !usable(target))
    return false;
  QQmlComponent component(m_engine, QUrl("qrc:/qml/PinOverlay.qml"));
  auto *overlay = qobject_cast<QQuickWindow *>(component.createWithInitialProperties(
      {{"screenName", name},
       {"originX", target->geometry().x()},
       {"originY", target->geometry().y()}}));
  if (!overlay) {
    qWarning("Could not create the pin overlay: %s", qPrintable(component.errorString()));
    return false;
  }
  // The top layer: above windows like the bar, below a fullscreen video
  // and below Omaframe's own capture surfaces, which are overlays.
  auto *layer = LayerShellQt::Window::get(overlay);
  layer->setScope("omaframe-pins");
  layer->setLayer(LayerShellQt::Window::LayerTop);
  layer->setExclusiveZone(-1);
  layer->setAnchors(LayerShellQt::Window::Anchors::fromInt(
      LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom |
      LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
  // Hyprland focuses an on-demand layer as soon as it maps, which would
  // take the keyboard from the window being typed in. It starts with none.
  layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
  overlay->setScreen(target);
  layer->setScreen(target);
  overlay->setGeometry(target->geometry());
  m_overlays.insert(name, overlay);
  // A display that changes size keeps its pins within reach.
  connect(target, &QScreen::geometryChanged, overlay, [this, name](const QRect &geometry) {
    if (auto *o = m_overlays.value(name)) {
      o->setProperty("originX", geometry.x());
      o->setProperty("originY", geometry.y());
      o->resize(geometry.size());
    }
    for (int i = 0; i < m_pins.size(); ++i)
      if (m_pins[i].screen == name) {
        keepReachable(m_pins[i]);
        changedAt(i, {PinX, PinY});
      }
  });
  return true;
}
void PinBoard::syncOverlays() {
  for (auto *display : QGuiApplication::screens()) {
    const QString name = display->name();
    const QRectF bounds = display->geometry();
    QRegion region;
    bool any = false;
    for (const auto &pin : std::as_const(m_pins)) {
      if (!usable(display) || !pin.rect.intersects(bounds))
        continue;
      any = true;
      const QRectF part = pin.clickThrough ? Pins::badge(pin.rect) : pin.rect;
      region += part.intersected(bounds).translated(-bounds.topLeft()).toAlignedRect();
    }
    QQuickWindow *overlay = m_overlays.value(name);
    // The overlay a pin is dragged from keeps its grab on the pointer.
    if (name == m_held && overlay) {
      if (overlay->mask() != region && !region.isEmpty())
        overlay->setMask(region);
      continue;
    }
    if (!any) {
      // Nothing left on this display: its overlay goes, so no invisible
      // surface stays on top of the desktop.
      m_menus.removeAll(name);
      if (overlay) {
        m_overlays.remove(name);
        overlay->hide();
        overlay->deleteLater();
      }
      continue;
    }
    if (!overlay && !showOverlay(name))
      continue;
    overlay = m_overlays.value(name);
    if (m_menus.contains(name))
      region = QRegion(QRect(QPoint(), overlay->size()));
    if (overlay->mask() != region)
      overlay->setMask(region);
    if (!overlay->isVisible())
      overlay->show();
  }
}
void PinBoard::screenRemoved(QScreen *removed) {
  const QString name = removed->name();
  if (auto *overlay = m_overlays.take(name)) {
    overlay->hide();
    overlay->deleteLater();
  }
  m_menus.removeAll(name);
  QScreen *fallback = nullptr;
  for (auto *s : QGuiApplication::screens())
    if (s != removed && usable(s)) {
      fallback = s;
      break;
    }
  // With no display left (a dock or KVM switch), pins wait for one to come
  // back; screenAdded() places them then.
  if (fallback)
    rehome(fallback, removed);
}
void PinBoard::screenAdded(QScreen *added) {
  rehome(added);
}
void PinBoard::rehome(QScreen *added, QScreen *gone) {
  if (!usable(added))
    return;
  // Pins whose display is gone move to the middle of this one.
  bool moved = false;
  for (int i = 0; i < m_pins.size(); ++i) {
    QScreen *current = screen(m_pins[i].screen);
    if (current != gone && usable(current))
      continue;
    Pin &pin = m_pins[i];
    pin.screen = added->name();
    pin.rect = Pins::place(added->geometry().size(), pin.rect.size())
                   .translated(added->geometry().topLeft());
    changedAt(i, {Screen, PinX, PinY, PinWidth, PinHeight, ZoomPercent});
    moved = true;
  }
  if (moved)
    m_sync.start();
}
