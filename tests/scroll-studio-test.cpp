/** Studio-side integration for scrolling capture: where the progress control
 *  goes, what the result does, and the image-size budget the stitcher and the
 *  editor share. The capture loop itself is covered by scroll-capture-test. */
#include "stitch.hpp"
#include "studio.hpp"
#include <QGuiApplication>
#include <QImage>
#include <QSignalSpy>
#include <QTest>
#include <cmath>

namespace {
/// A page-like image, so the tests read like the real result.
QImage pageImage(int width, int height) {
  QImage image(width, height, QImage::Format_RGB32);
  image.fill(QColor("#fbfbf8"));
  return image;
}
} // namespace

class ScrollStudioTest : public QObject {
  Q_OBJECT
private slots:
  void controlSitsOutsideTheCapture() {
    const QList<ScrollUi::Display> displays{
        {"eDP-1", QRect(0, 0, 1920, 1080)}};
    const QRect capture(400, 200, 600, 400);
    const auto control = ScrollUi::placeControl(displays, "eDP-1", capture);
    QCOMPARE(control.monitor, QString("eDP-1"));
    QVERIFY(!control.inside);
    QCOMPARE(control.coverTop, 0.0);
    QCOMPARE(control.bounds.size(), ScrollUi::controlSize());
    QVERIFY2(!control.bounds.intersects(capture),
             qPrintable(QString("control %1,%2 %3x%4")
                            .arg(control.bounds.x())
                            .arg(control.bounds.y())
                            .arg(control.bounds.width())
                            .arg(control.bounds.height())));
  }

  void controlCoversTheTopWhenThereIsNowhereElse() {
    const QList<ScrollUi::Display> displays{
        {"eDP-1", QRect(0, 0, 1920, 1080)}};
    const QRect capture(0, 0, 1920, 1080);
    const auto control = ScrollUi::placeControl(displays, "eDP-1", capture);
    QCOMPARE(control.monitor, QString("eDP-1"));
    QVERIFY(control.inside);
    QVERIFY(capture.contains(control.bounds));
    // The first frame's rows up to the bottom of the control are kept.
    QVERIFY(control.coverTop > 0.0);
    QVERIFY(control.coverTop <= 1.0);
    QVERIFY(control.coverTop * 1080.0 >= control.bounds.bottom() + 1);
  }

  void imageBudgetMatchesTheScrollingCapture() {
    // Within the stitcher's 200 MiB / 32000 px budget.
    QVERIFY(ScrollUi::withinImageBudget({2560, 20000}));
    QVERIFY(ScrollUi::withinImageBudget({32000, 1600}));
    QVERIFY(ScrollUi::withinImageBudget({1200, 800}));
    // Past it, on pixels or on an edge.
    QVERIFY(!ScrollUi::withinImageBudget({2560, 24000}));
    QVERIFY(!ScrollUi::withinImageBudget({40000, 1000}));
    QVERIFY(!ScrollUi::withinImageBudget({1000, 40000}));
    QVERIFY(!ScrollUi::withinImageBudget({}));
    QVERIFY(!ScrollUi::imageBudgetError({2560, 24000}).isEmpty());
    QVERIFY(ScrollUi::imageBudgetError({1200, 800}).isEmpty());
    // The budget is exactly the one the stitcher enforces.
    QVERIFY(ScrollUi::withinImageBudget(
        {stitch::kMaxStitchedPixels / stitch::kMaxStitchedEdge,
         stitch::kMaxStitchedEdge}));
  }

  void tallPreviewKeepsItsWidth() {
    // A 20000 px page must not become an unreadable sliver: scaling by a pixel
    // budget keeps a usable width instead of capping the long edge alone.
    const QSize out =
        ScrollUi::fitBudget({2560, 24000}, 6000000, stitch::kMaxStitchedEdge);
    QVERIFY2(out.width() >= 700, qPrintable(QString::number(out.width())));
    QVERIFY(out.height() <= stitch::kMaxStitchedEdge);
    QVERIFY(qint64(out.width()) * out.height() <= 6000000);
    QVERIFY(std::abs(double(out.width()) / out.height() - 2560.0 / 24000.0) <
            0.01);
    // An image already inside the budget keeps every pixel.
    QCOMPARE(ScrollUi::fitBudget({1000, 800}, 6000000, 32000),
             QSize(1000, 800));
  }

