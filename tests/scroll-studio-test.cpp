/** Studio-side integration for scrolling capture: where the progress control
 *  goes, what the result does, and the image-size budget the stitcher and the
 *  editor share. The capture loop itself is covered by scroll-capture-test. */
#include "stitch.hpp"
#include "studio.hpp"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
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

  void damagedDraftSourceImageIsRejected() {
    // A draft whose PNG header cannot be read must not fall through to a
    // decode: its size is unknown, so the budget cannot be checked.
    const QString directory = QStandardPaths::writableLocation(
                                  QStandardPaths::AppLocalDataLocation) +
                              "/drafts";
    QVERIFY(QDir().mkpath(directory));
    const QString id = QStringLiteral("deadbeefdeadbeefdeadbeefdeadbeef");
    QFile image(directory + "/" + id + ".png");
    QVERIFY(image.open(QIODevice::WriteOnly));
    QVERIFY(image.write("\x89PNG\r\n\x1a\n not really a png") > 0);
    image.close();
    QFile metadata(directory + "/" + id + ".json");
    QVERIFY(metadata.open(QIODevice::WriteOnly));
    QVERIFY(metadata.write(
                QJsonDocument(QJsonObject{{"version", 1}}).toJson()) > 0);
    metadata.close();

    ImageStore store;
    Studio studio(&store, false);
    studio.resumeDraft(id);
    QVERIFY(!studio.hasImage());
    QVERIFY2(studio.status().contains("invalid source image", Qt::CaseInsensitive),
             qPrintable(studio.status()));
    QFile::remove(directory + "/" + id + ".png");
    QFile::remove(directory + "/" + id + ".json");
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

  void tallPreviewProvidersRespectTheGpuTextureLimit() {
    // A 64x32000 page fits the 6 MP preview budget yet is far past the
    // longest texture a GPU is guaranteed to accept, so every image the GUI
    // loads must be capped while the editable original keeps every pixel.
    ImageStore store;
    Studio studio(&store, false);
    studio.scrollFinished(pageImage(64, 32000), false, true);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 20000);
    QCOMPARE(studio.sourceSize(), QSize(64, 32000));
    QCOMPARE(studio.workingSize(), QSize(64, 32000));
    for (const QString &provider :
         {QStringLiteral("source"), QStringLiteral("uncropped"),
          QStringLiteral("preview"), QStringLiteral("style0")}) {
      QSize size;
      const QImage image = store.requestImage(provider, &size, {});
      QVERIFY2(!image.isNull(), qPrintable(provider));
      QVERIFY2(std::max(size.width(), size.height()) <= 8192,
               qPrintable(QString("%1: %2x%3")
                              .arg(provider)
                              .arg(size.width())
                              .arg(size.height())));
    }
    // Capping the long edge must keep the page's shape, not squash it.
    QSize size;
    store.requestImage("source", &size, {});
    QVERIFY(std::abs(double(size.width()) / size.height() - 64.0 / 32000.0) <
            0.01);
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
    QSignalSpy dismiss(&studio, &Studio::dismissRequested);
    studio.scrollFinished({}, false, false);
    QVERIFY(!studio.hasImage());
    QCOMPARE(chooser.count(), 0);
    QCOMPARE(ended.count(), 1);
    // No image means the session is over: it must end terminally instead of
    // leaving the scrolling state up with its controls already hidden.
    QCOMPARE(dismiss.count(), 1);
    QCOMPARE(studio.quickState(), QString("cancelled"));
    QVERIFY(!studio.busy());
    QVERIFY(!studio.status().isEmpty());
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
