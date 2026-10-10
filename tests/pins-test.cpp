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
  void opensExactlyOverItsArea() {
    const QRectF area(0.25, 0.5, 600.0 / 1920, 400.0 / 1080);
    QVERIFY(near(Pins::place(display, {600, 400}, area), QRectF(480, 540, 600, 400)));
    // Rounding in the crop still covers the area exactly.
    QVERIFY(near(Pins::place(display, {601, 399}, area), QRectF(480, 540, 600, 400)));
  }
  void framedImageCentresOnItsArea() {
    const QRectF area(0.25, 0.25, 600.0 / 1920, 400.0 / 1080);
    const QRectF rect = Pins::place(display, {720, 520}, area);
    QCOMPARE(rect.size(), QSizeF(720, 520));
    QCOMPARE(rect.center(), QPointF(480 + 300, 270 + 200));
  }
  void staysOnTheDisplay() {
    const QRectF rect = Pins::place(display, {720, 520}, QRectF(0, 0, 0.2, 0.2));
    QCOMPARE(rect.topLeft(), QPointF(0, 0));
    const QRectF corner = Pins::place(display, {720, 520}, QRectF(0.9, 0.9, 0.1, 0.1));
    QCOMPARE(corner.bottomRight(), QPointF(1920, 1080));
  }
  void wholeDisplayOpensSmaller() {
    const QRectF rect = Pins::place(display, {1920, 1080}, QRectF(0, 0, 1, 1));
    QVERIFY(rect.width() <= 1920 * 0.6 + 0.01);
    QCOMPARE(rect.center(), QPointF(960, 540));
    QCOMPARE(rect.width() / rect.height(), 1920.0 / 1080);
  }
  void withoutAreaOpensCentred() {
    QVERIFY(near(Pins::place(display, {400, 300}), QRectF(760, 390, 400, 300)));
    const QRectF big = Pins::place(display, {4000, 1000});
    QVERIFY(big.width() <= 960.01);
    QCOMPARE(big.width() / big.height(), 4.0);
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
    QCOMPARE(board.data(board.index(0), PinBoard::ZoomPercent).toInt(),
             qRound(200 * QGuiApplication::primaryScreen()->devicePixelRatio()));
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
