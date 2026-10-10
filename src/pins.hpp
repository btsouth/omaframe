#pragma once
#include <QAbstractListModel>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>
#include <QRectF>
#include <QTimer>
#include <functional>

class QQmlEngine;
class QQuickWindow;
class QScreen;

/** Geometry for screenshots pinned to the screen. Rectangles are logical
 *  pixels relative to their display's top left. */
namespace Pins {
/** The smallest a pin gets along its short edge, and how far it can grow
 *  past its natural size. */
constexpr double minEdge = 48;
constexpr double maxZoom = 8;
/** Where a new pin of `image` (logical pixels) opens on a display of
 *  `display` size. `area` is the part of the display it was captured from,
 *  as fractions; the pin covers it exactly, so the screen looks frozen there.
 *  A larger finished image stays centred on that area. Without an area it
 *  opens centred, at most half the display. Either way it fits the display. */
QRectF place(QSizeF display, QSizeF image, const QRectF &area = {});
/** `rect` scaled by `factor` around `anchor`, keeping its shape, between
 *  minEdge and maxZoom times `natural`. */
QRectF zoom(const QRectF &rect, double factor, QPointF anchor, QSizeF natural);
/** `rect` resized from the corner opposite `fixed` towards `pointer`,
 *  keeping its shape, within the same limits as zoom(). */
QRectF resize(const QRectF &rect, QPointF fixed, QPointF pointer, QSizeF natural);
/** Moves `rect` the least distance that keeps at least `keep` pixels of it
 *  on a display of `display` size, so a pin cannot be lost off an edge. */
QRectF keepReachable(const QRectF &rect, QSizeF display, double keep = 40);
/** Where the badge of a click-through pin sits; the only part of it that
 *  still takes the pointer. */
QRectF badge(const QRectF &rect);
} // namespace Pins

class PinImages final : public QQuickImageProvider {
public:
  PinImages() : QQuickImageProvider(Image) {}
  QImage requestImage(const QString &id, QSize *size, const QSize &) override;
  void put(int id, const QImage &image);
  void remove(int id);

private:
  QMutex mutex;
  QHash<int, QImage> images;
};

/** Screenshots pinned to the screen. Every display with a pin gets one
 *  transparent overlay that draws all of its pins and only takes the pointer
 *  where they are, so moving and resizing never waits on the compositor. */
class PinBoard final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
  enum Role {
    PinId = Qt::UserRole + 1,
    Screen,
    PinX,
    PinY,
    PinWidth,
    PinHeight,
    PinOpacity,
    ClickThrough,
    Stack,
    Source,
    ZoomPercent
  };
  /** `directory` is where Save writes, the screenshot folder. */
  PinBoard(PinImages *images, std::function<QString()> directory,
           QObject *parent = nullptr);
  ~PinBoard() override;
  /** The overlays' window title, so other code can tell them apart. */
  static constexpr const char *overlayTitle = "Omaframe pins";
  int rowCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  int count() const { return m_pins.size(); }
  /** Creates the overlays with this engine; call once before add(). */
  void setEngine(QQmlEngine *engine) { m_engine = engine; }
  /** Pins `image` on display `monitor` (or the first one), over `area`
   *  when it came from there. The image's device pixel ratio is ignored: it
   *  shows at one image pixel per display pixel. */
  void add(QImage image, const QString &monitor, const QRectF &area = {});
  Q_INVOKABLE void move(int id, double x, double y);
  Q_INVOKABLE void zoomBy(int id, double factor, double anchorX, double anchorY);
  Q_INVOKABLE void resizeTo(int id, double fixedX, double fixedY,
                            double pointerX, double pointerY);
  Q_INVOKABLE void actualSize(int id);
  Q_INVOKABLE void setOpacity(int id, double opacity);
  Q_INVOKABLE void setClickThrough(int id, bool on);
  Q_INVOKABLE void raise(int id);
  /** A drag ended with the pointer at `x, y` on `screen`'s overlay. A pin
   *  dropped mostly on another display moves there. */
  Q_INVOKABLE void dropped(int id, const QString &screen, double x, double y);
  Q_INVOKABLE void copy(int id);
  Q_INVOKABLE void save(int id);
  Q_INVOKABLE void close(int id);
  Q_INVOKABLE void closeAll();
  /** While a menu is open its overlay takes every click, so one outside
   *  closes the menu instead of reaching the window below. */
  Q_INVOKABLE void setMenuOpen(const QString &screen, bool open);
  /** Lets `screen`'s overlay take the keyboard, after a pin there is
   *  clicked, or gives it back. A new pin never takes it by itself. */
  Q_INVOKABLE void setKeyboard(const QString &screen, bool on);
signals:
  void countChanged();
  /** A short note to show on the pin, such as "Copied". */
  void notice(int id, const QString &text);
  /** Save wrote a PNG into the screenshot folder. */
  void saved(const QString &path);
  /** The last pin closed. */
  void emptied();

private:
  struct Pin {
    int id = 0;
    QImage image;
    QString screen;
    QRectF rect;
    double opacity = 1;
    bool clickThrough = false;
    int stack = 0;
    bool busy = false;
  };
  int row(int id) const;
  void changedAt(int row, const QList<int> &roles);
  QScreen *screen(const QString &name) const;
  void showOverlay(const QString &screen);
  void syncOverlays();
  void screenRemoved(QScreen *screen);
  PinImages *m_images;
  std::function<QString()> m_directory;
  QQmlEngine *m_engine = nullptr;
  QList<Pin> m_pins;
  QHash<QString, QQuickWindow *> m_overlays;
  QStringList m_menus;
  QTimer m_sync;
  int m_nextId = 1, m_nextStack = 1;
};
