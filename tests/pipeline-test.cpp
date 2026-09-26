#include "capture-session.hpp"
#include "capture.hpp"
#include "studio.hpp"
#include "video.hpp"
#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QScreen>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class PipelineTest : public QObject {
  Q_OBJECT
  QTemporaryDir temp;
  QString recording;
  static QByteArray contents(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
      return {};
    return f.readAll();
  }
  static QJsonObject probe(const QString &path) {
    QProcess p;
    p.start("ffprobe", {"-v", "error", "-show_format", "-show_streams", "-of",
                        "json", path});
    if (!p.waitForFinished(10000))
      return {};
    return QJsonDocument::fromJson(p.readAllStandardOutput()).object();
  }
private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName("Omaframe-test");
    QCoreApplication::setApplicationName("Pipelines");
    QSettings().clear();
    QVERIFY(temp.isValid());
    recording = temp.filePath("input.mp4");
    QProcess ffmpeg;
    ffmpeg.start("ffmpeg", {"-hide_banner",
                            "-loglevel",
                            "error",
                            "-f",
                            "lavfi",
                            "-i",
                            "testsrc2=size=640x360:rate=25",
                            "-f",
                            "lavfi",
                            "-i",
                            "sine=frequency=440:sample_rate=48000",
                            "-f",
                            "lavfi",
                            "-i",
                            "sine=frequency=880:sample_rate=48000",
                            "-t",
                            "6",
                            "-map",
                            "0:v",
                            "-map",
                            "1:a",
                            "-map",
                            "2:a",
                            "-c:v",
                            "libx264",
                            "-preset",
                            "ultrafast",
                            "-threads",
                            "2",
                            "-c:a",
                            "aac",
                            "-metadata",
                            "comment=private-source-metadata",
                            recording});
    QVERIFY(ffmpeg.waitForFinished(20000));
    QCOMPARE(ffmpeg.exitCode(), 0);
  }
  void savedPngMatchesClipboardAndHasNoSourceMetadata() {
    QImage source(400, 200, QImage::Format_ARGB32_Premultiplied);
    source.fill(Qt::magenta);
    source.setText("Secret", "private source text");
    const QString path = temp.filePath("input.png");
    QVERIFY(source.save(path));
    const QByteArray before = contents(path);
    ImageStore store;
    Studio studio(&store);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    studio.setOutputDirectory(QUrl::fromLocalFile(temp.filePath("images")));
    studio.open(QUrl::fromLocalFile(path));
    QTRY_VERIFY_WITH_TIMEOUT(!studio.busy() && !studio.rendering(), 5000);
    studio.edit("crop", 0.25, 0, 0.75, 1);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.dimensions(), QString("200 × 200"));
    studio.undo();
    QTRY_VERIFY(!studio.rendering());
    QCOMPARE(studio.dimensions(), QString("400 × 200"));
    studio.redo();
    QTRY_VERIFY(!studio.rendering());
    QCOMPARE(studio.dimensions(), QString("200 × 200"));
    studio.edit("redact", 0.5, 0, 1, 1);
    QTRY_VERIFY(!studio.rendering());
    studio.setStyle(8);
    QTRY_VERIFY(!studio.rendering());
    studio.accept();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.busy(), 15000);
    QVERIFY2(!studio.savedPath().isEmpty(), qPrintable(studio.status()));
    QImage saved(studio.savedPath());
    QCOMPARE(saved.size(), QSize(200, 200));
    QCOMPARE(saved.pixelColor(150, 100), QColor("#151a20"));
    QVERIFY(saved.textKeys().isEmpty());
    QProcess clipboard;
    clipboard.start("wl-paste", {"--type", "image/png"});
    QVERIFY(clipboard.waitForFinished(3000));
    QCOMPARE(clipboard.exitCode(), 0);
    QCOMPARE(clipboard.readAllStandardOutput(), contents(studio.savedPath()));
    QCOMPARE(contents(path), before);
  }
  void latestStyleWinsRapidChanges() {
    ImageStore store;
    Studio studio(&store);
    QTRY_VERIFY(!studio.rendering());
    studio.setStyle(3);
    studio.setStyle(1);
    studio.setStyle(8);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 10000);
    QSize size;
    QImage image = store.requestImage("preview", &size, {});
    QCOMPARE(size, QSize(1280, 800));
    QVERIFY(!image.isNull());
    for (int i = 0; i < 9; ++i)
      QVERIFY(
          !store.requestImage(QString("style%1").arg(i), nullptr, {}).isNull());
  }
  void trimPreservesTracksMuteRemovesThem() {
    QByteArray originalHash = QCryptographicHash::hash(
        contents(recording), QCryptographicHash::Sha256);
    Video video;
    video.setOutputDirectory(QUrl::fromLocalFile(temp.filePath("clips")));
    video.open(QUrl::fromLocalFile(recording));
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 12000);
    QCOMPARE(video.audioTracks(), 2);
    video.exportClip(1.2, 4.8, false);
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 20000);
    QVERIFY2(!video.savedPath().isEmpty(), qPrintable(video.status()));
    auto doc = probe(video.savedPath());
    double duration =
        doc.value("format").toObject().value("duration").toString().toDouble();
    QVERIFY(qAbs(duration - 3.6) < 0.1);
    auto streams = doc.value("streams").toArray();
    QCOMPARE(streams.size(), 3);
    QCOMPARE(streams[0].toObject().value("width").toInt(), 640);
    QVERIFY(!doc.value("format").toObject().value("tags").toObject().contains(
        "comment"));
    video.exportClip(0.5, 2.5, true);
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 20000);
    QVERIFY2(!video.savedPath().isEmpty(), qPrintable(video.status()));
    QCOMPARE(probe(video.savedPath()).value("streams").toArray().size(), 1);
    QCOMPARE(QCryptographicHash::hash(contents(recording),
                                      QCryptographicHash::Sha256),
             originalHash);
  }
  void timelineThumbnailsAreGeneratedAndRemoved() {
    QString first;
    {
      Video video;
      video.open(QUrl::fromLocalFile(recording));
      QTRY_COMPARE_WITH_TIMEOUT(video.thumbnails().size(),
                                Video::ThumbnailCount, 20000);
      first = QUrl(video.thumbnails().first()).toLocalFile();
      const QImage frame(first);
      QCOMPARE(frame.height(), 120);
      QVERIFY(frame.width() > 200);
    }
    QVERIFY(!QFileInfo::exists(first));
  }
  void cancelledExportLeavesNoPartialFile() {
    const QString directory = temp.filePath("cancelled");
    Video video;
    video.setOutputDirectory(QUrl::fromLocalFile(directory));
    video.open(QUrl::fromLocalFile(recording));
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 12000);
    video.exportClip(0, 5.5, false);
    video.cancel();
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 5000);
    QVERIFY(video.savedPath().isEmpty());
    QVERIFY(QDir(directory).entryList(QDir::Files | QDir::Hidden).isEmpty());
  }
  void invalidExportRangeAndBadInputStayRecoverable() {
    Video video;
    video.open(QUrl::fromLocalFile(recording));
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 12000);
    video.exportClip(4, 2, false);
    QVERIFY(!video.busy());
    QVERIFY(video.savedPath().isEmpty());
    video.open(QUrl::fromLocalFile(temp.filePath("missing.mp4")));
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 12000);
    QVERIFY(video.status().contains("readable video"));
    ImageStore store;
    Studio studio(&store);
    QTRY_VERIFY(!studio.rendering());
    studio.open(QUrl::fromLocalFile(temp.filePath("missing.png")));
    QTRY_VERIFY_WITH_TIMEOUT(!studio.busy(), 5000);
    QVERIFY(studio.status().contains("Could not open image"));
  }
  void monitorSelectionUsesItsOwnNativePixels() {
    QStringList grabbed;
    const auto result = Capture::freeze(
        {"left", "right"}, [&](const QString &name, QImage &image, QString &) {
          grabbed.append(name);
          image = QImage(name == "left" ? QSize(1920, 1080) : QSize(2560, 1440),
                         QImage::Format_ARGB32_Premultiplied);
          image.fill(name == "left" ? Qt::red : Qt::green);
          return true;
        });
    QCOMPARE(grabbed, QStringList({"left", "right"}));
    const auto second =
        Capture::crop(result.images, "right", {0.75, 0.75}, {0.25, 0.25});
    QCOMPARE(second.size(), QSize(1280, 720));
    QCOMPARE(second.pixelColor(0, 0), QColor(Qt::green));
    QVERIFY(Capture::crop(result.images, "missing", {0, 0}, {1, 1}).isNull());
    QVERIFY(Capture::crop(result.images, "right", {0, 0}, {0.0001, 0.0001})
                .isNull());
    const auto failed =
        Capture::freeze({"left", "right"},
                        [](const QString &name, QImage &image, QString &error) {
                          if (name == "right") {
                            error = "Disconnected";
                            return false;
                          }
                          image = QImage(100, 100, QImage::Format_RGB32);
                          image.fill(Qt::red);
                          return true;
                        });
    QVERIFY(failed.images.isEmpty());
    QVERIFY(failed.error.contains("right"));
  }
  void quickCaptureKeepsEditsAndAcceptsExactlyOnce() {
    ImageStore store;
    Studio studio(&store, false);
    QVERIFY(!studio.rendering());
    QVERIFY(store.requestImage("source", nullptr, {}).isNull());
    studio.setOutputDirectory(QUrl::fromLocalFile(temp.filePath("quick")));
    QSignalSpy selections(&studio, &Studio::selectionReady);
    QSignalSpy dismissed(&studio, &Studio::dismissRequested);
    studio.capture(true);
    QTRY_COMPARE_WITH_TIMEOUT(selections.count(), 1, 8000);
    const auto monitors = selections.first().first().toStringList();
    QCOMPARE(monitors.size(), QGuiApplication::screens().size());
    studio.finishSelection(monitors.last(), 0.1, 0.1, 0.6, 0.6);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 8000);
    QCOMPARE(studio.quickState(), QString("choosing"));
    studio.openEditor();
    studio.edit("redact", 0.1, 0.1, 0.4, 0.4);
    QTRY_VERIFY(!studio.rendering());
    QSize size;
    const auto edited = store.requestImage("source", &size, {});
    studio.showFinishes();
    studio.openEditor();
    QCOMPARE(store.requestImage("source", &size, {}), edited);
    QVERIFY(studio.canUndo());
    studio.showFinishes();
    studio.chooseFinish(8);
    studio.chooseFinish(8);
    QTRY_COMPARE_WITH_TIMEOUT(dismissed.count(), 1, 10000);
    QCOMPARE(studio.quickState(), QString("done"));
    QVERIFY(QFileInfo::exists(studio.savedPath()));
    const auto exported = QImage(studio.savedPath());
    QCOMPARE(exported.size(), QGuiApplication::screens().last()->size() / 2);
    QCOMPARE(exported.pixelColor(exported.width() / 5, exported.height() / 5),
             QColor("#151a20"));
    studio.chooseFinish(8);
    QTest::qWait(200);
    QCOMPARE(dismissed.count(), 1);
  }
  void failedQuickSaveKeepsCaptureForRetry() {
    ImageStore store;
    Studio studio(&store);
    const auto blocked = temp.filePath("not-a-directory");
    QFile obstruction(blocked);
    QVERIFY(obstruction.open(QIODevice::WriteOnly));
    obstruction.write("test");
    obstruction.close();
    studio.setOutputDirectory(QUrl::fromLocalFile(blocked));
    QSignalSpy chosen(&studio, &Studio::chooserRequested);
    QSignalSpy dismissed(&studio, &Studio::dismissRequested);
    studio.capture(false);
    QTRY_COMPARE_WITH_TIMEOUT(chosen.count(), 1, 8000);
    QTRY_VERIFY(!studio.rendering());
    studio.chooseFinish(8);
    QTRY_COMPARE_WITH_TIMEOUT(studio.quickState(), QString("failed"), 8000);
    QCOMPARE(dismissed.count(), 0);
    QVERIFY(studio.savedPath().isEmpty());
    studio.openEditor();
    QCOMPARE(studio.quickState(), QString("editing"));
    studio.setOutputDirectory(QUrl::fromLocalFile(temp.filePath("retry")));
    studio.showFinishes();
    studio.chooseFinish(8);
    QTRY_COMPARE_WITH_TIMEOUT(dismissed.count(), 1, 8000);
    QVERIFY(QFileInfo::exists(studio.savedPath()));
  }
  void cancelledCaptureNeverDismissesAsAccepted() {
    ImageStore store;
    Studio studio(&store);
    QSignalSpy selected(&studio, &Studio::selectionReady);
    studio.capture(true);
    QTRY_COMPARE_WITH_TIMEOUT(selected.count(), 1, 8000);
    studio.cancelSelection();
    QCOMPARE(studio.quickState(), QString("cancelled"));
    QVERIFY(studio.savedPath().isEmpty());
    studio.chooseFinish(0);
    QVERIFY(!studio.busy());
  }
  void nativeCaptureHasDisplayPixels() {
    const QString name = QGuiApplication::primaryScreen()->name();
    QImage image;
    QString error;
    MonitorInfo info;
    info.name = name;
    QVERIFY2(captureOutputSurface(info, image, error), qPrintable(error));
    QVERIFY(image.width() > 0);
    QVERIFY(image.height() > 0);
  }
};
QTEST_MAIN(PipelineTest)
#include "pipeline-test.moc"
