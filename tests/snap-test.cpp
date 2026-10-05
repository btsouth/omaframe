#include "mark-constraints.hpp"
#include "marks.hpp"
#include "edit-json.hpp"
#include "omarchy-theme.hpp"
#include <QGuiApplication>
#include <QJsonDocument>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>
#include <numbers>
#include <limits>

namespace {
QPointF pixels(QPointF point, QSizeF size) {
  return {point.x() * size.width(), point.y() * size.height()};
}
bool near(double a, double b) { return std::abs(a - b) < 1e-7; }
struct Scene {
  QTemporaryDir temp;
  MarkDocument marks;
  OmarchyTheme theme{nullptr, temp.filePath("theme"), temp.filePath("config"), false};
  QQmlEngine engine;
  QQmlComponent component{&engine};
  QQuickWindow window;
  std::unique_ptr<QQuickItem> canvas;
  Scene() {
    QImage image(1920, 1080, QImage::Format_RGB32);
    image.fill(Qt::white);
    marks.reset(image);
    engine.rootContext()->setContextProperty("theme", &theme);
    component.loadUrl(QUrl::fromLocalFile(QFINDTESTDATA("../qml/MarkCanvas.qml")));
  }
  bool open(QString tool = "arrow") {
    canvas.reset(qobject_cast<QQuickItem *>(component.createWithInitialProperties({
        {"doc", QVariant::fromValue(&marks)}, {"width", 640}, {"height", 360},
        {"workingSize", QSize(1920, 1080)}, {"sourceSize", QSize(1920, 1080)},
        {"tool", tool}})));
    if (!canvas) return false;
    window.resize(640, 360);
    canvas->setParentItem(window.contentItem());
    window.show(); window.requestActivate();
    return QTest::qWaitForWindowExposed(&window);
  }
  void press(QPoint p) { QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, p); }
  void move(QPoint p) { QTest::mouseMove(&window, p); QTest::qWait(60); }
  void release(QPoint p, Qt::KeyboardModifiers mods = Qt::NoModifier) {
    QTest::mouseRelease(&window, Qt::LeftButton, mods, p);
  }
};
}
class SnapTest : public QObject {
  Q_OBJECT
private slots:
  void sectorsAndClockwiseTies() {
    const QSizeF size(1920, 1080);
    for (int sector = 0; sector < 8; ++sector) {
      for (double offset : {-22.49, 0., 22.49, 22.5}) {
        const double radians = (sector * 45. + offset) * std::numbers::pi / 180.;
        const QPointF pointer = QPointF(.5, .5) + QPointF(100 * std::cos(radians) / size.width(),
                                                         100 * std::sin(radians) / size.height());
        const auto result = MarkConstraints::resolve("arrow", {.5, .5}, pointer, size, true);
        QVERIFY(result.valid);
        const int expected = (sector + (offset == 22.5 ? 1 : 0)) % 8;
        QCOMPARE(result.label, QString::number(expected * 45) + QChar(0x00b0));
        const auto v = pixels(result.point - result.anchor, size);
        QVERIFY(near(std::hypot(v.x(), v.y()), 100));
      }
    }
  }
  void boundsAndDegenerate() {
    auto result = MarkConstraints::resolve("line", {.8, .7}, {2, 3}, {1920, 1080}, true);
    QVERIFY(result.valid);
    QVERIFY(near(result.point.x(), 1));
    QVERIFY(near((result.point.x()-.8)*1920, (result.point.y()-.7)*1080));
    result = MarkConstraints::resolve("box", {.9, .7}, {4, 2}, {1920, 1080}, true);
    QVERIFY(result.valid);
    QVERIFY(near((result.point.x()-.9)*1920, (result.point.y()-.7)*1080));
    // Anchor outside the crop can reach it, or have no feasible ray.
    result = MarkConstraints::resolve("line", {-.2, .5}, {.4, .5}, {100, 100}, true);
    QVERIFY(result.valid); QVERIFY(near(result.point.x(), .4));
    QVERIFY(!MarkConstraints::resolve("line", {-.2, .5}, {-.4, .5}, {100, 100}, true).valid);
    result = MarkConstraints::resolve("arrow", {.3, .4}, {.3, .4}, {100, 100}, true);
    QVERIFY(result.valid); QCOMPARE(result.point, result.anchor); QVERIFY(result.label.isEmpty());
    QVERIFY(!MarkConstraints::resolve("box", {}, {}, {0, 100}, true).valid);
    QVERIFY(!MarkConstraints::resolve("box", {}, {}, {100, 100}, true, 0).valid);
    QVERIFY(!MarkConstraints::resolve("line", {}, {std::numeric_limits<double>::infinity(), 0}, {100, 100}, true).valid);
    for (int x : {-1, 1}) for (int y : {-1, 1}) {
      result = MarkConstraints::resolve("ellipse", {.5, .5}, {.5 + x*.2, .5+y*.1}, {200, 100}, true);
      QVERIFY(result.valid);
      const QPointF v = pixels(result.point-result.anchor, {200, 100});
      QVERIFY(near(std::abs(v.x()), std::abs(v.y())));
      QVERIFY(v.x()*x > 0 && v.y()*y > 0);
    }
  }
  void cropsZoomAndDraftRoundTrip() {
    for (QSize source : {QSize(1920,1080), QSize(360,2400)}) {
      for (bool video : {false, true}) for (double zoom : {1., 2., 8.}) {
        QImage image(source, QImage::Format_RGB32);
        MarkDocument marks; marks.reset(image);
        if (video) marks.setDuration(10);
        marks.edit("crop", .137, .219, .837, .891);
        const QRectF crop = marks.cropBounds();
        const QSizeF canvas(source.width()*crop.width()*zoom, source.height()*crop.height()*zoom);
        for (QString type : {QString("arrow"), QString("box"), QString("ellipse"), QString("highlight"), QString("redact"), QString("blur")}) {
          const auto preview = marks.creationPreview(type, .1, .1, .4, .4, canvas.width(), canvas.height(), true);
          QVERIFY(preview.value("valid").toBool());
          marks.edit(type, .1, .1, preview.value("x2").toDouble(), preview.value("y2").toDouble());
          // Both draft writers use these JSON helpers; parse the serialized bytes too.
          const auto json = QJsonDocument::fromJson(QJsonDocument(Frame::editToJson(marks.edits().last())).toJson()).object();
          const auto edit = Frame::editFromJson(json); QVERIFY(edit);
          const auto v = pixels(edit->to - edit->from, source);
          if (type == "arrow") {
            const double angle = std::atan2(v.y(),v.x()) * 180 / std::numbers::pi;
            QVERIFY(near(angle / 45, std::round(angle / 45)));
          } else QVERIFY(near(std::abs(v.x()), std::abs(v.y())));
          if (video) {
            QCOMPARE(edit->start, 0.); QCOMPARE(edit->end, 10.);
            const QRect pixels = MarkConstraints::videoCropPixels(source, Frame::cropBounds(marks.edits()));
            QCOMPARE(crop, QRectF(double(pixels.x())/source.width(), double(pixels.y())/source.height(),
                                   double(pixels.width())/source.width(), double(pixels.height())/source.height()));
          }
        }
      }
    }
  }
  void originalGeometryTransforms() {
    MarkDocument marks; marks.reset(QImage(1920,1080,QImage::Format_RGB32));
    for (int handle : {0,1}) {
      marks.resetEdits(); marks.edit("arrow", .2,.2,.6,.6); marks.beginTransform();
      const QPointF anchor = handle == 0 ? marks.edits()[0].to : marks.edits()[0].from;
      auto preview = marks.previewConstrainedTransform(handle, .7,.5,1920,1080,true);
      QVERIFY(preview.value("valid").toBool());
      const auto v = pixels((handle == 0 ? marks.edits()[0].from : marks.edits()[0].to)-anchor,{1920,1080});
      const double angle = std::atan2(v.y(),v.x())*180/std::numbers::pi;
      QVERIFY(near(angle/45,std::round(angle/45)));
      marks.previewConstrainedTransform(handle,.7,.5,1920,1080,false);
      QCOMPARE(handle == 0 ? marks.edits()[0].from : marks.edits()[0].to, QPointF(.7,.5));
      marks.endTransform(false);
    }
    for (QString type : {QString("box"),QString("ellipse"),QString("highlight"),QString("blur"),QString("redact")}) {
      for (int handle=0; handle<8; ++handle) {
        marks.resetEdits(); marks.edit(type,.2,.2,.5,.4);
        const auto original = marks.edits()[0];
        marks.beginTransform();
        marks.previewConstrainedTransform(handle,.8,.7,1920,1080,true);
        auto constrained = marks.edits()[0];
        if (handle<4) {
          const auto v = constrained.to-constrained.from;
          QVERIFY(near(std::abs(v.x()/v.y()), 1.5));
          // Recompute from the original ratio, including crossing and bounds.
          marks.previewConstrainedTransform(handle,-.5,-.7,1920,1080,true);
          const auto cross = marks.edits()[0].to-marks.edits()[0].from;
          QVERIFY(near(std::abs(cross.x()/cross.y()),1.5));
        } else {
          marks.previewConstrainedTransform(handle,.8,.7,1920,1080,false);
          QCOMPARE(marks.edits()[0], constrained);
        }
        marks.endTransform(false); QCOMPARE(marks.edits()[0],original);
      }
    }
    // Pen, Text and Steps retain their existing resize math.
    for (QString type : {QString("pen"),QString("text"),QString("step")}) {
      marks.resetEdits();
      if (type == "pen") marks.addStroke({QVariantMap{{"x",.2},{"y",.2}},QVariantMap{{"x",.5},{"y",.4}}});
      else marks.edit(type,.2,.2,.2,.2,"Label");
      marks.beginTransform(); marks.previewConstrainedTransform(2,.7,.7,1920,1080,true);
      const auto shifted = marks.edits()[0]; marks.previewConstrainedTransform(2,.7,.7,1920,1080,false);
      QCOMPARE(marks.edits()[0],shifted); marks.endTransform(false);
    }
  }
  void stationaryModifierToggleAndUndo() {
    Scene scene; QTRY_VERIFY2(scene.component.isReady(),qPrintable(scene.component.errorString()));
    QVERIFY2(scene.open(),qPrintable(scene.component.errorString()));
    QSignalSpy edited(&scene.marks,&MarkDocument::edited);
    scene.press({100,100}); scene.move({240,180});
    QTest::keyPress(&scene.window,Qt::Key_Shift);
    QTRY_COMPARE(scene.canvas->property("constraintLabel").toString(),QString("45°"));
    auto *readout = scene.canvas->findChild<QQuickItem *>("constraintReadout"); QVERIFY(readout);
    QVERIFY(readout->isVisible()); QVERIFY(readout->x()+readout->width() <= 640);
    QTest::keyRelease(&scene.window,Qt::Key_Shift);
    QTRY_VERIFY(!scene.canvas->property("constraintActive").toBool());
    QTest::keyPress(&scene.window,Qt::Key_Shift);
    scene.release({240,180},Qt::ShiftModifier);
    QTest::keyRelease(&scene.window,Qt::Key_Shift);
    QCOMPARE(scene.marks.edits().size(),1);
    const auto v=pixels(scene.marks.edits()[0].to-scene.marks.edits()[0].from,{640,360});
    QVERIFY(near(v.x(),v.y()));
    QCOMPARE(edited.count(),1); QVERIFY(!readout->isVisible());
    scene.marks.undo(); QVERIFY(scene.marks.edits().isEmpty()); QVERIFY(!scene.marks.canUndo());
    scene.marks.redo(); QCOMPARE(scene.marks.edits().size(),1);
    // Endpoint preview toggles immediately and cancels without adding history.
    const auto original=scene.marks.edits()[0];
    const QPoint tip=pixels(original.to,{640,360}).toPoint();
    scene.press(tip); scene.move({400,200});
    QTest::keyPress(&scene.window,Qt::Key_Shift);
    QTRY_VERIFY(scene.canvas->property("constraintActive").toBool());
    QTest::keyRelease(&scene.window,Qt::Key_Shift);
    QTRY_VERIFY(near(scene.marks.edits()[0].to.x(),400./640));
    QTest::keyPress(&scene.window,Qt::Key_Shift);
    QVERIFY(QMetaObject::invokeMethod(scene.canvas.get(),"cancelDrag"));
    scene.release({400,200},Qt::ShiftModifier); QTest::keyRelease(&scene.window,Qt::Key_Shift);
    QCOMPARE(scene.marks.edits()[0],original); QVERIFY(!scene.marks.transforming());
    QVERIFY(!scene.canvas->property("constraintActive").toBool());
    scene.marks.undo(); QVERIFY(scene.marks.edits().isEmpty());
  }
  void cornerSideAndFocusLoss() {
    Scene scene; QTRY_VERIFY(scene.component.isReady()); QVERIFY(scene.open("box"));
    scene.marks.edit("box",.2,.2,.5,.4);
    scene.press({320,144}); scene.move({460,220});
    QTest::keyPress(&scene.window,Qt::Key_Shift);
    QTRY_VERIFY(scene.canvas->property("constraintActive").toBool());
    auto v=scene.marks.edits()[0].to-scene.marks.edits()[0].from;
    QVERIFY(near(v.x()/v.y(),1.5));
    QTest::keyRelease(&scene.window,Qt::Key_Shift);
    QTRY_VERIFY(near(scene.marks.edits()[0].to.x(),460./640));
    QTest::keyPress(&scene.window,Qt::Key_Shift);
    scene.release({460,220},Qt::ShiftModifier); QTest::keyRelease(&scene.window,Qt::Key_Shift);
    scene.marks.undo(); QCOMPARE(scene.marks.edits()[0].to,QPointF(.5,.4));
    scene.press({320,108}); scene.move({400,108});
    QTest::keyPress(&scene.window,Qt::Key_Shift);
    QVERIFY(!scene.canvas->property("constraintActive").toBool());
    scene.release({400,108},Qt::ShiftModifier); QTest::keyRelease(&scene.window,Qt::Key_Shift);
    QCOMPARE(scene.marks.edits()[0].to,QPointF(.625,.4));
    scene.marks.undo();
    scene.press({320,144}); scene.move({400,240});
    QTest::keyPress(&scene.window,Qt::Key_Shift);
    scene.window.contentItem()->forceActiveFocus();
    QTRY_VERIFY(!scene.marks.transforming());
    QVERIFY(!scene.canvas->property("constraintActive").toBool());
    scene.release({400,240}); QTest::keyRelease(&scene.window,Qt::Key_Shift);
    QCOMPARE(scene.marks.edits()[0].to,QPointF(.5,.4));
  }
  void croppedTallZoomedCanvas() {
    Scene scene; QTRY_VERIFY(scene.component.isReady()); QVERIFY(scene.open("ellipse"));
    QImage image(360,2400,QImage::Format_RGB32); image.fill(Qt::white);
    scene.marks.reset(image); QVERIFY(scene.marks.cropCurrentView(.13,.21,.83,.89));
    const auto crop=scene.marks.cropBounds();
    const QSizeF canvas(360*crop.width()*2,2400*crop.height()*2);
    scene.canvas->setSize(canvas);
    scene.canvas->setPosition({-100,-200});
    scene.canvas->setProperty("feedbackViewport",QRectF(100,200,540,360));
    scene.press({30,30}); scene.move({200,100});
    QTest::keyPress(&scene.window,Qt::Key_Shift);
    QTRY_COMPARE(scene.canvas->property("constraintLabel").toString(),QString("1:1"));
    auto *readout=scene.canvas->findChild<QQuickItem *>("constraintReadout"); QVERIFY(readout);
    QVERIFY(readout->x()>=100 && readout->y()>=200);
    QVERIFY(readout->x()+readout->width()<=canvas.width());
    QVERIFY(readout->y()+readout->height()<=560);
    scene.release({200,100},Qt::ShiftModifier); QTest::keyRelease(&scene.window,Qt::Key_Shift);
    QCOMPARE(scene.marks.edits().size(),2);
    const auto v=pixels(scene.marks.edits()[1].to-scene.marks.edits()[1].from,image.size());
    QVERIFY(near(std::abs(v.x()),std::abs(v.y())));
  }
  void shiftClickDragRouting() {
    Scene scene; QTRY_VERIFY(scene.component.isReady()); QVERIFY(scene.open("box"));
    scene.marks.edit("box",.1,.1,.8,.8); scene.marks.clearSelection();
    scene.press({240,160});
    QTest::keyPress(&scene.window,Qt::Key_Shift);
    QVERIFY(!scene.canvas->property("constraintActive").toBool());
    scene.release({240,160},Qt::ShiftModifier); QTest::keyRelease(&scene.window,Qt::Key_Shift);
    QCOMPARE(scene.marks.selected(),0); QCOMPARE(scene.marks.edits().size(),1);
    scene.marks.clearSelection(); scene.press({240,160}); scene.move({360,200});
    QTest::keyPress(&scene.window,Qt::Key_Shift);
    QTRY_COMPARE(scene.canvas->property("constraintLabel").toString(),QString("1:1"));
    scene.release({360,200},Qt::ShiftModifier); QTest::keyRelease(&scene.window,Qt::Key_Shift);
    QCOMPARE(scene.marks.edits().size(),2);
    const auto v=pixels(scene.marks.edits()[1].to-scene.marks.edits()[1].from,{640,360});
    QVERIFY(near(v.x(),v.y()));
    scene.canvas->setProperty("tool","step"); scene.marks.clearSelection();
    QTest::mouseClick(&scene.window,Qt::LeftButton,Qt::ShiftModifier,{150,200});
    QCOMPARE(scene.marks.edits().size(),3); QCOMPARE(scene.marks.edits()[2].type,QString("step"));
    // A modifier-only press at empty space creates no edit.
    scene.canvas->setProperty("tool","arrow"); scene.marks.clearSelection();
    scene.press({600,330}); QTest::keyPress(&scene.window,Qt::Key_Shift);
    scene.release({600,330},Qt::ShiftModifier); QTest::keyRelease(&scene.window,Qt::Key_Shift);
    QCOMPARE(scene.marks.edits().size(),3);
  }
};
QTEST_MAIN(SnapTest)
#include "snap-test.moc"
