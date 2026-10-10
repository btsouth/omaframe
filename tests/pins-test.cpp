#include "pins.hpp"
#include <QGuiApplication>
#include <QScreen>
#include <QSignalSpy>
#include <QTest>

class PinsTest : public QObject {
  Q_OBJECT
  static constexpr QSizeF display{1920, 1080};
  static bool near(const QRectF &a, const QRectF &b) {
    return std::abs(a.x() - b.x()) < 0.01 && std::abs(a.y() - b.y()) < 0.01 &&
           std::abs(a.width() - b.width()) < 0.01 &&
           std::abs(a.height() - b.height()) < 0.01;
  }
private slots:
  void restsInTheCornerFarthestFromItsArea() {
    using Pins::margin, Pins::topMargin;
    // Captured top left: it goes bottom right, at its own size.
    const QRectF topLeft = Pins::place(display, {400, 300}, QRectF(0.1, 0.1, 0.2, 0.2));
    QVERIFY(near(topLeft, QRectF(1920 - margin - 400, 1080 - margin - 300, 400, 300)));
    // Captured bottom right: top left, below the bar.
    const QRectF bottomRight = Pins::place(display, {400, 300}, QRectF(0.7, 0.7, 0.2, 0.2));
    QVERIFY(near(bottomRight, QRectF(margin, topMargin, 400, 300)));
    // Captured top right: bottom left.
    const QRectF topRight = Pins::place(display, {400, 300}, QRectF(0.7, 0.1, 0.2, 0.2));
    QCOMPARE(topRight.topLeft(), QPointF(margin, 1080 - margin - 300));
  }
  void withoutAreaRestsBottomRight() {
    QVERIFY(near(Pins::place(display, {400, 300}),
                 QRectF(1920 - Pins::margin - 400, 1080 - Pins::margin - 300, 400, 300)));
    // A whole display ties, and goes bottom right too.
    QCOMPARE(Pins::place(display, {400, 300}, QRectF(0, 0, 1, 1)).bottomRight(),
             QPointF(1920 - Pins::margin, 1080 - Pins::margin));
  }
  void largeCapturesRestSmaller() {
    const QRectF whole = Pins::place(display, {1920, 1080}, QRectF(0, 0, 1, 1));
    QVERIFY(whole.width() <= 1920 * 0.4 + 0.01);
    QCOMPARE(whole.width() / whole.height(), 1920.0 / 1080);
    const QRectF tall = Pins::place(display, {800, 6000});
    QVERIFY(tall.height() <= 1080 * 0.45 + 0.01);
    QCOMPARE(tall.width() / tall.height(), 800.0 / 6000);
  }
  void newPinsStepInFromOnesInTheCorner() {
    const QRectF first = Pins::place(display, {400, 300});
    const QRectF second = Pins::cascade(first, display, {first});
    QCOMPARE(second.topLeft(), first.topLeft() - QPointF(28, 28));
    const QRectF third = Pins::cascade(first, display, {first, second});
    QCOMPARE(third.topLeft(), first.topLeft() - QPointF(56, 56));
    // A pin elsewhere is no reason to move.
    QCOMPARE(Pins::cascade(first, display, {QRectF(0, 0, 50, 50)}), first);
  }
  void startsOverItsArea() {
    QVERIFY(near(Pins::origin(display, QRectF(0.25, 0.5, 0.5, 0.25)),
                 QRectF(480, 540, 960, 270)));
    QVERIFY(Pins::origin(display, {}).isEmpty());
  }
  void zoomKeepsThePointUnderTheCursor() {
    const QRectF rect(100, 100, 400, 200);
    const QPointF anchor(200, 150);
    const QRectF zoomed = Pins::zoom(rect, 2, anchor, {400, 200});
    QCOMPARE(zoomed.size(), QSizeF(800, 400));
    // The anchor stays at the same fraction of the pin.
    QCOMPARE((anchor.x() - zoomed.x()) / zoomed.width(), 0.25);
    QCOMPARE((anchor.y() - zoomed.y()) / zoomed.height(), 0.25);
  }
  void zoomHasLimits() {
    const QRectF rect(0, 0, 400, 200);
    QCOMPARE(Pins::zoom(rect, 0.001, {}, {400, 200}).height(), Pins::minEdge);
    QCOMPARE(Pins::zoom(rect, 1000, {}, {400, 200}).width(), 400 * Pins::maxZoom);
    // A tiny capture is never forced larger than itself.
    QCOMPARE(Pins::zoom(QRectF(0, 0, 30, 20), 0.5, {}, {30, 20}).size(), QSizeF(30, 20));
  }
  void cornerResizeKeepsShapeAndOppositeCorner() {
    const QRectF rect(100, 100, 400, 200);
    const QRectF grown = Pins::resize(rect, rect.topLeft(), {700, 200}, {400, 200});
    QCOMPARE(grown.topLeft(), rect.topLeft());
    QCOMPARE(grown.size(), QSizeF(600, 300));
    const QRectF fromTopLeft = Pins::resize(rect, rect.bottomRight(), {300, 250}, {400, 200});
    QCOMPARE(fromTopLeft.bottomRight(), rect.bottomRight());
    QCOMPARE(fromTopLeft.width() / fromTopLeft.height(), 2.0);
  }
  void cannotBeLostOffAnEdge() {
    const QRectF gone = Pins::keepReachable(QRectF(5000, -300, 400, 200), display);
    QCOMPARE(gone.left(), 1920.0 - 40);
    QCOMPARE(gone.top(), 0.0);
    const QRectF left = Pins::keepReachable(QRectF(-1000, 500, 400, 200), display);
    QCOMPARE(left.right(), 40.0);
    const QRectF inside(10, 10, 100, 100);
    QCOMPARE(Pins::keepReachable(inside, display), inside);
  }
  void fractionalScaleShowsOnePixelPerPixel() {
    auto *images = new PinImages;
    PinBoard board(images, [] { return QString(); });
    QImage image(300, 150, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::blue);
    board.add(image, QGuiApplication::primaryScreen()->name(), {}, 1.5);
    QCOMPARE(board.data(board.index(0), PinBoard::PinWidth).toDouble(), 200.0);
    QCOMPARE(board.data(board.index(0), PinBoard::ZoomPercent).toInt(), 100);
    const int id = board.data(board.index(0), PinBoard::PinId).toInt();
    board.zoomBy(id, 3, 0, 0);
    board.actualSize(id);
    QCOMPARE(board.data(board.index(0), PinBoard::PinHeight).toDouble(), 100.0);
    delete images;
  }
  void boardKeepsPinsUntilClosed() {
    auto *images = new PinImages;
    PinBoard board(images, [] { return QString(); });
    QSignalSpy emptied(&board, &PinBoard::emptied);
    QImage image(200, 100, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::red);
    board.add(image, "no such display");
    board.add(image, QGuiApplication::primaryScreen()->name());
    QCOMPARE(board.count(), 2);
    const int first = board.data(board.index(0), PinBoard::PinId).toInt();
    const int second = board.data(board.index(1), PinBoard::PinId).toInt();
    QCOMPARE(board.data(board.index(0), PinBoard::Screen).toString(),
             QGuiApplication::primaryScreen()->name());
    QCOMPARE(images->requestImage(QString::number(first), nullptr, {}).size(), QSize(200, 100));
    board.move(first, 12, 34);
    QCOMPARE(board.data(board.index(0), PinBoard::PinX).toDouble(), 12.0);
    board.setOpacity(first, 0);
    QCOMPARE(board.data(board.index(0), PinBoard::PinOpacity).toDouble(), 0.15);
    board.zoomBy(first, 2, 12, 34);
    QCOMPARE(board.data(board.index(0), PinBoard::ZoomPercent).toInt(), 200);
    board.actualSize(first);
    QCOMPARE(board.data(board.index(0), PinBoard::ZoomPercent).toInt(), 100);
    board.raise(first);
    QVERIFY(board.data(board.index(0), PinBoard::Stack).toInt() >
            board.data(board.index(1), PinBoard::Stack).toInt());
    board.close(first);
    QCOMPARE(board.count(), 1);
    QCOMPARE(emptied.count(), 0);
    QVERIFY(images->requestImage(QString::number(first), nullptr, {}).isNull());
    board.close(second);
    QCOMPARE(emptied.count(), 1);
    delete images;
  }
};

int main(int argc, char **argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  PinsTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "pins-test.moc"
