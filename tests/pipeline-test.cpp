#include "capture-session.hpp"
#include "edit-json.hpp"
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
#include <QFontDatabase>
#include <QPainter>
#include <QProcess>
#include <QScreen>
#include <QSettings>
#include <QSignalSpy>
#include <QScopeGuard>
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
    studio.marks()->edit("crop", 0.25, 0, 0.75, 1);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.dimensions(), QString("200 × 200"));
    studio.marks()->undo();
    QTRY_VERIFY(!studio.rendering());
    QCOMPARE(studio.dimensions(), QString("400 × 200"));
    studio.marks()->redo();
    QTRY_VERIFY(!studio.rendering());
    QCOMPARE(studio.dimensions(), QString("200 × 200"));
    studio.marks()->edit("redact", 0.5, 0, 1, 1);
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
    studio.marks()->edit("text", 0.2, 0.2, 0.2, 0.2, "Draft");
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.marks()->selectedAnnotation().value("fontPx").toInt(), studio.marks()->newTextPixels());
    // While a label is being typed its rendered copy is hidden, and the
    // typed words are applied even if a preview is still rendering.
    studio.marks()->beginTextEdit();
    QVERIFY(studio.marks()->textEditing());
    studio.marks()->endTextEdit("Final words", true);
    QVERIFY(!studio.marks()->textEditing());
    QCOMPARE(studio.marks()->selectedAnnotation().value("text").toString(), QString("Final words"));
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    studio.marks()->beginTextEdit();
    studio.marks()->endTextEdit("Ignored", false);
    QCOMPARE(studio.marks()->selectedAnnotation().value("text").toString(), QString("Final words"));
    studio.marks()->undo();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.marks()->selectedAnnotation().value("text").toString(), QString("Draft"));
    studio.marks()->beginTextEdit();
    studio.marks()->endTextEdit("   ", true);
    QVERIFY(studio.marks()->selectedAnnotation().isEmpty());
    QVERIFY(studio.status().contains("Empty label removed"));
    // A drawing tool can start a new mark inside a highlight; its border
    // still picks the highlight up.
    studio.marks()->edit("highlight", 0.4, 0.4, 0.8, 0.8);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.marks()->hitAt(0.6, 0.6).contains("index"));
    QVERIFY(!studio.marks()->hitAt(0.6, 0.6, true).contains("index"));
    QVERIFY(studio.marks()->hitAt(0.4, 0.6, true).contains("index"));
    studio.marks()->clearSelection();
    QVERIFY(studio.marks()->selectedAnnotation().isEmpty());
    studio.marks()->select(studio.marks()->hitAt(0.4, 0.6).value("index").toInt());
    QCOMPARE(studio.marks()->selectedAnnotation().value("type").toString(), QString("highlight"));
    // Finish thumbnails wait until the editor closes.
    studio.setEditing(false);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    studio.closeImage();
    QVERIFY(!studio.hasImage());
    QVERIFY(studio.marks()->selectedAnnotation().isEmpty());
  }
  void sideHandlesKeepTextReadableAndPersistBox() {
    QImage source(1000, 800, QImage::Format_ARGB32_Premultiplied);
    source.fill(Qt::white);
    MarkDocument doc;
    doc.reset(source);
    doc.edit("text", .1, .1, .1, .1, "A clear label with several words to wrap");
    const auto original = doc.edits().first();
    const int font = Frame::textPixelSize(original, source);
    doc.resizeSelected(5, .4, .9); // Right edge changes width, not font or top.
    const auto narrow = doc.edits().first();
    QCOMPARE(Frame::textPixelSize(narrow, source), font);
    QCOMPARE(narrow.from, original.from);
    QVERIFY(qAbs(narrow.textBox.width() - .3) < 1e-8);
    const auto narrowBounds = Frame::annotationBounds(narrow, source);
    QVERIFY(narrowBounds.height() > Frame::annotationBounds(original, source).height());
    doc.resizeSelected(6, .9, .7);
    QCOMPARE(doc.edits().first().textBox.width(), narrow.textBox.width());
    QCOMPARE(Frame::textPixelSize(doc.edits().first(), source), font);
    QVERIFY(qAbs(Frame::annotationBounds(doc.edits().first(), source).height() - .6) < 1e-8);
    const auto saved = Frame::editFromJson(Frame::editToJson(doc.edits().first()));
    QVERIFY(saved.has_value());
    QCOMPARE(saved->textBox, doc.edits().first().textBox);
    QCOMPARE(Frame::applyEdits(source, {*saved}), Frame::applyEdits(source, doc.edits()));
    // Older drafts retain automatic sizing.
    auto legacy = Frame::editToJson(original);
    legacy.remove("textBoxWidth"); legacy.remove("textBoxHeight");
    QCOMPARE(Frame::editFromJson(legacy)->textBox, QSizeF(0, 0));
    doc.resizeSelected(4, .5, .2);
    const auto raised = Frame::annotationBounds(doc.edits().first(), source);
    QVERIFY(qAbs(raised.bottom() - .7) < 1e-8);
    doc.resizeSelected(7, .05, .5);
    const auto left = Frame::annotationBounds(doc.edits().first(), source);
    QVERIFY(qAbs(left.right() - .4) < 1e-8);
    MarkDocument longLabel;
    longLabel.reset(source);
    longLabel.edit("text", .1, .1, .1, .1, QString("Readable words ").repeated(16));
    const int longFont = Frame::textPixelSize(longLabel.edits().first(), source);
    longLabel.resizeSelected(5, .12, .5);
    QCOMPARE(Frame::textPixelSize(longLabel.edits().first(), source), longFont);
    QVERIFY(Frame::annotationBounds(longLabel.edits().first(), source).height() <= 1.);
    // A crop maps the side handle back to source coordinates.
    doc.edit("crop", 0, 0, .8, .8);
    doc.select(0);
    doc.resizeSelected(5, .75, .5);
    QVERIFY(qAbs(Frame::annotationBounds(doc.edits().first(), source).right() - .6) < 1e-8);
  }
  void allAreaToolsAndPenResizeFromSides() {
    QImage source(1000, 800, QImage::Format_ARGB32_Premultiplied);
    for (const QString type : {"box", "ellipse", "highlight", "redact", "blur", "pen"}) {
      MarkDocument doc;
      doc.reset(source);
      if (type == "pen")
        doc.addStroke({QVariantMap{{"x", .2}, {"y", .2}}, QVariantMap{{"x", .4}, {"y", .4}}});
      else doc.edit(type, .2, .2, .4, .4);
      doc.resizeSelected(5, .7, .9);
      auto bounds = Frame::annotationBounds(doc.edits().first(), source);
      QVERIFY(qAbs(bounds.right() - .7) < 1e-8);
      QVERIFY(qAbs(bounds.top() - .2) < 1e-8);
      QVERIFY(qAbs(bounds.bottom() - .4) < 1e-8);
      doc.resizeSelected(4, .9, .1);
      doc.resizeSelected(6, .9, .6);
      doc.resizeSelected(7, .1, .9);
      bounds = Frame::annotationBounds(doc.edits().first(), source);
      QCOMPARE(bounds, QRectF(.1, .1, .6, .5));
    }
    MarkDocument doc;
    doc.reset(source);
    doc.edit("step", .5, .5, .5, .5);
    doc.resizeSelected(5, .6, .5);
    const auto step = Frame::annotationBounds(doc.edits().first(), source);
    QCOMPARE(step.center(), QPointF(.5, .5));
    QVERIFY(qAbs(step.width() * 1000 - step.height() * 800) < 1e-6);
  }
  void dragPreviewHasOneUndoAndCanCancel() {
    QImage source(1000, 800, QImage::Format_ARGB32_Premultiplied);
    MarkDocument doc;
    doc.reset(source);
    doc.edit("box", .1, .1, .3, .3);
    const auto original = doc.edits();
    QSignalSpy edited(&doc, &MarkDocument::edited);
    doc.beginTransform();
    doc.previewTransform(-1, .1, .2);
    doc.previewTransform(-1, .2, .3);
    QVERIFY(qAbs(doc.edits().first().from.x() - .3) < 1e-8);
    QVERIFY(!edited.last().first().toBool());
    doc.previewTransform(-1, 0, 0);
    QVERIFY(doc.edits() == original);
    doc.previewTransform(5, .8, .1);
    doc.endTransform(false);
    QVERIFY(doc.edits() == original);
    QVERIFY(!doc.canRedo());
    doc.beginTransform();
    doc.previewTransform(5, .7, .1);
    doc.previewTransform(5, .8, .1);
    doc.endTransform(true);
    QVERIFY(edited.last().first().toBool());
    doc.undo();
    QVERIFY(doc.edits() == original);
    doc.redo();
    QVERIFY(qAbs(doc.edits().first().to.x() - .8) < 1e-8);
  }
  void annotationsCanMoveResizeDeleteAndReframe() {
    ImageStore store;
    Studio studio(&store);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    studio.marks()->edit("box", 0.1, 0.1, 0.3, 0.3);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.marks()->selectAt(0.2, 0.2) >= 0);
    studio.marks()->moveSelected(0.2, 0.1);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(qAbs(studio.marks()->selectedAnnotation().value("x1").toDouble() - 0.3) < 1e-8);
    studio.marks()->undo();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.marks()->selectAt(0.2, 0.2) >= 0);
    QVERIFY(qAbs(studio.marks()->selectedAnnotation().value("x1").toDouble() - 0.1) < 1e-8);
    studio.marks()->redo();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.marks()->selectAt(0.4, 0.3) >= 0);
    studio.marks()->resizeSelected(2, 0.65, 0.55);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(qAbs(studio.marks()->selectedAnnotation().value("x2").toDouble() - 0.65) < 1e-8);
    studio.marks()->edit("crop", 0.25, 0.0, 0.75, 1.0);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.marks()->hasCrop());
    QCOMPARE(studio.marks()->selectedAnnotation().size(), 0);
    QVERIFY(studio.marks()->selectAt(0.5, 0.35) >= 0);
    studio.marks()->deleteSelected();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.marks()->selectAt(0.5, 0.35), -1);
    studio.marks()->undo();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.marks()->selectAt(0.5, 0.35) >= 0);
    studio.marks()->clearCrop();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(!studio.marks()->hasCrop());
    QVERIFY(studio.marks()->selectAt(0.5, 0.35) >= 0);
  }
  void annotationsCanDuplicateReorderAndKeepSelectionThroughHistory() {
    ImageStore store;
    Studio studio(&store);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    studio.marks()->edit("box", 0.1, 0.1, 0.4, 0.4);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    studio.marks()->edit("box", 0.3, 0.3, 0.6, 0.6);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.marks()->selectAt(0.2, 0.2) >= 0);
    QCOMPARE(studio.marks()->selectedAnnotation().value("layer").toInt(), 1);
    studio.marks()->moveSelectedLayer(1);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.marks()->selectedAnnotation().value("layer").toInt(), 2);
    QVERIFY(studio.marks()->selectAt(0.35, 0.35) >= 0);
    QCOMPARE(studio.marks()->selectedAnnotation().value("x1").toDouble(), 0.1);
    studio.marks()->undo();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.marks()->selectedAnnotation().value("layer").toInt(), 1);
    QVERIFY(studio.marks()->selectAt(0.35, 0.35) >= 0);
    QCOMPARE(studio.marks()->selectedAnnotation().value("x1").toDouble(), 0.3);
    studio.marks()->redo();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.marks()->selectedAnnotation().value("layer").toInt(), 2);
    studio.marks()->duplicateSelected();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.marks()->selectedAnnotation().value("layers").toInt(), 3);
    QVERIFY(studio.marks()->selectedAnnotation().value("x1").toDouble() > 0.1);
    studio.marks()->deleteSelected();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.marks()->selectedAnnotation().isEmpty());
    studio.marks()->undo();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.marks()->selectedAnnotation().value("layers").toInt(), 3);
    QCOMPARE(studio.marks()->selectedAnnotation().value("layer").toInt(), 3);
  }
  void textCanBeSelectedEditedMovedAndUndone() {
    ImageStore store;
    Studio studio(&store);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    studio.marks()->edit("text", 0.2, 0.2, 0.2, 0.2, "First label");
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    auto selected = studio.marks()->selectedAnnotation();
    const double initialSize = selected.value("size").toDouble();
    const double beforeWidth = selected.value("boundW").toDouble();
    const double hitX = selected.value("boundX").toDouble() +
                        selected.value("boundW").toDouble() * 0.85;
    const double hitY = selected.value("boundY").toDouble() +
                        selected.value("boundH").toDouble() * 0.5;
    QVERIFY(studio.marks()->selectAt(hitX, hitY) >= 0);
    QCOMPARE(studio.marks()->selectedAnnotation().value("text").toString(),
             QString("First label"));
    studio.marks()->updateSelectedText("A longer revised label");
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    selected = studio.marks()->selectedAnnotation();
    QCOMPARE(selected.value("text").toString(), QString("A longer revised label"));
    QVERIFY(selected.value("boundW").toDouble() > beforeWidth);
    QVERIFY(studio.marks()->selectAt(selected.value("boundX").toDouble() +
                                selected.value("boundW").toDouble() * 0.9,
                            selected.value("boundY").toDouble() +
                                selected.value("boundH").toDouble() * 0.5) >= 0);
    studio.marks()->moveSelected(0.1, 0.1);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    selected = studio.marks()->selectedAnnotation();
    QVERIFY(qAbs(selected.value("x1").toDouble() - 0.3) < 1e-8);
    const double movedWidth = selected.value("boundW").toDouble();
    studio.marks()->resizeSelected(2, selected.value("boundX").toDouble() + movedWidth * 1.5,
                          selected.value("boundY").toDouble() +
                              selected.value("boundH").toDouble() * 1.5);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.marks()->selectedAnnotation().value("size").toDouble() > 1.3);
    QVERIFY(studio.marks()->selectedAnnotation().value("boundW").toDouble() > movedWidth);
    studio.marks()->setSelectedColor("#459ec7");
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.marks()->selectedAnnotation().value("color").toString(),
             QString("#459ec7"));
    studio.marks()->undo();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.marks()->selectAt(0.31, 0.31) >= 0);
    QCOMPARE(studio.marks()->selectedAnnotation().value("color").toString(),
             QString("#ffffff"));
    studio.marks()->undo();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.marks()->selectAt(0.31, 0.31) >= 0);
    QVERIFY(qAbs(studio.marks()->selectedAnnotation().value("size").toDouble() - initialSize) < 1e-8);
    studio.marks()->undo();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.marks()->selectAt(hitX, hitY) >= 0);
    QCOMPARE(studio.marks()->selectedAnnotation().value("text").toString(),
             QString("A longer revised label"));
    studio.marks()->undo();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.marks()->selectAt(hitX, hitY) >= 0);
    QCOMPARE(studio.marks()->selectedAnnotation().value("text").toString(),
             QString("First label"));
  }
  void largeTextHasDirectSizeAndEditableStyles() {
    ImageStore store;
    Studio studio(&store);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    studio.marks()->edit("text", 0.1, 0.2, 0.1, 0.2, "Go");
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    auto mark = studio.marks()->selectedAnnotation();
    const double right = mark.value("boundX").toDouble() + mark.value("boundW").toDouble();
    const double bottom = mark.value("boundY").toDouble() + mark.value("boundH").toDouble();
    studio.marks()->resizeSelected(0, mark.value("boundX").toDouble() -
                                 mark.value("boundW").toDouble(),
                          mark.value("boundY").toDouble() -
                                 mark.value("boundH").toDouble());
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    mark = studio.marks()->selectedAnnotation();
    QVERIFY(mark.value("fontPx").toInt() > 30);
    QVERIFY(qAbs(mark.value("boundX").toDouble() +
                 mark.value("boundW").toDouble() - right) < 0.02);
    QVERIFY(qAbs(mark.value("boundY").toDouble() +
                 mark.value("boundH").toDouble() - bottom) < 0.02);
    studio.marks()->setSelectedFontPixels(192);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.marks()->selectedAnnotation().value("fontPx").toInt(), 192);
    studio.marks()->setSelectedTextStyle("shadow");
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.marks()->selectedAnnotation().value("textStyle").toString(),
             QString("shadow"));
    studio.marks()->setSelectedTextStyle("box");
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    studio.marks()->setSelectedTextAlignment("left");
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.marks()->selectedAnnotation().value("textAlign").toString(),
             QString("left"));
    studio.marks()->setSelectedBackground("#101044");
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    studio.marks()->setSelectedBackgroundOpacity(0.5);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.marks()->selectedAnnotation().value("background").toString(),
             QString("#101044"));
    QCOMPARE(studio.marks()->selectedAnnotation().value("backgroundOpacity").toDouble(),
             0.5);
    studio.marks()->setSelectedFontPixels(999);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.marks()->selectedAnnotation().value("fontPx").toInt() > 300);
    QVERIFY(studio.marks()->selectedAnnotation().value("fontPx").toInt() <= 4096);
    studio.marks()->updateSelectedText("A longer label that should wrap instead of running beyond the image");
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    mark = studio.marks()->selectedAnnotation();
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
    studio.marks()->edit("step", 0.4, 0.4, 0.4, 0.4);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    const double originalX = studio.marks()->selectedAnnotation().value("x1").toDouble();
    studio.marks()->nudgeSelected(10, 0);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.marks()->selectedAnnotation().value("x1").toDouble() > originalX);
    const auto before = studio.marks()->selectedAnnotation();
    studio.marks()->resizeSelected(2, before.value("boundX").toDouble() +
                                 before.value("boundW").toDouble() * 1.5,
                          before.value("boundY").toDouble() +
                                 before.value("boundH").toDouble() * 1.5);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.marks()->selectedAnnotation().value("size").toDouble() > 1.0);
    studio.marks()->setSelectedColor("#4ca782");
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.marks()->selectedAnnotation().value("color").toString(),
             QString("#4ca782"));
    studio.marks()->deleteSelected();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.marks()->selectedAnnotation().isEmpty());
    studio.marks()->undo();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.marks()->selectAt(0.4, 0.4) >= 0);
  }
  void freehandStrokeCanBeSelectedMovedResizedAndUndone() {
    ImageStore store;
    Studio studio(&store);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    studio.marks()->addStroke({QVariantMap{{"x", 0.2}, {"y", 0.2}},
                      QVariantMap{{"x", 0.4}, {"y", 0.4}},
                      QVariantMap{{"x", 0.6}, {"y", 0.2}}});
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.marks()->selectedAnnotation().value("type").toString(), QString("pen"));
    QVERIFY(studio.marks()->selectAt(0.4, 0.4) >= 0);
    studio.marks()->moveSelected(0.1, 0.1);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(qAbs(studio.marks()->selectedAnnotation().value("boundX").toDouble() - 0.3) < 1e-8);
    studio.marks()->resizeSelected(2, 0.8, 0.6);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.marks()->selectedAnnotation().value("boundW").toDouble() > 0.4);
    studio.marks()->setSelectedColor("#459ec7");
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.marks()->selectedAnnotation().value("color").toString(), QString("#459ec7"));
    studio.marks()->deleteSelected();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    studio.marks()->undo();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.marks()->selectAt(0.55, 0.6) >= 0);
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
    // Returning to the unchanged source must copy it, while the previous
    // edited export remains available if the user redoes those edits.
    const QString saved = video.savedPath();
    QVERIFY(video.copyFile(true));
    paste.start("wl-paste", {"--type", "text/uri-list"});
    QVERIFY(paste.waitForFinished(3000));
    QCOMPARE(QString::fromUtf8(paste.readAllStandardOutput()).trimmed(),
             QUrl::fromLocalFile(source).toString());
    QCOMPARE(video.savedPath(), saved);
    QVERIFY(video.copyFile());
    paste.start("wl-paste", {"--type", "text/uri-list"});
    QVERIFY(paste.waitForFinished(3000));
    QCOMPARE(QString::fromUtf8(paste.readAllStandardOutput()).trimmed(),
             QUrl::fromLocalFile(saved).toString());
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
    first.marks()->edit("text", 0.25, 0.3, 0.25, 0.3, "Editable again");
    QTRY_VERIFY_WITH_TIMEOUT(!first.rendering(), 8000);
    first.marks()->setSelectedColor("#459ec7");
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
    first.marks()->updateSelectedText("Latest edit");
    QTRY_VERIFY_WITH_TIMEOUT(!first.rendering(), 8000);
    first.resumeDraft(id);
    QTRY_VERIFY_WITH_TIMEOUT(!first.rendering(), 8000);
    QCOMPARE(first.marks()->selectedAnnotation().value("text").toString(),
             QString("Latest edit"));

    ImageStore reopenedStore;
    Studio reopened(&reopenedStore, false);
    reopened.resumeDraft(id);
    QTRY_VERIFY_WITH_TIMEOUT(!reopened.rendering(), 8000);
    QCOMPARE(reopened.marks()->selectedAnnotation().value("text").toString(),
             QString("Latest edit"));
    QCOMPARE(reopened.marks()->selectedAnnotation().value("color").toString(),
             QString("#459ec7"));
    QCOMPARE(reopened.style(), 1);
    reopened.marks()->deleteSelected();
    QTRY_VERIFY_WITH_TIMEOUT(!reopened.rendering(), 8000);
    reopened.saveDraftNow();
    const QString secondInput = temp.filePath("another-draft.png");
    QVERIFY(source.save(secondInput));
    reopened.open(QUrl::fromLocalFile(secondInput));
    QTRY_VERIFY_WITH_TIMEOUT(!reopened.busy() && !reopened.rendering(), 8000);
    reopened.marks()->edit("step", 0.5, 0.5, 0.5, 0.5);
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
  void possibleSecretsCanBeHiddenAndTextCopied() {
    if (QStandardPaths::findExecutable("tesseract").isEmpty())
      QSKIP("tesseract is not installed");
    QImage source(900, 150, QImage::Format_RGB32);
    source.fill(QColor("#1e1e2e"));
    {
      QPainter painter(&source);
      QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
      font.setPixelSize(18);
      painter.setFont(font);
      painter.setPen(QColor("#cdd6f4"));
      painter.drawText(24, 50, "export TOKEN=ghp_R8x2KqLm4Vn7Pz9Wt3Ys6Bd1Fh5Jc0Ae2Gk");
      painter.drawText(24, 100, "Build finished in 4 seconds");
    }
    const QString path = temp.filePath("secret.png");
    QVERIFY(source.save(path));
    ImageStore store;
    Studio studio(&store, false);
    studio.open(QUrl::fromLocalFile(path));
    QTRY_VERIFY_WITH_TIMEOUT(!studio.busy(), 8000);
    QVERIFY(studio.canReadText());
    // Asked before the text is read: it waits, then copies.
    studio.copyText();
    QVERIFY(studio.textNote() == "Reading text…" ||
            studio.textNote() == "Copied the text.");
    QTRY_VERIFY_WITH_TIMEOUT(studio.textNote() == "Copied the text.", 20000);
    QProcess paste;
    paste.start("wl-paste", {"--no-newline"});
    QVERIFY(paste.waitForFinished(3000));
    QVERIFY(QString::fromUtf8(paste.readAllStandardOutput())
                .contains("Build finished in 4 seconds"));
    QCOMPARE(studio.secretCount(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 8000);
    studio.hideSecrets();
    QCOMPARE(studio.secretCount(), 0);
    QCOMPARE(studio.marks()->edits().size(), 1);
    QCOMPARE(studio.marks()->edits().first().type, QString("redact"));
    QVERIFY(studio.textNote().startsWith("Hid 1 possible secret."));
    studio.copyText();
    QTRY_COMPARE_WITH_TIMEOUT(studio.textNote(), QString("Copied the text."), 20000);
    paste.start("wl-paste", {"--no-newline"});
    QVERIFY(paste.waitForFinished(3000));
    const QString hiddenText = QString::fromUtf8(paste.readAllStandardOutput());
    QVERIFY(hiddenText.contains("Build finished in 4 seconds"));
    QVERIFY(!hiddenText.contains("ghp_"));
    // An ordinary mark: one undo brings the secret back.
    studio.marks()->undo();
    QCOMPARE(studio.secretCount(), 1);
    studio.marks()->redo();
    QCOMPARE(studio.secretCount(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 8000);
    QVERIFY(studio.marks()->cropCurrentView(0, 0.45, 1, 1));
    studio.copyText();
    QTRY_COMPARE_WITH_TIMEOUT(studio.textNote(), QString("Copied the text."), 20000);
    paste.start("wl-paste", {"--no-newline"});
    QVERIFY(paste.waitForFinished(3000));
    const QString croppedText = QString::fromUtf8(paste.readAllStandardOutput());
    QVERIFY(croppedText.contains("Build finished in 4 seconds"));
    QVERIFY(!croppedText.contains("TOKEN"));
    studio.marks()->undo();
    studio.saveDraftNow();
    // What was read never reaches the disk.
    const QDir drafts(QStandardPaths::writableLocation(
                          QStandardPaths::AppLocalDataLocation) + "/drafts");
    for (const QString &name : drafts.entryList({"*.json"}, QDir::Files)) {
      const QByteArray draft = contents(drafts.filePath(name));
      QVERIFY(!draft.contains("Build finished"));
      QVERIFY(!draft.contains("ghp_"));
    }
    studio.accept();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.busy(), 15000);
    const QImage saved(studio.savedPath());
    QVERIFY(!saved.isNull());
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
  void scrollSelectionStartsWhileTheSelectorOwnsTheBusyState() {
    ImageStore store;
    Studio studio(&store, false);
    QSignalSpy selections(&studio, &Studio::selectionReady);
    QSignalSpy ended(&studio, &Studio::scrollEnded);
    studio.captureScroll();
    QTRY_COMPARE_WITH_TIMEOUT(selections.count(), 1, 8000);
    QCOMPARE(studio.quickState(), QString("selecting"));
    QVERIFY(studio.busy());
    QVERIFY(studio.scrollSelection());
    const auto monitor = selections.first().first().toStringList().first();
    studio.finishScrollSelection(monitor, 0.1, 0.1, 0.8, 0.8, 0.5, 0.5);
    QCOMPARE(studio.quickState(), QString("scrolling"));
    QVERIFY(studio.scrollCapture()->active());
    studio.scrollCapture()->cancel();
    QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 8000);
    QVERIFY(!studio.busy());
    QCOMPARE(studio.quickState(), QString("cancelled"));
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
    studio.marks()->edit("redact", 0.1, 0.1, 0.4, 0.4);
    QTRY_VERIFY(!studio.rendering());
    QSize size;
    const auto edited = store.requestImage("source", &size, {});
    studio.showFinishes();
    studio.openEditor();
    QCOMPARE(store.requestImage("source", &size, {}), edited);
    QVERIFY(studio.marks()->canUndo());
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
  void failedQuickSaveKeepsCaptureForRetry_data() {
    QTest::addColumn<bool>("throughEditor");
    QTest::newRow("chooser") << false;
    QTest::newRow("editor") << true;
  }
  void failedQuickSaveKeepsCaptureForRetry() {
    QFETCH(bool, throughEditor);
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
    if (throughEditor) {
      studio.openEditor();
      QCOMPARE(studio.quickState(), QString("editing"));
    }
    studio.setOutputDirectory(QUrl::fromLocalFile(temp.filePath("retry")));
    if (throughEditor)
      studio.showFinishes();
    else {
      QCOMPARE(studio.quickState(), QString("choosing"));
      QVERIFY(studio.status().contains("Save folder changed"));
    }
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
  void toolStylesPersistUndoAndKeepRedactionsOpaque() {
    const auto cleanup = qScopeGuard([] { QSettings().remove("tools"); });
    QSettings().remove("tools");
    QImage source(1000, 800, QImage::Format_ARGB32_Premultiplied);
    source.fill(Qt::blue);
    MarkDocument doc;
    doc.reset(source);
    QSignalSpy edited(&doc, &MarkDocument::edited);
    doc.setToolStyle("arrow", {{"color", "#ffffff"}, {"size", 2.},
                                {"outline", true}, {"arrowHead", "filled"}});
    QVERIFY(!doc.canUndo());
    QCOMPARE(edited.count(), 0);
    doc.edit("arrow", .1, .1, .7, .3);
    const auto arrow = doc.edits().first();
    QVERIFY(arrow.outline);
    QCOMPARE(arrow.arrowHead, QString("filled"));
    const auto signature = doc.annotations();
    doc.setToolStyle("arrow", {{"outline", false}, {"arrowHead", "open"}});
    QVERIFY(doc.annotations() != signature);
    doc.undo();
    QVERIFY(doc.edits().first() == arrow);
    doc.redo();
    QVERIFY(!doc.edits().first().outline);
    const auto unchanged = doc.edits();
    doc.setToolStyle("arrow", {{"color", "#e75439"}, {"size", "invalid"}});
    QVERIFY(doc.edits() == unchanged);
    doc.setToolStyle("arrow", {{"arrowHead", "invalid"}});
    QVERIFY(doc.edits() == unchanged);
    doc.clearSelection();
    doc.setToolStyle("box", {{"filled", true}, {"background", "#ff0000"}, {"opacity", .5}});
    doc.edit("box", .2, .2, .8, .8);
    const auto box = doc.edits().last();
    const QColor mixed = Frame::applyEdits(source, {box}).pixelColor(500, 400);
    QVERIFY(qAbs(mixed.red()-128) <= 1 && qAbs(mixed.blue()-128) <= 1);
    doc.setToolStyle("box", {{"opacity", 0.}});
    QCOMPARE(Frame::applyEdits(source, {doc.edits().last()}).pixelColor(500, 400), QColor(Qt::blue));
    doc.clearSelection();
    doc.setToolStyle("highlight", {{"color", "#00ff00"}, {"opacity", .5}});
    doc.edit("highlight", .2, .2, .8, .8);
    const QColor highlighted = Frame::applyEdits(source, {doc.edits().last()}).pixelColor(500, 400);
    QVERIFY(qAbs(highlighted.green()-128) <= 1 && qAbs(highlighted.blue()-128) <= 1);
    doc.setToolStyle("step", {{"numberColor", "#151a20"}});
    doc.edit("step", .5, .5, .5, .5);
    QCOMPARE(doc.edits().last().numberColor, QColor("#151a20"));
    doc.setToolStyle("pen", {{"color", "#ffffff"}, {"outline", true}});
    doc.addStroke({QVariantMap{{"x", .1}, {"y", .1}}, QVariantMap{{"x", .8}, {"y", .8}}});
    QVERIFY(doc.edits().last().outline);
    QCOMPARE(doc.edits().last().color, QColor(Qt::white));
    doc.setToolStyle("blur", {{"size", 3.}});
    doc.edit("blur", .2, .2, .8, .8);
    QCOMPARE(doc.edits().last().size, 3.);
    doc.setToolStyle("ellipse", {{"filled", true}, {"opacity", .25}});
    doc.edit("ellipse", .2, .2, .8, .8);
    for (const auto &edit : doc.edits()) {
      const auto restored = Frame::editFromJson(Frame::editToJson(edit));
      QVERIFY(restored && *restored == edit);
    }
    QSettings().sync();
    MarkDocument reopened;
    reopened.reset(source);
    reopened.edit("box", .2, .2, .8, .8);
    QVERIFY(reopened.edits().first().filled);
    QCOMPARE(reopened.edits().first().opacity, 0.);
    reopened.edit("step", .5, .5, .5, .5);
    QCOMPARE(reopened.edits().last().numberColor, QColor("#151a20"));
    reopened.edit("redact", .2, .2, .8, .8);
    const auto redact = reopened.edits().last();
    reopened.setToolStyle("redact", {{"opacity", 0.}});
    QVERIFY(reopened.edits().last() == redact);
    QCOMPARE(Frame::applyEdits(source, {redact}).pixelColor(500, 400), QColor("#151a20"));
    auto legacy = Frame::editToJson(Frame::Edit{"highlight", {.2,.2}, {.8,.8}});
    legacy.remove("opacity");
    const auto oldHighlight = Frame::editFromJson(legacy);
    QVERIFY(oldHighlight);
    QCOMPARE(oldHighlight->color, QColor("#eab841"));
  }
  void labelAppearanceIsSharedRememberedAndUndoable() {
    const auto cleanup = qScopeGuard([] { QSettings().remove("labels"); });
    QSettings().remove("labels");
    QImage source(1000, 800, QImage::Format_ARGB32_Premultiplied);
    source.fill(QColor("#0000ff"));
    MarkDocument doc;
    doc.reset(source);
    QSignalSpy edited(&doc, &MarkDocument::edited);
    doc.setLabelStyle({{"color", "#eab841"}, {"background", "#ff0000"},
                       {"backgroundOpacity", .5}, {"fontPx", 36}, {"textAlign", "left"}});
    QCOMPARE(edited.count(), 0);
    QVERIFY(!doc.canUndo());
    doc.edit("text", .2, .2, .2, .2, "My label");
    const auto original = doc.edits().first();
    QCOMPARE(original.color, QColor("#eab841"));
    QCOMPARE(original.background, QColor("#ff0000"));
    QCOMPARE(original.backgroundOpacity, .5);
    QCOMPARE(Frame::textPixelSize(original, source), 36);
    QCOMPARE(original.textAlign, QString("left"));
    const auto bounds = Frame::annotationBounds(original, source);
    const auto image = Frame::applyEdits(source, {original});
    const auto pixel = image.pixelColor(qRound(bounds.center().x()*1000), qRound(bounds.top()*800)+2);
    QVERIFY(qAbs(pixel.red()-128) <= 1);
    QVERIFY(qAbs(pixel.blue()-128) <= 1);
    const auto signature = doc.annotations();
    doc.setLabelStyle({{"color", "#ffffff"}, {"backgroundOpacity", 0.},
                       {"textStyle", "shadow"}, {"textAlign", "right"}});
    const auto styled = doc.edits().first();
    QCOMPARE(styled.color, QColor(Qt::white));
    QCOMPARE(styled.backgroundOpacity, 0.);
    QCOMPARE(styled.textStyle, QString("shadow"));
    QVERIFY(doc.annotations() != signature);
    // All fields of the single choice undo together. Preferences remain deliberate choices.
    doc.undo();
    QVERIFY(doc.edits().first() == original);
    doc.redo();
    QVERIFY(doc.edits().first() == styled);
    MarkDocument reopened;
    reopened.reset(source);
    reopened.edit("text", .1, .1, .1, .1, "Remember me");
    QCOMPARE(reopened.edits().first().color, styled.color);
    QCOMPARE(reopened.edits().first().backgroundOpacity, 0.);
    QCOMPARE(reopened.edits().first().textStyle, styled.textStyle);
    const auto roundTrip = Frame::editFromJson(Frame::editToJson(styled));
    QVERIFY(roundTrip.has_value());
    QVERIFY(*roundTrip == styled);
    doc.setLabelStyle({{"color", "invalid"}, {"backgroundOpacity", .3}});
    QVERIFY(doc.edits().first() == styled);
    doc.setLabelStyle({{"backgroundOpacity", "invalid"}});
    QVERIFY(doc.edits().first() == styled);
    doc.resetLabelStyle();
    QCOMPARE(doc.edits().first().color, QColor(Qt::white));
    QCOMPARE(doc.edits().first().backgroundOpacity, 1.);
    QCOMPARE(doc.edits().first().textStyle, QString("box"));
    doc.clearSelection();
    QCOMPARE(doc.labelDefaults().value("fontPx").toInt(), doc.newTextPixels());
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