  void scrollFinishedLoadsTheImageAndOpensTheChooser() {
    ImageStore store;
    Studio studio(&store, false);
    QSignalSpy chooser(&studio, &Studio::chooserRequested);
    QSignalSpy ended(&studio, &Studio::scrollEnded);
    studio.scrollFinished(pageImage(1200, 5000), false, true);
    QVERIFY(studio.hasImage());
    QCOMPARE(studio.dimensions(), QString("1200 × 5000"));
    QVERIFY(studio.tallImage());
    QCOMPARE(studio.name(), QString("Scrolling capture"));
    QCOMPARE(chooser.count(), 1);
    QCOMPARE(ended.count(), 1);
  }

  void scrollFinishedStopsAtTheLimitWithANote() {
    ImageStore store;
    Studio studio(&store, false);
    studio.scrollFinished(pageImage(800, 4000), true, false);
    QVERIFY(studio.hasImage());
    QVERIFY2(studio.status().contains("limit", Qt::CaseInsensitive),
             qPrintable(studio.status()));
    QVERIFY(!studio.scrollReachedEnd());
    QVERIFY(studio.scrollReachedLimit());
  }

  void aNullResultEndsTheControlWithoutAChooser() {
    ImageStore store;
    Studio studio(&store, false);
    QSignalSpy chooser(&studio, &Studio::chooserRequested);
    QSignalSpy ended(&studio, &Studio::scrollEnded);
    studio.scrollFinished({}, false, false);
    QVERIFY(!studio.hasImage());
    QCOMPARE(chooser.count(), 0);
    QCOMPARE(ended.count(), 1);
  }

  void cancellingDiscardsAndEndsTheControl() {
    ImageStore store;
    Studio studio(&store, false);
    QSignalSpy dismiss(&studio, &Studio::dismissRequested);
    QSignalSpy ended(&studio, &Studio::scrollEnded);
    studio.scrollCancelled();
    QCOMPARE(ended.count(), 1);
    QCOMPARE(dismiss.count(), 1);
    QCOMPARE(studio.quickState(), QString("cancelled"));
    QVERIFY(!studio.hasImage());
  }

  void aFailureIsReportedWhereTheUserIsLooking() {
    ImageStore store;
    Studio studio(&store, false);
    QSignalSpy failed(&studio, &Studio::captureFailed);
    QSignalSpy ended(&studio, &Studio::scrollEnded);
    studio.scrollFailed("The display turned off before capture began.");
    QCOMPARE(ended.count(), 1);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(studio.status(),
             QString("The display turned off before capture began."));
    QCOMPARE(studio.quickState(), QString("capture-error"));
  }

  void tallImageTracksTheWorkingImage() {
    ImageStore store;
    Studio studio(&store, false);
    studio.scrollFinished(pageImage(2000, 1000), false, false);
    QVERIFY(!studio.tallImage());
    studio.scrollFinished(pageImage(800, 3200), false, false);
    QVERIFY(studio.tallImage());
    // A full 60-row browser page is already unreadable when fitted to height.
    studio.scrollFinished(pageImage(1200, 2800), false, false);
    QVERIFY(studio.tallImage());
  }

  void scrollSelectionIsOffUntilTheSelectorAsksForIt() {
    ImageStore store;
    Studio studio(&store, false);
    QVERIFY(!studio.scrollSelection());
    QVERIFY(studio.scrollCapture() != nullptr);
    QVERIFY(!studio.scrollCapture()->active());
    studio.useScreenshotSelection();
    QVERIFY(!studio.scrollSelection());
  }
};

QTEST_MAIN(ScrollStudioTest)
#include "scroll-studio-test.moc"
