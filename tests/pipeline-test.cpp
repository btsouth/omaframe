#include "capture-session.hpp"
#include "capture.hpp"
#include "studio.hpp"
#include "video.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QScreen>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
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
  static QByteArray videoPacketHash(const QString &path) {
    QProcess process;
    process.start("ffmpeg", {"-hide_banner", "-loglevel", "error", "-i",
                              path, "-map", "0:v:0", "-c", "copy", "-f",
                              "hash", "-hash", "sha256", "-"});
    if (!process.waitForFinished(10000) || process.exitCode() != 0)
      return {};
    return process.readAllStandardOutput().trimmed();
  }
  static QStringList videoPacketHashes(const QString &path) {
    QProcess process;
    process.start("ffprobe", {"-v", "error", "-select_streams", "v:0",
                               "-show_packets", "-show_data_hash", "sha256",
                               "-show_entries", "packet=data_hash", "-of",
                               "json", path});
    if (!process.waitForFinished(10000) || process.exitCode() != 0)
      return {};
    QStringList hashes;
    const auto packets = QJsonDocument::fromJson(process.readAllStandardOutput())
                             .object()
                             .value("packets")
                             .toArray();
    for (const auto &packet : packets)
      hashes << packet.toObject().value("data_hash").toString();
    return hashes;
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
    // Unredacted private copies are kept only when the user turns them on.
    QCOMPARE(studio.originalsCount(), 0);
    QVERIFY(!studio.keepOriginals());
    studio.setKeepOriginals(true);
    studio.accept();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.busy(), 15000);
    QVERIFY(studio.originalsCount() > 0);
    const QString finishedPath = studio.savedPath();
    studio.clearOriginals();
    QCOMPARE(studio.originalsCount(), 0);
    QVERIFY(QFileInfo::exists(finishedPath));
    studio.setKeepOriginals(false);
  }
  void labelsAreTypedInPlaceAndMarksPickedUpByEdges() {
    ImageStore store;
    Studio studio(&store);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    studio.setEditing(true);
    studio.edit("text", 0.2, 0.2, 0.2, 0.2, "Draft");
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.selectedAnnotation().value("fontPx").toInt(), studio.newTextPixels());
    // While a label is being typed its rendered copy is hidden, and the
    // typed words are applied even if a preview is still rendering.
    studio.beginTextEdit();
    QVERIFY(studio.textEditing());
    studio.endTextEdit("Final words", true);
    QVERIFY(!studio.textEditing());
    QCOMPARE(studio.selectedAnnotation().value("text").toString(), QString("Final words"));
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    studio.beginTextEdit();
    studio.endTextEdit("Ignored", false);
    QCOMPARE(studio.selectedAnnotation().value("text").toString(), QString("Final words"));
    studio.undo();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.selectedAnnotation().value("text").toString(), QString("Draft"));
    studio.beginTextEdit();
    studio.endTextEdit("   ", true);
    QVERIFY(studio.selectedAnnotation().isEmpty());
    QVERIFY(studio.status().contains("Empty label removed"));
    // A drawing tool can start a new mark inside a highlight; its border
    // still picks the highlight up.
    studio.edit("highlight", 0.4, 0.4, 0.8, 0.8);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.hitAt(0.6, 0.6).contains("index"));
    QVERIFY(!studio.hitAt(0.6, 0.6, true).contains("index"));
    QVERIFY(studio.hitAt(0.4, 0.6, true).contains("index"));
    studio.clearSelection();
    QVERIFY(studio.selectedAnnotation().isEmpty());
    studio.select(studio.hitAt(0.4, 0.6).value("index").toInt());
    QCOMPARE(studio.selectedAnnotation().value("type").toString(), QString("highlight"));
    // Finish thumbnails wait until the editor closes.
    studio.setEditing(false);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    studio.closeImage();
    QVERIFY(!studio.hasImage());
    QVERIFY(studio.selectedAnnotation().isEmpty());
  }
  void annotationsCanMoveResizeDeleteAndReframe() {
    ImageStore store;
    Studio studio(&store);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    studio.edit("box", 0.1, 0.1, 0.3, 0.3);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.selectAt(0.2, 0.2) >= 0);
    studio.moveSelected(0.2, 0.1);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(qAbs(studio.selectedAnnotation().value("x1").toDouble() - 0.3) < 1e-8);
    studio.undo();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.selectAt(0.2, 0.2) >= 0);
    QVERIFY(qAbs(studio.selectedAnnotation().value("x1").toDouble() - 0.1) < 1e-8);
    studio.redo();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.selectAt(0.4, 0.3) >= 0);
    studio.resizeSelected(2, 0.65, 0.55);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(qAbs(studio.selectedAnnotation().value("x2").toDouble() - 0.65) < 1e-8);
    studio.edit("crop", 0.25, 0.0, 0.75, 1.0);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.hasCrop());
    QCOMPARE(studio.selectedAnnotation().size(), 0);
    QVERIFY(studio.selectAt(0.5, 0.35) >= 0);
    studio.deleteSelected();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.selectAt(0.5, 0.35), -1);
    studio.undo();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.selectAt(0.5, 0.35) >= 0);
    studio.clearCrop();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(!studio.hasCrop());
    QVERIFY(studio.selectAt(0.5, 0.35) >= 0);
  }
  void annotationsCanDuplicateReorderAndKeepSelectionThroughHistory() {
    ImageStore store;
    Studio studio(&store);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    studio.edit("box", 0.1, 0.1, 0.4, 0.4);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    studio.edit("box", 0.3, 0.3, 0.6, 0.6);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.selectAt(0.2, 0.2) >= 0);
    QCOMPARE(studio.selectedAnnotation().value("layer").toInt(), 1);
    studio.moveSelectedLayer(1);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.selectedAnnotation().value("layer").toInt(), 2);
    QVERIFY(studio.selectAt(0.35, 0.35) >= 0);
    QCOMPARE(studio.selectedAnnotation().value("x1").toDouble(), 0.1);
    studio.undo();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.selectedAnnotation().value("layer").toInt(), 1);
    QVERIFY(studio.selectAt(0.35, 0.35) >= 0);
    QCOMPARE(studio.selectedAnnotation().value("x1").toDouble(), 0.3);
    studio.redo();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.selectedAnnotation().value("layer").toInt(), 2);
    studio.duplicateSelected();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.selectedAnnotation().value("layers").toInt(), 3);
    QVERIFY(studio.selectedAnnotation().value("x1").toDouble() > 0.1);
    studio.deleteSelected();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.selectedAnnotation().isEmpty());
    studio.undo();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.selectedAnnotation().value("layers").toInt(), 3);
    QCOMPARE(studio.selectedAnnotation().value("layer").toInt(), 3);
  }
  void textCanBeSelectedEditedMovedAndUndone() {
    ImageStore store;
    Studio studio(&store);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    studio.edit("text", 0.2, 0.2, 0.2, 0.2, "First label");
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    auto selected = studio.selectedAnnotation();
    const double initialSize = selected.value("size").toDouble();
    const double beforeWidth = selected.value("boundW").toDouble();
    const double hitX = selected.value("boundX").toDouble() +
                        selected.value("boundW").toDouble() * 0.85;
    const double hitY = selected.value("boundY").toDouble() +
                        selected.value("boundH").toDouble() * 0.5;
    QVERIFY(studio.selectAt(hitX, hitY) >= 0);
    QCOMPARE(studio.selectedAnnotation().value("text").toString(),
             QString("First label"));
    studio.updateSelectedText("A longer revised label");
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    selected = studio.selectedAnnotation();
    QCOMPARE(selected.value("text").toString(), QString("A longer revised label"));
    QVERIFY(selected.value("boundW").toDouble() > beforeWidth);
    QVERIFY(studio.selectAt(selected.value("boundX").toDouble() +
                                selected.value("boundW").toDouble() * 0.9,
                            selected.value("boundY").toDouble() +
                                selected.value("boundH").toDouble() * 0.5) >= 0);
    studio.moveSelected(0.1, 0.1);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    selected = studio.selectedAnnotation();
    QVERIFY(qAbs(selected.value("x1").toDouble() - 0.3) < 1e-8);
    const double movedWidth = selected.value("boundW").toDouble();
    studio.resizeSelected(2, selected.value("boundX").toDouble() + movedWidth * 1.5,
                          selected.value("boundY").toDouble() +
                              selected.value("boundH").toDouble() * 1.5);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.selectedAnnotation().value("size").toDouble() > 1.3);
    QVERIFY(studio.selectedAnnotation().value("boundW").toDouble() > movedWidth);
    studio.setSelectedColor("#459ec7");
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.selectedAnnotation().value("color").toString(),
             QString("#459ec7"));
    studio.undo();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.selectAt(0.31, 0.31) >= 0);
    QCOMPARE(studio.selectedAnnotation().value("color").toString(),
             QString("#ffffff"));
    studio.undo();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.selectAt(0.31, 0.31) >= 0);
    QVERIFY(qAbs(studio.selectedAnnotation().value("size").toDouble() - initialSize) < 1e-8);
    studio.undo();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.selectAt(hitX, hitY) >= 0);
    QCOMPARE(studio.selectedAnnotation().value("text").toString(),
             QString("A longer revised label"));
    studio.undo();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.selectAt(hitX, hitY) >= 0);
    QCOMPARE(studio.selectedAnnotation().value("text").toString(),
             QString("First label"));
  }
  void largeTextHasDirectSizeAndEditableStyles() {
    ImageStore store;
    Studio studio(&store);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    studio.edit("text", 0.1, 0.2, 0.1, 0.2, "Go");
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    auto mark = studio.selectedAnnotation();
    const double right = mark.value("boundX").toDouble() + mark.value("boundW").toDouble();
    const double bottom = mark.value("boundY").toDouble() + mark.value("boundH").toDouble();
    studio.resizeSelected(0, mark.value("boundX").toDouble() -
                                 mark.value("boundW").toDouble(),
                          mark.value("boundY").toDouble() -
                                 mark.value("boundH").toDouble());
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    mark = studio.selectedAnnotation();
    QVERIFY(mark.value("fontPx").toInt() > 30);
    QVERIFY(qAbs(mark.value("boundX").toDouble() +
                 mark.value("boundW").toDouble() - right) < 0.02);
    QVERIFY(qAbs(mark.value("boundY").toDouble() +
                 mark.value("boundH").toDouble() - bottom) < 0.02);
    studio.setSelectedFontPixels(192);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.selectedAnnotation().value("fontPx").toInt(), 192);
    studio.setSelectedTextStyle("shadow");
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.selectedAnnotation().value("textStyle").toString(),
             QString("shadow"));
    studio.setSelectedTextStyle("box");
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    studio.setSelectedTextAlignment("left");
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.selectedAnnotation().value("textAlign").toString(),
             QString("left"));
    studio.setSelectedBackground("#101044");
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    studio.setSelectedBackgroundOpacity(0.5);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.selectedAnnotation().value("background").toString(),
             QString("#101044"));
    QCOMPARE(studio.selectedAnnotation().value("backgroundOpacity").toDouble(),
             0.5);
    studio.setSelectedFontPixels(999);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.selectedAnnotation().value("fontPx").toInt() > 300);
    QVERIFY(studio.selectedAnnotation().value("fontPx").toInt() <= 4096);
    studio.updateSelectedText("A longer label that should wrap instead of running beyond the image");
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    mark = studio.selectedAnnotation();
    QVERIFY(mark.value("fontPx").toInt() < 999);
    QVERIFY(mark.value("boundW").toDouble() <= 0.86);
    QVERIFY(mark.value("boundH").toDouble() <= 1.0);
    QVERIFY(mark.value("boundX").toDouble() + mark.value("boundW").toDouble() <= 1.00001);
    QVERIFY(mark.value("boundY").toDouble() + mark.value("boundH").toDouble() <= 1.00001);
  }
  void numberedStepCanBeResizedRestyledAndDeleted() {
    ImageStore store;
    Studio studio(&store);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    studio.edit("step", 0.4, 0.4, 0.4, 0.4);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    const double originalX = studio.selectedAnnotation().value("x1").toDouble();
    studio.nudgeSelected(10, 0);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.selectedAnnotation().value("x1").toDouble() > originalX);
    const auto before = studio.selectedAnnotation();
    studio.resizeSelected(2, before.value("boundX").toDouble() +
                                 before.value("boundW").toDouble() * 1.5,
                          before.value("boundY").toDouble() +
                                 before.value("boundH").toDouble() * 1.5);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.selectedAnnotation().value("size").toDouble() > 1.0);
    studio.setSelectedColor("#4ca782");
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.selectedAnnotation().value("color").toString(),
             QString("#4ca782"));
    studio.deleteSelected();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.selectedAnnotation().isEmpty());
    studio.undo();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.selectAt(0.4, 0.4) >= 0);
  }
  void freehandStrokeCanBeSelectedMovedResizedAndUndone() {
    ImageStore store;
    Studio studio(&store);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    studio.addStroke({QVariantMap{{"x", 0.2}, {"y", 0.2}},
                      QVariantMap{{"x", 0.4}, {"y", 0.4}},
                      QVariantMap{{"x", 0.6}, {"y", 0.2}}});
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.selectedAnnotation().value("type").toString(), QString("pen"));
    QVERIFY(studio.selectAt(0.4, 0.4) >= 0);
    studio.moveSelected(0.1, 0.1);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(qAbs(studio.selectedAnnotation().value("boundX").toDouble() - 0.3) < 1e-8);
    studio.resizeSelected(2, 0.8, 0.6);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.selectedAnnotation().value("boundW").toDouble() > 0.4);
    studio.setSelectedColor("#459ec7");
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.selectedAnnotation().value("color").toString(), QString("#459ec7"));
    studio.deleteSelected();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    studio.undo();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.selectAt(0.55, 0.6) >= 0);
  }
  void clipboardRetryUsesAlreadySavedPng() {
    const QByteArray originalPath = qgetenv("PATH");
    struct RestorePath {
      QByteArray value;
      ~RestorePath() { qputenv("PATH", value); }
    } restore{originalPath};
    const QString stubDir = temp.filePath("failing-clipboard");
    QVERIFY(QDir().mkpath(stubDir));
    QFile stub(stubDir + "/wl-copy");
    QVERIFY(stub.open(QIODevice::WriteOnly));
    stub.write("#!/bin/sh\nexit 1\n");
    stub.close();
    QVERIFY(stub.setPermissions(QFile::ReadOwner | QFile::WriteOwner |
                                QFile::ExeOwner));
    qputenv("PATH", QFile::encodeName(stubDir) + ':' + originalPath);

    ImageStore store;
    Studio studio(&store);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    const QString outputDir = temp.filePath("retry-copy");
    studio.setOutputDirectory(QUrl::fromLocalFile(outputDir));
    studio.accept();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.busy(), 15000);
    QCOMPARE(studio.recoveryAction(), QString("Retry copy"));
    const QString savedPath = studio.savedPath();
    QVERIFY(QFileInfo::exists(savedPath));
    QCOMPARE(QDir(outputDir).entryList({"*.png"}, QDir::Files).size(), 1);

    qputenv("PATH", originalPath);
    studio.retryOutput();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.busy(), 15000);
    QVERIFY(studio.recoveryAction().isEmpty());
    QCOMPARE(studio.savedPath(), savedPath);
    QCOMPARE(QDir(outputDir).entryList({"*.png"}, QDir::Files).size(), 1);
    studio.setStyle(studio.style() == 0 ? 1 : 0);
    QVERIFY(studio.savedPath().isEmpty());
    QVERIFY(studio.recoveryAction().isEmpty());
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
  void unchangedRecordingCanBeKeptWithoutExport() {
    Video video;
    QSignalSpy kept(&video, &Video::originalAccepted);
    QSignalSpy exported(&video, &Video::exported);
    video.setOutputDirectory(QUrl::fromLocalFile(temp.filePath("kept-clips")));
    video.open(QUrl::fromLocalFile(recording));
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 12000);
    video.keepOriginal();
    QCOMPARE(kept.size(), 1);
    QCOMPARE(exported.size(), 0);
    QCOMPARE(video.savedPath(), recording);
    QVERIFY(!QDir(temp.filePath("kept-clips")).exists());
    video.finish();
    QCOMPARE(kept.size(), 2);
    QVERIFY(video.copyFile());
    QProcess paste;
    paste.start("wl-paste", {"--type", "text/uri-list"});
    QVERIFY(paste.waitForFinished(3000));
    QCOMPARE(QString::fromUtf8(paste.readAllStandardOutput()).trimmed(),
             QUrl::fromLocalFile(recording).toString());
  }
  void editedClipIsNamedAfterItsSourceAndCanBeCopied() {
    const QString source = temp.filePath("Recording-test.mp4");
    QFile::remove(source);
    QVERIFY(QFile::copy(recording, source));
    const QString folder = temp.filePath("named-clips");
    Video video;
    video.setOutputDirectory(QUrl::fromLocalFile(folder));
    video.open(QUrl::fromLocalFile(source));
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 12000);
    video.exportClip(0, 2, false);
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 20000);
    QCOMPARE(video.savedPath(), folder + "/Recording-test-edited.mp4");
    QCOMPARE(video.savedName(), QString("Recording-test-edited.mp4"));
    QVERIFY(video.savedSummary().startsWith("2.0 s · "));
    // A second export never overwrites the first.
    video.exportClip(0, 1.5, true);
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 20000);
    QCOMPARE(video.savedPath(), folder + "/Recording-test-edited-2.mp4");
    QVERIFY(QFileInfo::exists(folder + "/Recording-test-edited.mp4"));
    // The saved clip goes on the clipboard as a file.
    QVERIFY(video.copyFile());
    QProcess paste;
    paste.start("wl-paste", {"--type", "text/uri-list"});
    QVERIFY(paste.waitForFinished(3000));
    QCOMPARE(QString::fromUtf8(paste.readAllStandardOutput()).trimmed(),
             QUrl::fromLocalFile(video.savedPath()).toString());
    QVERIFY(video.status().contains("on the clipboard"));
    QVERIFY(QFileInfo::exists(source));
  }
  void editableDraftSurvivesRestartAndCanBeRemoved() {
    const QString input = temp.filePath("draft-input.png");
    QImage source(320, 200, QImage::Format_RGB32);
    source.fill(Qt::white);
    QVERIFY(source.save(input));
    ImageStore firstStore;
    Studio first(&firstStore, false);
    first.open(QUrl::fromLocalFile(input));
    QTRY_VERIFY_WITH_TIMEOUT(!first.busy() && !first.rendering(), 8000);
    const int priorDrafts = first.drafts().size();
    first.setStyle(1);
    QTRY_VERIFY_WITH_TIMEOUT(!first.rendering(), 8000);
    first.saveDraftNow();
    QCOMPARE(first.drafts().size(), priorDrafts);
    first.edit("text", 0.25, 0.3, 0.25, 0.3, "Editable again");
    QTRY_VERIFY_WITH_TIMEOUT(!first.rendering(), 8000);
    first.setSelectedColor("#459ec7");
    QTRY_VERIFY_WITH_TIMEOUT(!first.rendering(), 8000);
    first.saveDraftNow();
    QVERIFY(!first.drafts().isEmpty());
    const QString id = first.drafts().first().toMap().value("id").toString();
    const QString directory = QStandardPaths::writableLocation(
                                  QStandardPaths::AppLocalDataLocation) + "/drafts/";
    const QFileInfo image(directory + id + ".png"), metadata(directory + id + ".json");
    QVERIFY(image.exists());
    QVERIFY(metadata.exists());
    QVERIFY(!(image.permissions() & QFile::ReadGroup));
    QVERIFY(!(metadata.permissions() & QFile::ReadOther));
    first.updateSelectedText("Latest edit");
    QTRY_VERIFY_WITH_TIMEOUT(!first.rendering(), 8000);
    first.resumeDraft(id);
    QTRY_VERIFY_WITH_TIMEOUT(!first.rendering(), 8000);
    QCOMPARE(first.selectedAnnotation().value("text").toString(),
             QString("Latest edit"));

    ImageStore reopenedStore;
    Studio reopened(&reopenedStore, false);
    reopened.resumeDraft(id);
    QTRY_VERIFY_WITH_TIMEOUT(!reopened.rendering(), 8000);
    QCOMPARE(reopened.selectedAnnotation().value("text").toString(),
             QString("Latest edit"));
    QCOMPARE(reopened.selectedAnnotation().value("color").toString(),
             QString("#459ec7"));
    QCOMPARE(reopened.style(), 1);
    reopened.deleteSelected();
    QTRY_VERIFY_WITH_TIMEOUT(!reopened.rendering(), 8000);
    reopened.saveDraftNow();
    const QString secondInput = temp.filePath("another-draft.png");
    QVERIFY(source.save(secondInput));
    reopened.open(QUrl::fromLocalFile(secondInput));
    QTRY_VERIFY_WITH_TIMEOUT(!reopened.busy() && !reopened.rendering(), 8000);
    reopened.edit("step", 0.5, 0.5, 0.5, 0.5);
    QTRY_VERIFY_WITH_TIMEOUT(!reopened.rendering(), 8000);
    reopened.saveDraftNow();
    QString secondId;
    for (const auto &draft : reopened.drafts()) {
      const QString candidate = draft.toMap().value("id").toString();
      if (candidate != id && draft.toMap().value("name") == "another-draft.png")
        secondId = candidate;
    }
    QVERIFY(!secondId.isEmpty());
    QVERIFY(QFileInfo::exists(image.absoluteFilePath()));
    reopened.deleteDraft(id);
    reopened.deleteDraft(secondId);
    QVERIFY(!QFileInfo::exists(image.absoluteFilePath()));
    QVERIFY(!QFileInfo::exists(metadata.absoluteFilePath()));
  }
  void trimPreservesTracksMuteRemovesThem() {
    QByteArray originalHash = QCryptographicHash::hash(
        contents(recording), QCryptographicHash::Sha256);
    Video video;
    QSignalSpy exported(&video, &Video::exported);
    video.setOutputDirectory(QUrl::fromLocalFile(temp.filePath("clips")));
    video.open(QUrl::fromLocalFile(recording));
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 12000);
    QCOMPARE(video.audioTracks(), 2);
    video.exportClip(1.2, 4.8, false);
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 20000);
    QVERIFY2(!video.savedPath().isEmpty(), qPrintable(video.status()));
    QCOMPARE(exported.size(), 1);
    QCOMPARE(exported.first().first().toUrl().toLocalFile(), video.savedPath());
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
    QCOMPARE(exported.size(), 2);
    QCOMPARE(exported.last().first().toUrl().toLocalFile(), video.savedPath());
    QCOMPARE(probe(video.savedPath()).value("streams").toArray().size(), 1);
    QCOMPARE(QCryptographicHash::hash(contents(recording),
                                      QCryptographicHash::Sha256),
             originalHash);
  }
  void unchangedCompatibleClipKeepsEncodedVideo() {
    Video video;
    video.setOutputDirectory(QUrl::fromLocalFile(temp.filePath("unchanged-clips")));
    video.open(QUrl::fromLocalFile(recording));
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 12000);
    video.exportClip(0, video.duration(), false);
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 15000);
    QVERIFY2(!video.savedPath().isEmpty(), qPrintable(video.status()));
    const auto output = probe(video.savedPath());
    QCOMPARE(output.value("streams").toArray().size(), 3);
    QVERIFY(!output.value("format").toObject().value("tags").toObject().contains("comment"));
    const QByteArray inputHash = videoPacketHash(recording);
    QVERIFY(!inputHash.isEmpty());
    QCOMPARE(videoPacketHash(video.savedPath()), inputHash);
  }
  void endTrimAndMuteCopyOriginalVideoPackets() {
    Video video;
    video.setOutputDirectory(QUrl::fromLocalFile(temp.filePath("end-trim-clips")));
    video.open(QUrl::fromLocalFile(recording));
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 12000);
    const auto originalPackets = videoPacketHashes(recording);
    QVERIFY(originalPackets.size() > 100);

    for (bool mute : {false, true}) {
      video.exportClip(0, 4.2, mute);
      QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 15000);
      QVERIFY2(!video.savedPath().isEmpty(), qPrintable(video.status()));
      const auto output = probe(video.savedPath());
      QVERIFY(qAbs(output.value("format").toObject().value("duration")
                       .toString().toDouble() - 4.2) < 0.08);
      QCOMPARE(output.value("streams").toArray().size(), mute ? 1 : 3);
      const auto packets = videoPacketHashes(video.savedPath());
      QVERIFY(!packets.isEmpty());
      QVERIFY(packets.size() < originalPackets.size());
      QCOMPARE(originalPackets.mid(0, packets.size()), packets);
    }
  }
  void removedSectionsJoinVideoAndBothAudioTracks() {
    const QByteArray originalHash = QCryptographicHash::hash(
        contents(recording), QCryptographicHash::Sha256);
    Video video;
    QSignalSpy exported(&video, &Video::exported);
    video.setOutputDirectory(QUrl::fromLocalFile(temp.filePath("cut-clips")));
    video.open(QUrl::fromLocalFile(recording));
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 12000);
    const QVariantList cuts{
        QVariantMap{{"start", 3.1}, {"end", 3.8}},
        QVariantMap{{"start", 1.0}, {"end", 2.0}}};
    video.exportEdited(0.5, 5.5, false, cuts);
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 20000);
    QVERIFY2(!video.savedPath().isEmpty(), qPrintable(video.status()));
    QCOMPARE(exported.size(), 1);
    const auto output = probe(video.savedPath());
    QVERIFY(qAbs(output.value("format").toObject().value("duration")
                     .toString().toDouble() - 3.3) < 0.12);
    QCOMPARE(output.value("streams").toArray().size(), 3);
    QProcess decode;
    decode.start("ffmpeg", {"-v", "error", "-xerror", "-i", video.savedPath(),
                            "-f", "null", "-"});
    QVERIFY(decode.waitForFinished(20000));
    QCOMPARE(decode.exitCode(), 0);
    QCOMPARE(QCryptographicHash::hash(contents(recording),
                                      QCryptographicHash::Sha256),
             originalHash);
    video.exportEdited(0.5, 5.5, true, cuts);
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 20000);
    QVERIFY2(!video.savedPath().isEmpty(), qPrintable(video.status()));
    QCOMPARE(exported.size(), 2);
    QCOMPARE(probe(video.savedPath()).value("streams").toArray().size(), 1);
    video.exportEdited(0.5, 5.5, false,
                       {QVariantMap{{"start", 0.5}, {"end", 5.5}}});
    QVERIFY(!video.busy());
    QCOMPARE(exported.size(), 2);
    QVERIFY(video.status().contains("Keep at least"));
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
    QSignalSpy exported(&video, &Video::exported);
    video.setOutputDirectory(QUrl::fromLocalFile(directory));
    video.open(QUrl::fromLocalFile(recording));
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 12000);
    video.exportClip(0, 5.5, false);
    video.cancel();
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 5000);
    QVERIFY(video.savedPath().isEmpty());
    QCOMPARE(exported.size(), 0);
    QVERIFY(QDir(directory).entryList(QDir::Files | QDir::Hidden).isEmpty());
  }
  void successfulEncoderWithInvalidMp4IsRejected() {
    const QString directory = temp.filePath("invalid-export");
    const QString fakeBin = temp.filePath("fake-encoder");
    QVERIFY(QDir().mkpath(fakeBin));
    QFile fake(fakeBin + "/ffmpeg");
    QVERIFY(fake.open(QIODevice::WriteOnly));
    fake.write("#!/bin/sh\nfor output; do :; done\nprintf 'not a video' > \"$output\"\n");
    fake.close();
    QVERIFY(fake.setPermissions(QFile::ReadOwner | QFile::WriteOwner |
                                QFile::ExeOwner));

    Video video;
    QSignalSpy exported(&video, &Video::exported);
    video.setOutputDirectory(QUrl::fromLocalFile(directory));
    video.open(QUrl::fromLocalFile(recording));
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 12000);
    const QByteArray originalPath = qgetenv("PATH");
    qputenv("PATH", QFile::encodeName(fakeBin) + ':' + originalPath);
    video.exportClip(0.5, 2.5, false);
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 12000);
    qputenv("PATH", originalPath);
    QVERIFY(video.savedPath().isEmpty());
    QCOMPARE(exported.size(), 0);
    QVERIFY(video.status().contains("could not be played"));
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
  void repeatLastAreaSurvivesRestartAndChecksDisplaySize() {
    QSettings().remove("lastArea");
    ImageStore firstStore;
    Studio first(&firstStore, false);
    QSignalSpy firstSelection(&first, &Studio::selectionReady);
    first.capture(true);
    QTRY_COMPARE_WITH_TIMEOUT(firstSelection.count(), 1, 8000);
    const auto monitor = firstSelection.first().first().toStringList().first();
    first.finishSelection(monitor, 0.1, 0.2, 0.6, 0.7);
    QTRY_VERIFY_WITH_TIMEOUT(!first.rendering(), 8000);
    const QString dimensions = first.dimensions();
    QVERIFY(first.hasLastArea());

    ImageStore secondStore;
    Studio second(&secondStore, false);
    QVERIFY(second.hasLastArea());
    QSignalSpy secondSelection(&second, &Studio::selectionReady);
    QSignalSpy chooser(&second, &Studio::chooserRequested);
    second.repeatLastArea();
    QTRY_COMPARE_WITH_TIMEOUT(chooser.count(), 1, 8000);
    QCOMPARE(secondSelection.count(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(!second.rendering(), 8000);
    QCOMPARE(second.dimensions(), dimensions);

    QSettings().setValue("lastArea/pixels", QSize(1, 1));
    ImageStore changedStore;
    Studio changed(&changedStore, false);
    QSignalSpy changedSelection(&changed, &Studio::selectionReady);
    changed.repeatLastArea();
    QTRY_COMPARE_WITH_TIMEOUT(changedSelection.count(), 1, 8000);
    QCOMPARE(changed.quickState(), QString("selecting"));
    QVERIFY(changed.status().contains("display changed", Qt::CaseInsensitive));
    changed.cancelSelection();
    QSettings().remove("lastArea");
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
