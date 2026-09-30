#include "marks.hpp"
#include "video.hpp"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class VideoMarksTest : public QObject {
  Q_OBJECT
  QTemporaryDir temp;
  QString plain, pattern;

  static bool makeClip(const QString &source, const QString &path,
                       int duration = 4) {
    QProcess ffmpeg;
    ffmpeg.start("ffmpeg",
                 {"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                  source, "-t", QString::number(duration), "-c:v", "libx264",
                  "-preset", "ultrafast", "-pix_fmt", "yuv420p", path});
    return ffmpeg.waitForFinished(20000) && ffmpeg.exitCode() == 0;
  }
  // The frame shown `seconds` into a file, as RGB.
  static QImage frameAt(const QString &path, double seconds, QSize size) {
    QProcess ffmpeg;
    ffmpeg.start("ffmpeg", {"-hide_banner", "-loglevel", "error", "-ss",
                            QString::number(seconds, 'f', 3), "-i", path,
                            "-frames:v", "1", "-f", "rawvideo", "-pix_fmt",
                            "rgb24", "-"});
    if (!ffmpeg.waitForFinished(10000) || ffmpeg.exitCode() != 0)
      return {};
    const QByteArray data = ffmpeg.readAllStandardOutput();
    if (data.size() != size.width() * size.height() * 3)
      return {};
    return QImage(reinterpret_cast<const uchar *>(data.constData()),
                  size.width(), size.height(), size.width() * 3,
                  QImage::Format_RGB888)
        .copy();
  }
  static bool redacted(const QImage &frame, int x, int y) {
    const QColor c = frame.pixelColor(x, y);
    return qAbs(c.red() - 0x15) < 14 && qAbs(c.green() - 0x1a) < 14 &&
           qAbs(c.blue() - 0x20) < 14;
  }
  static bool white(const QImage &frame, int x, int y) {
    const QColor c = frame.pixelColor(x, y);
    return c.red() > 235 && c.green() > 235 && c.blue() > 235;
  }
  // How sharply neighbouring pixels differ inside `area`: lower is softer.
  // Squared, because a blurred edge changes as much in total, only gradually.
  static double detail(const QImage &frame, QRect area) {
    double total = 0;
    for (int y = area.top(); y <= area.bottom(); ++y)
      for (int x = area.left(); x < area.right(); ++x) {
        const int step = qGray(frame.pixel(x, y)) - qGray(frame.pixel(x + 1, y));
        total += step * step;
      }
    return total;
  }
  static Frame::Edit mark(const QString &type, double start, double end) {
    Frame::Edit edit{type, {0.25, 0.25}, {0.5, 0.5}};
    edit.start = start;
    edit.end = end;
    return edit;
  }
  // A white clip with a redaction from 1 to 2 seconds, ready to export.
  void openRedacted(Video &video, const QString &folder) {
    video.setOutputDirectory(QUrl::fromLocalFile(temp.filePath(folder)));
    video.open(QUrl::fromLocalFile(plain));
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 12000);
    QCOMPARE(video.frameSize(), QSize(320, 240));
    video.marks()->edit("redact", 0.25, 0.25, 0.5, 0.5);
    video.marks()->setSelectedTimes(1, 2);
    QCOMPARE(video.marks()->annotations().size(), 1);
  }

