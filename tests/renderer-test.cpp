#include "renderer.hpp"
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
      const QImage result = Frame::compose(source, {style, 0.09, 0});
      QCOMPARE(result.size(), QSize(944, 644));
      QCOMPARE(result.pixelColor(result.width() / 2, result.height() / 2),
               QColor("#e21c87"));
      QVERIFY(result.pixelColor(0, 0) != QColor("#e21c87"));
    }
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
  void cropThenEditUsesCroppedCoordinates() {
    QImage source(200, 100, QImage::Format_ARGB32_Premultiplied);
    source.fill(Qt::white);
    auto result = Frame::applyEdits(
        source, {{"crop", {0.25, 0}, {0.75, 1}}, {"redact", {0.5, 0}, {1, 1}}});
    QCOMPARE(result.size(), QSize(100, 100));
    QCOMPARE(result.pixelColor(75, 50), QColor("#151a20"));
    QCOMPARE(result.pixelColor(25, 50), QColor(Qt::white));
  }
  void reversedSelectionIsNormalized() {
    QImage source(200, 100, QImage::Format_ARGB32_Premultiplied);
    source.fill(Qt::white);
    QCOMPARE(
        Frame::applyEdits(source, {{"crop", {0.8, 0.9}, {0.2, 0.1}}}).size(),
        QSize(120, 80));
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
