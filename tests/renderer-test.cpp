#include "renderer.hpp"
#include <QLinearGradient>
#include <QPainter>
#include <QTest>

class RendererTest : public QObject {
  Q_OBJECT
private slots:
  void adaptiveKeepsDominantHue_data() {
    QTest::addColumn<QColor>("color");
    QTest::newRow("bright-green") << QColor("#00ff00");
    QTest::newRow("forest-green") << QColor("#087934");
    QTest::newRow("warm-red-wrap") << QColor("#d50818");
    QTest::newRow("gold") << QColor("#e8c724");
  }
  void adaptiveKeepsDominantHue() {
    QFETCH(QColor, color);
    QImage source(600, 400, QImage::Format_ARGB32_Premultiplied);
    source.fill(color);
    QPainter p(&source);
    p.fillRect(0, 0, 600, 50, QColor("#eeeeee"));
    p.fillRect(0, 350, 600, 50, QColor("#151515"));
    p.end();
    auto result = Frame::compose(source, {4, 0.12, 0});
    for (QPoint point :
         {QPoint(0, 0), QPoint(result.width() - 1, result.height() - 1)}) {
      const QColor border = result.pixelColor(point);
      double delta = std::abs(border.hsvHueF() - color.hsvHueF());
      delta = std::min(delta, 1. - delta);
      QVERIFY2(delta < 0.035, qPrintable(border.name()));
      QVERIFY(border.hsvSaturationF() > 0.25);
    }
  }
  void adaptiveNeutralIgnoresTinyAccent() {
    QImage source(600, 400, QImage::Format_ARGB32_Premultiplied);
    source.fill(QColor("#383838"));
    QPainter p(&source);
    p.fillRect(0, 0, 5, 5, Qt::blue);
    p.end();
    auto result = Frame::compose(source, {4, 0.12, 0});
    QVERIFY(result.pixelColor(0, 0).hsvSaturationF() < 0.05);
  }
  void rawPreservesPixels() {
    QImage source(127, 81, QImage::Format_ARGB32_Premultiplied);
    source.fill(QColor("#e21c87"));
    auto result = Frame::compose(source, {8, 0.09, 2});
    QCOMPARE(result, source);
  }
  void allStylesKeepSizeAndContent() {
    QImage source(800, 500, QImage::Format_ARGB32_Premultiplied);
    source.fill(QColor("#e21c87"));
    for (int style = 0; style < 8; ++style) {
      auto options = Frame::Options{};
      options.style = style;
      const QImage result = Frame::compose(source, options);
      QCOMPARE(result.size(), style == 5 ? QSize(830, 530) : QSize(850, 550));
      QCOMPARE(result.pixelColor(result.width() / 2, result.height() / 2),
               QColor("#e21c87"));
      QVERIFY(result.pixelColor(0, 0) != QColor("#e21c87"));
    }
  }
  // A dialog cropped tight: flat background, text a few pixels from the
  // left and top edges.
  static QImage crampedCapture() {
    QImage source(800, 500, QImage::Format_ARGB32_Premultiplied);
    source.fill(QColor("#1e1e2e"));
    QPainter p(&source);
    p.fillRect(4, 4, 300, 20, QColor("#cdd6f4"));
    p.fillRect(4, 200, 700, 20, QColor("#cdd6f4"));
    return source;
  }
  void crampedFlatEdgesGetRoom() {
    const QImage source = crampedCapture();
    // Room is 4 percent of the short side, less what the edge already has.
    const QMargins room = Frame::edgeRoom(source);
    QCOMPARE(room.left(), 16);
    QCOMPARE(room.top(), 16);
    // Right and bottom already have more than enough flat space.
    QCOMPARE(room.right(), 0);
    QCOMPARE(room.bottom(), 0);
    const Frame::Options options{};
    const QImage framed = Frame::compose(source, options);
    QCOMPARE(framed.size(), Frame::outputSize(source.size(), options, room));
    QVERIFY(framed.width() > Frame::outputSize(source.size(), options).width());
    // Raw is the capture, untouched.
    QCOMPARE(Frame::compose(source, {8, 0.05, 0}), source);
  }
  void busyEdgesGetNoRoom() {
    // A photo or a gradient reaches the edge; nothing can be carried out.
    QImage source(800, 500, QImage::Format_ARGB32_Premultiplied);
    QPainter p(&source);
    QLinearGradient gradient(0, 0, 800, 500);
    gradient.setColorAt(0, Qt::darkBlue);
    gradient.setColorAt(1, QColor("#e8b6d3"));
    p.fillRect(source.rect(), gradient);
    p.end();
    QVERIFY(Frame::edgeRoom(source).isNull());
    // Text that touches the edge makes it busy too.
    QImage touching = crampedCapture();
    QPainter t(&touching);
    for (int x = 0; x < 800; x += 6)
      t.fillRect(x, 0, 3, 12, QColor("#cdd6f4"));
    t.end();
    QCOMPARE(Frame::edgeRoom(touching).top(), 0);
    // A capture that already has room, or is one color, keeps its size.
    QImage solid(800, 500, QImage::Format_ARGB32_Premultiplied);
    solid.fill(QColor("#e21c87"));
    QVERIFY(Frame::edgeRoom(solid).isNull());
  }
  void roundedCornersDoNotHideAFlatEdge() {
    QImage source = crampedCapture();
    QPainter p(&source);
    // The wallpaper behind a rounded window corner.
    for (QPoint corner : {QPoint(0, 0), QPoint(790, 0), QPoint(0, 490), QPoint(790, 490)})
      p.fillRect(QRect(corner, QSize(10, 10)), Qt::green);
    p.end();
    QCOMPARE(Frame::edgeRoom(source).left(), 16);
  }
  void paddingFitsSmallAndLargeCaptures() {
    const auto options = Frame::Options{};
    QCOMPARE(Frame::outputSize({200, 100}, options), QSize(232, 132));
    QCOMPARE(Frame::outputSize({3840, 2160}, options), QSize(4032, 2352));
    auto outline = options;
    outline.style = 5;
    QCOMPARE(Frame::outputSize({3840, 2160}, outline), QSize(3952, 2272));
  }
  void aspectNeverCropsSource() {
    const QSize landscape(1920, 1080);
    for (int aspect = 1; aspect <= 4; ++aspect) {
      auto s = Frame::outputSize(landscape, {0, 0.09, aspect});
      QVERIFY(s.width() > 1920);
      QVERIFY(s.height() > 1080);
    }
    QCOMPARE(Frame::outputSize(landscape, {0, 0.09, 1}).width(),
             Frame::outputSize(landscape, {0, 0.09, 1}).height());
  }
  void redactionReplacesAlphaAndColor() {
    QImage source(100, 100, QImage::Format_ARGB32_Premultiplied);
    source.fill(QColor(240, 0, 255, 80));
    auto result =
        Frame::applyEdits(source, {{"redact", {0.1, 0.2}, {0.6, 0.7}}});
    QCOMPARE(result.pixelColor(20, 30), QColor("#151a20"));
    QCOMPARE(result.pixelColor(59, 69).alpha(), 255);
    QCOMPARE(result.pixel(0, 0), source.pixel(0, 0));
    QVERIFY(source.pixelColor(20, 30) != result.pixelColor(20, 30));
  }
  void cropAndAnnotationsUseSourceCoordinates() {
    QImage source(200, 100, QImage::Format_ARGB32_Premultiplied);
    source.fill(Qt::white);
    auto result = Frame::applyEdits(
        source, {{"redact", {0.5, 0}, {0.75, 1}},
                 {"crop", {0.25, 0}, {0.75, 1}}});
    QCOMPARE(result.size(), QSize(100, 100));
    QCOMPARE(result.pixelColor(75, 50), QColor("#151a20"));
    QCOMPARE(result.pixelColor(25, 50), QColor(Qt::white));
    auto reframed = Frame::applyEdits(
        source, {{"redact", {0.5, 0}, {0.75, 1}},
                 {"crop", {0.5, 0}, {1, 1}}});
    QCOMPARE(reframed.size(), QSize(100, 100));
    QCOMPARE(reframed.pixelColor(25, 50), QColor("#151a20"));
    QCOMPARE(reframed.pixelColor(75, 50), QColor(Qt::white));
    QCOMPARE(Frame::applyEdits(source, {{"redact", {0.5, 0}, {0.75, 1}},
                                       {"crop", {0.25, 0}, {0.75, 1}}}, false).size(),
             source.size());
  }
  void shapeToolsDrawOnlyTheirOutlines() {
    QImage source(200, 100, QImage::Format_ARGB32_Premultiplied);
    source.fill(Qt::white);
    auto box = Frame::applyEdits(source, {{"box", {0.1, 0.1}, {0.3, 0.5}}});
    QVERIFY(box.pixelColor(20, 10) != QColor(Qt::white));
    QCOMPARE(box.pixelColor(40, 30), QColor(Qt::white));
    auto oval = Frame::applyEdits(source, {{"ellipse", {0.4, 0.1}, {0.8, 0.9}}});
    QVERIFY(oval.pixelColor(120, 10) != QColor(Qt::white));
    QCOMPARE(oval.pixelColor(120, 50), QColor(Qt::white));
    auto line = Frame::applyEdits(source, {{"line", {0.1, 0.8}, {0.9, 0.8}}});
    QVERIFY(line.pixelColor(100, 80) != QColor(Qt::white));
  }
  void penAndBlurRenderWithoutChangingOutsidePixels() {
    QImage source(200, 100, QImage::Format_ARGB32_Premultiplied);
    source.fill(Qt::white);
    QPainter painter(&source);
    painter.fillRect(0, 0, 100, 100, Qt::black);
    painter.end();
    Frame::Edit blur{"blur", {0.25, 0.1}, {0.75, 0.9}};
    const QImage softened = Frame::applyEdits(source, {blur});
    QCOMPARE(softened.pixelColor(20, 50), QColor(Qt::black));
    QCOMPARE(softened.pixelColor(180, 50), QColor(Qt::white));
    const QColor seam = softened.pixelColor(100, 50);
    QVERIFY(seam.red() > 0 && seam.red() < 255);
    Frame::Edit pen{"pen", {0.1, 0.1}, {0.9, 0.9}};
    pen.points = {{0.1, 0.1}, {0.5, 0.5}, {0.9, 0.9}};
    const QImage marked = Frame::applyEdits(source, {pen});
    QVERIFY(marked.pixelColor(100, 50) != source.pixelColor(100, 50));
    QCOMPARE(Frame::annotationBounds(pen, source), QRectF(0.1, 0.1, 0.8, 0.8));
  }
  void reversedSelectionIsNormalized() {
    QImage source(200, 100, QImage::Format_ARGB32_Premultiplied);
    source.fill(Qt::white);
    QCOMPARE(
        Frame::applyEdits(source, {{"crop", {0.8, 0.9}, {0.2, 0.1}}}).size(),
        QSize(120, 80));
  }
  void largeTextWrapsWithinImageWidth() {
    QImage source(1280, 800, QImage::Format_ARGB32_Premultiplied);
    source.fill(Qt::white);
    Frame::Edit label;
    label.type = "text";
    label.from = {0.1, 0.1};
    label.to = label.from;
    label.text = "A longer label that should wrap instead of running beyond the image";
    label.size = Frame::textSizeForPixels(192, source);
    const QRectF bounds = Frame::annotationBounds(label, source);
    QVERIFY(bounds.width() <= 0.86);
    QVERIFY(bounds.height() > 0.25);
    const QImage result = Frame::applyEdits(source, {label});
    QCOMPARE(result.size(), source.size());
    QVERIFY(result != source);
  }
  void textCanExceedOldSizeLimitOnLargeCapture() {
    QImage source(3840, 2160, QImage::Format_ARGB32_Premultiplied);
    Frame::Edit label;
    label.type = "text";
    label.from = {0.1, 0.1};
    label.text = "A";
    label.size = Frame::textSizeForPixels(1024, source);
    QCOMPARE(Frame::textPixelSize(label, source), 1024);
    const QRectF bounds = Frame::annotationBounds(label, source);
    QVERIFY(bounds.right() < 1.);
    QVERIFY(bounds.bottom() < 1.);
  }
  void previewBounded() {
    QImage source(1800, 1200, QImage::Format_ARGB32_Premultiplied);
    source.fill(Qt::white);
    auto preview = Frame::compose(source, {3, 0.1, 1}, 480);
    QCOMPARE(preview.size(), QSize(480, 480));
  }
};
QTEST_MAIN(RendererTest)
#include "renderer-test.moc"