private slots:
  void cropAndCameraAreComposedBeforeCuts() {
    const QString source = temp.filePath("screen-camera.mp4");
    const QString camera = temp.filePath("screen-camera-webcam.mp4");
    QVERIFY(QFile::copy(plain, source));
    QVERIFY(makeClip("color=c=red:size=640x360:rate=24", camera));
    QFile sidecar(source + ".camera.json");
    QVERIFY(sidecar.open(QIODevice::WriteOnly));
    sidecar.write(
        QJsonDocument(QJsonObject{{"version", 1},
                                  {"file", QFileInfo(camera).fileName()},
                                  {"duration", 4.0}})
            .toJson());
    sidecar.close();
    Video video;
    video.setOutputDirectory(QUrl::fromLocalFile(temp.filePath("crop-camera")));
    video.open(QUrl::fromLocalFile(source));
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 12000);
    QCOMPARE(video.cameraSource(), QUrl::fromLocalFile(camera));
    video.marks()->edit("redact", 0.25, 0.25, 0.5, 0.5);
    video.marks()->edit("crop", 0.25, 0.25, 0.75, 0.75);
    QCOMPARE(video.outputSize(), QSize(160, 120));
    video.setCameraLayout(
        {{"visible", true}, {"width", 0.5}, {"x", 0.5}, {"y", 0.6}});
    video.exportEdited(0.5, 3.5, false,
                       {QVariantMap{{"start", 1.0}, {"end", 2.0}}});
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 12000);
    QVERIFY2(!video.savedPath().isEmpty(), qPrintable(video.status()));
    const QImage frame = frameAt(video.savedPath(), 0.1, {160, 120});
    QVERIFY(!frame.isNull());
    QVERIFY(redacted(frame, 10, 10));
    const QColor face = frame.pixelColor(120, 90);
    QVERIFY(face.red() > 220 && face.green() < 30);
    const QString mp4 = video.savedPath();
    const QString summary = video.savedSummary();
    QSignalSpy gifSaved(&video, &Video::gifExported);
    QSignalSpy mp4Saved(&video, &Video::exported);
    video.exportGif(0.5, 3.5, {QVariantMap{{"start", 1.0}, {"end", 2.0}}});
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 12000);
    QCOMPARE(gifSaved.count(), 1);
    QCOMPARE(mp4Saved.count(), 0);
    QCOMPARE(video.savedPath(), mp4);
    QCOMPARE(video.savedSummary(), summary);
    const QImage gif = frameAt(video.gifPath(), 0.1, {160, 120});
    QVERIFY(!gif.isNull() && redacted(gif, 10, 10));
    QVERIFY(gif.pixelColor(120, 90).red() > 220);
    QProcess probe;
    probe.start("ffprobe", {"-v", "error", "-show_entries",
                            "format=duration:stream=codec_name,codec_type",
                            "-of", "json", video.gifPath()});
    QVERIFY(probe.waitForFinished(5000) && probe.exitCode() == 0);
    const auto info =
        QJsonDocument::fromJson(probe.readAllStandardOutput()).object();
    QCOMPARE(info.value("streams").toArray().size(), 1);
    QCOMPARE(info.value("streams")
                 .toArray()[0]
                 .toObject()
                 .value("codec_name")
                 .toString(),
             QString("gif"));
    QVERIFY(qAbs(info.value("format")
                     .toObject()
                     .value("duration")
                     .toString()
                     .toDouble() -
                 2.0) < 0.1);
    QFile gifFile(video.gifPath());
    QVERIFY(gifFile.open(QIODevice::ReadOnly));
    QVERIFY(gifFile.readAll().contains(QByteArray("NETSCAPE2.0") +
                                       QByteArray::fromHex("03010000")));
    video.setCameraLayout({{"visible", false}});
    video.exportEdited(0, 4, false, {});
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 12000);
    const QImage hidden = frameAt(video.savedPath(), 0.1, {160, 120});
    QVERIFY(!hidden.isNull() && white(hidden, 120, 90));
  }
  void gifWithoutEditsKeepsOriginalSizeAndCancellationKeepsMp4() {
    Video video;
    video.setOutputDirectory(
        QUrl::fromLocalFile(temp.filePath("gif-unedited")));
    video.open(QUrl::fromLocalFile(pattern));
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 12000);
    video.exportGif(0, 0.1, {});
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 12000);
    QVERIFY2(!video.gifPath().isEmpty(), qPrintable(video.status()));
    QVERIFY(!frameAt(video.gifPath(), 0, {320, 240}).isNull());
    video.exportClip(0, 4, false);
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 12000);
    const QString mp4 = video.savedPath();
    QVERIFY(!mp4.isEmpty());
    QSignalSpy failed(&video, &Video::exportFailed);
    QSignalSpy gifSaved(&video, &Video::gifExported);
    video.exportGif(0, 4, {});
    video.cancel();
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 12000);
    QCOMPARE(gifSaved.count(), 0);
    QCOMPARE(video.savedPath(), mp4);
    QVERIFY(QDir(temp.filePath("gif-unedited"))
                .entryList({"*.part.gif"}, QDir::Files | QDir::Hidden)
                .isEmpty());
  }
  void gifSizeAndLengthStaySuitableForSharing() {
    const QString source = temp.filePath("portrait.mp4");
    QVERIFY(makeClip("color=c=red:size=800x1200:rate=1", source, 31));
    Video video;
    video.setOutputDirectory(
        QUrl::fromLocalFile(temp.filePath("gif-portrait")));
    video.open(QUrl::fromLocalFile(source));
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 12000);
    QSignalSpy failed(&video, &Video::exportFailed);
    video.exportGif(0, 31, {});
    QCOMPARE(failed.count(), 1);
    QVERIFY(!video.busy() && video.gifPath().isEmpty());
    QVERIFY(video.status().contains("30 seconds"));
    video.exportGif(0, 31, {QVariantMap{{"start", 1.0}, {"end", 31.0}}});
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 12000);
    QVERIFY2(!video.gifPath().isEmpty(), qPrintable(video.status()));
    const QImage frame = frameAt(video.gifPath(), 0, {480, 720});
    QVERIFY(!frame.isNull() && frame.pixelColor(240, 360).red() > 220);
  }
  void draftRestoresCropTimesAndPointingMarks() {
    Video video;
    video.open(QUrl::fromLocalFile(pattern));
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 12000);
    video.marks()->edit("arrow", 0.8, 0.6, 0.2, 0.4);
    video.marks()->setSelectedTimes(1, 3);
    video.marks()->edit("text", 0.4, 0.4, 0.4, 0.4, "Resume this edit");
    video.marks()->edit("crop", 0.1, 0.1, 0.9, 0.9);
    const auto before = video.marks()->edits();
    video.setEditState(
        {{"clipStart", 0.5},
         {"clipEnd", 3.5},
         {"muted", true},
         {"cuts", QVariantList{QVariantMap{{"start", 1.5}, {"end", 2.0}}}},
         {"signature", "edited-pattern"}});
    QVERIFY(video.saveDraftNow());
    QString id;
    for (const auto &draft : video.drafts())
      if (draft.toMap().value("name").toString() == "pattern.mp4")
        id = draft.toMap().value("id").toString();
    QVERIFY(!id.isEmpty());
    Video reopened;
    reopened.resumeDraft(id);
    QTRY_VERIFY_WITH_TIMEOUT(!reopened.busy(), 12000);
    QCOMPARE(reopened.source(), video.source());
    QCOMPARE(reopened.editState(), video.editState());
    QCOMPARE(reopened.marks()->edits().size(), before.size());
    QCOMPARE(reopened.marks()->edits()[0].from, before[0].from);
    QCOMPARE(reopened.marks()->edits()[0].to, before[0].to);
    QCOMPARE(reopened.marks()->edits()[0].start, 1.0);
    QCOMPARE(reopened.marks()->edits()[0].end, 3.0);
    QCOMPARE(reopened.marks()->edits()[1].text, QString("Resume this edit"));
    QCOMPARE(reopened.cropPixels(), video.cropPixels());
    reopened.deleteDraft(id);
    QVERIFY(reopened.saveDraftNow());
    for (const auto &draft : reopened.drafts())
      QVERIFY(draft.toMap().value("id").toString() != id);
  }
  void changedOriginalDoesNotSilentlyRestoreDraft() {
    const QString source = temp.filePath("changed-original.mp4");
    QVERIFY(QFile::copy(plain, source));
    Video video;
    video.open(QUrl::fromLocalFile(source));
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 12000);
    video.setEditState({{"clipStart", 0.0},
                        {"clipEnd", 4.0},
                        {"muted", true},
                        {"cuts", QVariantList{}},
                        {"signature", "muted"}});
    QVERIFY(video.saveDraftNow());
    QString id;
    for (const auto &draft : video.drafts())
      if (draft.toMap().value("name").toString() == "changed-original.mp4")
        id = draft.toMap().value("id").toString();
    QVERIFY(!id.isEmpty());
    QFile changed(source);
    QVERIFY(changed.open(QIODevice::Append));
    changed.write("changed");
    changed.close();
    QVERIFY(!video.saveDraftNow());
    Video reopened;
    reopened.resumeDraft(id);
    QVERIFY(reopened.source().isEmpty());
    QVERIFY(reopened.status().contains("has changed"));
    reopened.deleteDraft(id);
  }
  void initTestCase() {
    QCoreApplication::setOrganizationName("Omaframe-test");
    QCoreApplication::setApplicationName("VideoMarks");
    QSettings().clear();
    QVERIFY(temp.isValid());
    plain = temp.filePath("plain.mp4");
    pattern = temp.filePath("pattern.mp4");
    QVERIFY(makeClip("color=c=white:size=320x240:rate=25", plain));
    QVERIFY(makeClip("testsrc2=size=320x240:rate=25", pattern));
  }

  void rejectedExportSignalsFailureAndKeepsMarks() {
    Video video;
    openRedacted(video, "rejected-export");
    QSignalSpy failed(&video, &Video::exportFailed);
    QSignalSpy saved(&video, &Video::exported);
    const auto marks = video.marks()->annotations();
    video.exportEdited(0, 0.01, false, {});
    QCOMPARE(failed.count(), 1);
    QCOMPARE(saved.count(), 0);
    QCOMPARE(video.marks()->annotations(), marks);
    QVERIFY(!video.busy());
    QFile blocked(temp.filePath("blocked-folder"));
    QVERIFY(blocked.open(QIODevice::WriteOnly));
    blocked.close();
    video.setOutputDirectory(QUrl::fromLocalFile(blocked.fileName() + "/clips"));
    video.exportEdited(0, video.duration(), false, {});
    QCOMPARE(failed.count(), 2);
    QCOMPARE(saved.count(), 0);
    QCOMPARE(video.marks()->annotations(), marks);
    QVERIFY(!video.busy());
  }
  void encoderFailureSignalsFailureAndKeepsMarks() {
    const QString source = temp.filePath("removed-source.mp4");
    QVERIFY(QFile::copy(plain, source));
    Video video;
    video.setOutputDirectory(QUrl::fromLocalFile(temp.filePath("failed-encode")));
    video.open(QUrl::fromLocalFile(source));
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 12000);
    video.marks()->edit("redact", 0.25, 0.25, 0.5, 0.5);
    const auto marks = video.marks()->annotations();
    QVERIFY(QFile::remove(source));
    QSignalSpy failed(&video, &Video::exportFailed);
    QSignalSpy saved(&video, &Video::exported);
    video.exportEdited(0, video.duration(), false, {});
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 12000);
    QCOMPARE(saved.count(), 0);
    QVERIFY(!video.busy() && video.savedPath().isEmpty());
    QCOMPARE(video.marks()->annotations(), marks);
  }
  void cancelledExportSignalsFailureAndKeepsMarks() {
    Video video;
    openRedacted(video, "cancelled-export");
    const auto marks = video.marks()->annotations();
    QSignalSpy failed(&video, &Video::exportFailed);
    QSignalSpy saved(&video, &Video::exported);
    video.exportEdited(0, video.duration(), false, {});
    video.cancel();
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 12000);
    QCOMPARE(saved.count(), 0);
    QVERIFY(!video.busy() && video.savedPath().isEmpty());
    QCOMPARE(video.marks()->annotations(), marks);
  }
  void noMarksGiveNoFilters() {
    QVERIFY(videoMarkFilters({}, {320, 240}, 0, "[0:v:0]", "[out]").isEmpty());
    // Arrows and the like come in as pictures, so without one there is
    // nothing to lay over the video.
    QVERIFY(videoMarkFilters({mark("arrow", 0, 4)}, {320, 240}, 0, "[0:v:0]",
                             "[out]").isEmpty());
    QVERIFY(videoMarkFilters({mark("redact", 0, 4)}, {}, 0, "[0:v:0]", "[out]")
                .isEmpty());
  }
  void redactionCoversEvenPixelsWhileItShows() {
    Frame::Edit edit = mark("redact", 1, 2);
    edit.from = {0.101, 0.201};
    edit.to = {0.499, 0.603};
    const auto filters =
        videoMarkFilters({edit}, {321, 241}, 0, "[0:v:0]", "[out]");
    QCOMPARE(filters, QStringList{
        "[0:v:0]drawbox=x=32:y=48:w=130:h=98:color=0x151a20@1:t=fill"
        ":enable='between(t,1.000,2.000)'[out]"});
  }
  void blurSoftensACropAndPutsItBack() {
    const auto filters = videoMarkFilters({mark("blur", 0, -1)}, {320, 240}, 0,
                                          "[0:v:0]", "[out]");
    QCOMPARE(filters, (QStringList{
        "[0:v:0]split[mark0base][mark0area]",
        "[mark0area]crop=80:60:80:60,gblur=sigma=24[mark0soft]",
        "[mark0base][mark0soft]overlay=80:60:enable='gte(t,0.000)'[out]"}));
  }
  void marksChainInOrderAndFollowTheInputStart() {
    const auto filters = videoMarkFilters(
        {mark("redact", 0, 1), mark("blur", 1.5, 3), mark("redact", 2, 4)},
        {320, 240}, 1.25, "[0:v:0]", "[out]");
    // The first mark ends before the input starts, so it is left out.
    QCOMPARE(filters.size(), 4);
    QVERIFY(filters[0].startsWith("[0:v:0]split[mark0base]"));
    QVERIFY(filters[2].endsWith("enable='between(t,0.250,1.750)'[mark0]"));
    QVERIFY(filters[3].startsWith("[mark0]drawbox="));
    QVERIFY(filters[3].endsWith("enable='between(t,0.750,2.750)'[out]"));
  }

  void picturesGoOverHiddenAreas() {
    VideoOverlay overlay;
    overlay.input = 1;
    overlay.area = QRect(40, 30, 50, 20);
    overlay.start = 2;
    overlay.end = -1;
    const auto filters = videoMarkFilters({mark("arrow", 0, 4), mark("redact", 0, 4)},
                                          {320, 240}, 0.5, "[0:v:0]", "[out]",
                                          {overlay});
    QCOMPARE(filters.size(), 2);
    QVERIFY(filters[0].startsWith("[0:v:0]drawbox="));
    QCOMPARE(filters[1], QString("[mark0][1:v]overlay=40:30:enable='gte(t,1.500)'[out]"));
  }
  void aMarkDrawnAloneMatchesTheScreenshot() {
    const QSize size(320, 240);
    Frame::Edit first{"step", {0.2, 0.2}, {0.2, 0.2}};
    Frame::Edit second{"step", {0.6, 0.5}, {0.6, 0.5}};
    const QVector<Frame::Edit> edits{first, mark("blur", 0, 4), second};
    QRect area;
    const QImage alone = renderVideoMark(edits, 2, size, &area);
    QVERIFY(!alone.isNull());
    QVERIFY(QRect(QPoint(), size).contains(area));
    QVERIFY(area.contains(QPoint(192, 120)));
    // The second step still shows 2, as it would on a screenshot.
    QImage clear(size, QImage::Format_ARGB32_Premultiplied);
    clear.fill(Qt::transparent);
    const QImage together = Frame::applyEdits(clear, {first, second}, false);
    QCOMPARE(alone, together.copy(area));
    QVERIFY(renderVideoMark(edits, 1, size, &area).isNull());
  }
  void newMarksOnAVideoGetTimes() {
    MarkDocument marks;
    QImage frame(320, 240, QImage::Format_ARGB32_Premultiplied);
    frame.fill(Qt::transparent);
    marks.reset(frame);
    marks.setDuration(8);
    marks.setPlayhead(3);
    marks.edit("blur", 0.1, 0.1, 0.3, 0.3);
    QCOMPARE(marks.selectedAnnotation().value("start").toDouble(), 0.);
    QCOMPARE(marks.selectedAnnotation().value("end").toDouble(), 8.);
    marks.edit("arrow", 0.5, 0.5, 0.8, 0.8);
    QCOMPARE(marks.selectedAnnotation().value("start").toDouble(), 3.);
    QCOMPARE(marks.selectedAnnotation().value("end").toDouble(), 8.);
    marks.setSelectedTimes(4, 20);
    QCOMPARE(marks.selectedAnnotation().value("start").toDouble(), 4.);
    QCOMPARE(marks.selectedAnnotation().value("end").toDouble(), 8.);
    // A mark that is not showing at the playhead cannot be picked up.
    QVERIFY(!marks.hitAt(0.65, 0.65).contains("index"));
    marks.setPlayhead(5);
    QVERIFY(marks.hitAt(0.65, 0.65).contains("index"));
    marks.undo();
    QCOMPARE(marks.selectedAnnotation().value("start").toDouble(), 3.);
    // Drawn on the last frame, a mark still lasts a tenth of a second, and a
    // mark that runs to the end can be picked up there.
    marks.setPlayhead(8);
    marks.edit("box", 0.4, 0.1, 0.6, 0.3);
    QCOMPARE(marks.selectedAnnotation().value("start").toDouble(), 7.9);
    QVERIFY(marks.hitAt(0.4, 0.2).contains("index"));
    QVERIFY(marks.hitAt(0.1, 0.2).contains("index"));
    // Screenshots ignore times.
    MarkDocument still;
    still.reset(frame);
    still.edit("blur", 0.1, 0.1, 0.3, 0.3);
    QCOMPARE(still.selectedAnnotation().value("end").toDouble(), 0.);
    still.setSelectedTimes(1, 2);
    QCOMPARE(still.selectedAnnotation().value("start").toDouble(), 0.);
  }

  void rewordingALabelChangesTheMarks() {
    // The review compares the marks to what was exported. New words on a
    // label that did not move are still a change.
    MarkDocument marks;
    QImage frame(320, 240, QImage::Format_ARGB32_Premultiplied);
    frame.fill(Qt::transparent);
    marks.reset(frame);
    marks.setDuration(8);
    marks.edit("text", 0.3, 0.3, 0.3, 0.3, "Wrong");
    const QVariantList exported = marks.annotations();
    marks.beginTextEdit();
    marks.endTextEdit("Right", true);
    QVERIFY(marks.annotations() != exported);
    marks.undo();
    QCOMPARE(marks.annotations(), exported);
  }

  void trimmedExportRedactsOnlyWhileTheMarkShows() {
    Video video;
    openRedacted(video, "trimmed");
    video.exportClip(0.5, 3.5, true);
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 20000);
    QVERIFY2(!video.savedPath().isEmpty(), qPrintable(video.status()));
    const QImage before = frameAt(video.savedPath(), 0.25, {320, 240});
    const QImage during = frameAt(video.savedPath(), 1.0, {320, 240});
    const QImage after = frameAt(video.savedPath(), 1.75, {320, 240});
    QVERIFY(!before.isNull() && !during.isNull() && !after.isNull());
    QVERIFY(white(before, 120, 90));
    QVERIFY(redacted(during, 120, 90));
    QVERIFY(redacted(during, 81, 61));
    QVERIFY(white(during, 200, 200));
    QVERIFY(white(after, 120, 90));
  }
  void exportWithACutRedactsInRecordingTime() {
    Video video;
    openRedacted(video, "cut");
    video.exportEdited(0, 4, true, {QVariantMap{{"start", 0.2}, {"end", 0.6}}});
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 20000);
    QVERIFY2(!video.savedPath().isEmpty(), qPrintable(video.status()));
    // 0.4 seconds were removed before the mark, so it shows from 0.6 to 1.6.
    QVERIFY(white(frameAt(video.savedPath(), 0.4, {320, 240}), 120, 90));
    QVERIFY(redacted(frameAt(video.savedPath(), 1.1, {320, 240}), 120, 90));
    QVERIFY(white(frameAt(video.savedPath(), 1.9, {320, 240}), 120, 90));
  }
  void marksStopTheUnchangedCopy() {
    Video video;
    openRedacted(video, "whole");
    video.exportClip(0, 4, true);
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 20000);
    QVERIFY2(!video.savedPath().isEmpty(), qPrintable(video.status()));
    QVERIFY(redacted(frameAt(video.savedPath(), 1.5, {320, 240}), 120, 90));
  }
  void movingOneMarkRedrawsOnlyThatOne() {
    Video video;
    video.open(QUrl::fromLocalFile(plain));
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 12000);
    video.marks()->edit("box", 0.1, 0.1, 0.3, 0.3);
    video.marks()->edit("arrow", 0.5, 0.5, 0.8, 0.8);
    const auto before = video.overlays();
    QCOMPARE(before.size(), 2);
    video.marks()->nudgeSelected(4, 0);
    const auto after = video.overlays();
    QCOMPARE(after.size(), 2);
    const auto source = [](const QVariant &mark) {
      return mark.toMap().value("source").toString();
    };
    QCOMPARE(source(after[0]), source(before[0]));
    QVERIFY(source(after[1]) != source(before[1]));
    // Back where it was, it is the same picture again.
    video.marks()->undo();
    QCOMPARE(source(video.overlays()[1]), source(before[1]));
  }
  void exportLaysPicturesOverTheVideo() {
    Video video;
    video.setOutputDirectory(QUrl::fromLocalFile(temp.filePath("box")));
    video.open(QUrl::fromLocalFile(plain));
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 12000);
    video.marks()->setPlayhead(1);
    video.marks()->edit("box", 0.25, 0.25, 0.5, 0.5);
    video.marks()->setSelectedTimes(1, 3);
    QCOMPARE(video.overlays().size(), 1);
    video.exportEdited(0.5, 4, true, {QVariantMap{{"start", 2.5}, {"end", 2.9}}});
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 20000);
    QVERIFY2(!video.savedPath().isEmpty(), qPrintable(video.status()));
    // The box's thin left edge runs down near x = 80, in the mark's colour.
    const auto boxed = [](const QImage &frame) {
      for (int x = 76; x <= 84; ++x) {
        const QColor c = frame.pixelColor(x, 90);
        if (c.red() > 170 && c.green() < 150)
          return true;
      }
      return false;
    };
    QVERIFY(!boxed(frameAt(video.savedPath(), 0.2, {320, 240})));
    QVERIFY(boxed(frameAt(video.savedPath(), 1.0, {320, 240})));
    // 2.5 to 2.9 was cut, so 3.0 in the recording is 2.1 in the clip.
    QVERIFY(boxed(frameAt(video.savedPath(), 1.9, {320, 240})));
    QVERIFY(!boxed(frameAt(video.savedPath(), 2.4, {320, 240})));
    QVERIFY(white(frameAt(video.savedPath(), 1.0, {320, 240}), 120, 90));
  }
  void marksOnARotatedVideoLandOnTheUprightPicture() {
    const QString rotated = temp.filePath("rotated.mp4");
    QProcess ffmpeg;
    ffmpeg.start("ffmpeg", {"-hide_banner", "-loglevel", "error", "-display_rotation",
                            "90", "-i", plain, "-c", "copy", rotated});
    QVERIFY(ffmpeg.waitForFinished(10000));
    QCOMPARE(ffmpeg.exitCode(), 0);
    Video video;
    video.setOutputDirectory(QUrl::fromLocalFile(temp.filePath("rotated-clips")));
    video.open(QUrl::fromLocalFile(rotated));
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 12000);
    QCOMPARE(video.frameSize(), QSize(240, 320));
    video.marks()->edit("redact", 0.5, 0.25, 1, 0.5);
    video.exportClip(0.5, 4, true);
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 20000);
    QVERIFY2(!video.savedPath().isEmpty(), qPrintable(video.status()));
    const QImage frame = frameAt(video.savedPath(), 1, {240, 320});
    QVERIFY(!frame.isNull());
    QVERIFY(redacted(frame, 200, 120));
    QVERIFY(white(frame, 60, 120));
    QVERIFY(white(frame, 200, 250));
  }
  void exportedBlurIsSofterThanTheRecording() {
    Video video;
    video.setOutputDirectory(QUrl::fromLocalFile(temp.filePath("blur")));
    video.open(QUrl::fromLocalFile(pattern));
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 12000);
    video.marks()->edit("blur", 0.25, 0.25, 0.75, 0.75);
    video.marks()->setSelectedTimes(0, 2);
    video.exportClip(0.5, 4, true);
    QTRY_VERIFY_WITH_TIMEOUT(!video.busy(), 20000);
    QVERIFY2(!video.savedPath().isEmpty(), qPrintable(video.status()));
    const QRect area(90, 70, 140, 100);
    const QImage original = frameAt(pattern, 1.2, {320, 240});
    const QImage blurred = frameAt(video.savedPath(), 0.7, {320, 240});
    QVERIFY(!original.isNull() && !blurred.isNull());
    QVERIFY2(detail(blurred, area) < detail(original, area) * 0.35,
             qPrintable(QString("%1 vs %2").arg(detail(blurred, area))
                            .arg(detail(original, area))));
    // Once the mark ends, the picture is sharp again.
    const QImage laterOriginal = frameAt(pattern, 3, {320, 240});
    const QImage later = frameAt(video.savedPath(), 2.5, {320, 240});
    QVERIFY(detail(later, area) > detail(laterOriginal, area) * 0.8);
  }
};

QTEST_MAIN(VideoMarksTest)
#include "video-marks-test.moc"
