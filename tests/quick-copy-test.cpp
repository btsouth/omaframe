#include "studio.hpp"
#include "omarchy-theme.hpp"
#include "navigation.hpp"
#include "recording.hpp"
#include "shortcuts.hpp"
#include "video.hpp"
#include <QColorSpace>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <cstdio>
#include <functional>

namespace {
QByteArray contents(const QString &path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
QMap<QString, QByteArray> filesUnder(const QString &path) {
  QMap<QString, QByteArray> files;
  QDirIterator it(path, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString file = it.next();
    files.insert(file, contents(file));
  }
  return files;
}
QImage captureImage() {
  QImage image(160, 100, QImage::Format_ARGB32_Premultiplied);
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x)
      image.setPixelColor(x, y, QColor(x, y, (x + y) % 256));
  image.setText("Secret", "private source metadata");
  image.setColorSpace(QColorSpace::SRgb);
  return image;
}
// Run this test executable as wl-copy. No shell, desktop or real clipboard.
int clipboardStub(int argc, char **argv) {
  if (argc != 3 || QByteArray(argv[1]) != "--type" ||
      (QByteArray(argv[2]) != "image/png" &&
       QByteArray(argv[2]) != "text/plain;charset=utf-8"))
    return 2;
  QFile input;
  if (!input.open(stdin, QIODevice::ReadOnly))
    return 3;
  const QByteArray png = input.readAll();
  QFile calls(qEnvironmentVariable("COPY_TEST_OUTPUT") + ".calls");
  if (!calls.open(QIODevice::WriteOnly | QIODevice::Append))
    return 4;
  calls.write("copy\n");
  calls.close();
  if (qEnvironmentVariableIsSet("COPY_TEST_FAIL"))
    return 1;
  QFile output(qEnvironmentVariable("COPY_TEST_OUTPUT"));
  return output.open(QIODevice::WriteOnly) && output.write(png) == png.size() ? 0 : 5;
}
} // namespace

class QuickCopyTest : public QObject {
  Q_OBJECT
  QTemporaryDir temp;
  QString clipboard() const { return temp.filePath("clipboard.png"); }
  void prepare(Studio &studio) {
    studio.setOutputDirectory(QUrl::fromLocalFile(temp.filePath("output")));
    // PATH contains fixture stubs, so this starts quick mode but
    // cannot query Hyprland or reach native capture. Supply a capture in memory
    // using the same result entry point as the scrolling capture tests.
    studio.capture(false);
    QTRY_COMPARE(studio.quickState(), QString("capture-error"));
    studio.scrollFinished(captureImage(), false, true);
    QCOMPARE(studio.quickState(), QString("choosing"));
    QVERIFY(studio.quickMode());
  }
private slots:
  void initTestCase() {
    QVERIFY(temp.isValid());
    const QString bin = temp.filePath("bin");
    QVERIFY(QDir().mkpath(bin));
    QVERIFY(QFile::link(QCoreApplication::applicationFilePath(), bin + "/wl-copy"));
    // finishNotice() only needs availability; no notification process is run.
    QVERIFY(QFile::link(QCoreApplication::applicationFilePath(), bin + "/notify-send"));
    QVERIFY(QFile::link(QCoreApplication::applicationFilePath(), bin + "/tesseract"));
    qputenv("PATH", QFile::encodeName(bin));
    qputenv("COPY_TEST_OUTPUT", QFile::encodeName(clipboard()));
  }
  void init() {
    QDir(temp.filePath("output")).removeRecursively();
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .removeRecursively();
    QSettings().clear();
    QSettings().setValue("paddingVersion", 2);
    QFile::remove(clipboard());
    QFile::remove(clipboard() + ".calls");
    qunsetenv("COPY_TEST_FAIL");
    qunsetenv("COPY_TEST_SECRET");
    qunsetenv("COPY_TEST_TEXT");
    qunsetenv("COPY_TEST_FIRST_READ_FAIL");
  }
  void automaticSavingDefaultsOnAndPersists() {
    // An existing config with no new key keeps its current behavior.
    QSettings().setValue("style", 4);
    ImageStore store;
    Studio studio(&store, false);
    QVERIFY(studio.autoSaveScreenshots());
    QSignalSpy changed(&studio, &Studio::changed);
    studio.setAutoSaveScreenshots(false);
    QCOMPARE(changed.count(), 1);
    QVERIFY(!studio.autoSaveScreenshots());
    studio.setAutoSaveScreenshots(false);
    QCOMPARE(changed.count(), 1);
    QSettings().sync();
    Studio reopened(&store, false);
    QVERIFY(!reopened.autoSaveScreenshots());
    QCOMPARE(reopened.style(), 4);
    reopened.setAutoSaveScreenshots(true);
    QVERIFY(studio.autoSaveScreenshots());
  }
  void failedDraftSaveKeepsImageAndEdits() {
    const QString first = temp.filePath("first.png");
    const QString second = temp.filePath("second.png");
    QVERIFY(captureImage().save(first));
    QImage replacement(80, 60, QImage::Format_RGB32);
    replacement.fill(Qt::white);
    QVERIFY(replacement.save(second));
    ImageStore store;
    Studio studio(&store, false);
    studio.open(QUrl::fromLocalFile(first));
    QTRY_VERIFY_WITH_TIMEOUT(!studio.busy() && !studio.rendering(), 5000);
    studio.marks()->edit("redact", .1, .1, .4, .4);
    const auto edits = studio.marks()->edits();
    const QString folder = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation) + "/drafts";
    QVERIFY(QDir(folder).removeRecursively());
    QVERIFY(QDir().mkpath(QFileInfo(folder).absolutePath()));
    QFile blocker(folder);
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    QVERIFY(!studio.saveDraftNow());
    QVERIFY(studio.draftDirty());
    studio.closeImage();
    QVERIFY(studio.hasImage());
    QCOMPARE(studio.marks()->edits(), edits);
    QVERIFY(studio.status().contains("Could not save"));
    studio.open(QUrl::fromLocalFile(second));
    QVERIFY(!studio.busy());
    QCOMPARE(studio.sourceSize(), QSize(160, 100));
    QCOMPARE(studio.marks()->edits(), edits);
    studio.resumeDraft(QString(32, 'a'));
    QCOMPARE(studio.marks()->edits(), edits);
    // Fixing the destination lets the same edits be saved and navigation resume.
    QVERIFY(QFile::remove(folder));
    QVERIFY(studio.saveDraftNow());
    QVERIFY(!studio.draftDirty());
    QCOMPARE(studio.drafts().size(), 1);
    studio.closeImage();
    QVERIFY(!studio.hasImage());
  }
  void failedDraftCanBeExplicitlyDiscarded() {
    const QString input = temp.filePath("discard.png");
    QVERIFY(captureImage().save(input));
    ImageStore store;
    Studio studio(&store, false);
    studio.open(QUrl::fromLocalFile(input));
    QTRY_VERIFY_WITH_TIMEOUT(!studio.busy() && !studio.rendering(), 5000);
    studio.marks()->edit("redact", .1, .1, .4, .4);
    const QString folder = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation) + "/drafts";
    QVERIFY(QDir(folder).removeRecursively());
    QVERIFY(QDir().mkpath(QFileInfo(folder).absolutePath()));
    QFile blocker(folder);
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    QVERIFY(!studio.saveDraftNow());
    studio.discardUnsavedDraft();
    QVERIFY(!studio.draftDirty());
    studio.closeImage();
    QVERIFY(!studio.hasImage());
    QVERIFY(studio.drafts().isEmpty());
  }
  void copiedTextUsesCurrentPixels_data() {
    QTest::addColumn<QString>("action");
    QTest::newRow("redact") << QString("redact");
    QTest::newRow("crop") << QString("crop");
    QTest::newRow("redact-pending-copy") << QString("pending");
  }
  void failedDraftNavigation_data() {
    QTest::addColumn<QString>("action");
    QTest::newRow("retry") << QString("retry");
    QTest::newRow("discard") << QString("discard");
    QTest::newRow("cancel") << QString("cancel");
  }
  void failedDraftNavigation() {
    QFETCH(QString, action);
    QQmlEngine engine;
    auto *store = new ImageStore;
    engine.addImageProvider("frames", store);
    auto *videoStore = new ImageStore;
    engine.addImageProvider("videomarks", videoStore);
    Studio studio(store, false);
    Video video;
    video.setImageStore(videoStore);
    Recorder recorder;
    ShortcutSetup shortcuts;
    Navigation navigation;
    OmarchyTheme theme(nullptr, temp.filePath("theme"), temp.filePath("theme-config"), false);
    engine.rootContext()->setContextProperty("studio", &studio);
    engine.rootContext()->setContextProperty("video", &video);
    engine.rootContext()->setContextProperty("recorder", &recorder);
    engine.rootContext()->setContextProperty("shortcuts", &shortcuts);
    engine.rootContext()->setContextProperty("navigation", &navigation);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("captureAtStartup", false);
    const QString input = temp.filePath("navigation.png");
    QVERIFY(captureImage().save(input));
    studio.open(QUrl::fromLocalFile(input));
    QTRY_VERIFY_WITH_TIMEOUT(!studio.busy() && !studio.rendering(), 5000);
    studio.marks()->edit("redact", .1, .1, .4, .4);
    const QString folder = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation) + "/drafts";
    QVERIFY(QDir(folder).removeRecursively());
    QVERIFY(QDir().mkpath(QFileInfo(folder).absolutePath()));
    QFile blocker(folder);
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../qml/Main.qml")));
    QTRY_VERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QQuickWindow> window(qobject_cast<QQuickWindow *>(component.create()));
    QVERIFY2(window, qPrintable(component.errorString()));
    QSignalSpy proceed(&navigation, &Navigation::proceed);
    QVERIFY(QMetaObject::invokeMethod(window.get(), "requestNavigation",
        Q_ARG(QVariant, "quit"), Q_ARG(QVariant, QUrl())));
    QVERIFY(navigation.pending());
    QVERIFY(proceed.isEmpty());
    QVERIFY(studio.hasImage() && studio.draftDirty());
    if (action == "retry") {
      navigation.save();
      QVERIFY(navigation.pending());
      QVERIFY(!navigation.saving());
      QVERIFY(proceed.isEmpty());
      QVERIFY(QFile::remove(folder));
      navigation.save();
      QCOMPARE(proceed.count(), 1);
      QVERIFY(!studio.draftDirty());
      QCOMPARE(studio.drafts().size(), 1);
    } else if (action == "discard") {
      // Exercise the dialog's real button handler, including the explicit discard.
      QQuickItem *button = nullptr;
      std::function<void(QQuickItem *)> visit = [&](QQuickItem *item) {
        if (item->objectName() == "discardUnsavedEdits") button = item;
        for (auto *child : item->childItems()) visit(child);
      };
      visit(window->contentItem());
      QVERIFY(button);
      QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
      QCOMPARE(proceed.count(), 1);
      QVERIFY(!studio.draftDirty());
      QVERIFY(studio.drafts().isEmpty());
    } else {
      navigation.cancel();
      QVERIFY(proceed.isEmpty());
      QVERIFY(studio.draftDirty());
      QVERIFY(studio.hasImage());
    }
  }
  void editorClipboardKeysKeepTheScreenshotOpen_data() {
    QTest::addColumn<bool>("autoSave");
    QTest::addColumn<bool>("ctrlS");
    QTest::addColumn<bool>("failFirst");
    QTest::newRow("copy-only") << false << false << false;
    QTest::newRow("save-and-copy") << true << false << false;
    QTest::newRow("ctrl-s-saves-anyway") << false << true << false;
    QTest::newRow("copy-fails-then-retries") << false << false << true;
  }
  void editorClipboardKeysKeepTheScreenshotOpen() {
    QFETCH(bool, autoSave);
    QFETCH(bool, ctrlS);
    QFETCH(bool, failFirst);
    QQmlEngine engine;
    auto *store = new ImageStore;
    engine.addImageProvider("frames", store);
    auto *videoStore = new ImageStore;
    engine.addImageProvider("videomarks", videoStore);
    Studio studio(store, false);
    studio.setAutoSaveScreenshots(autoSave);
    Video video;
    video.setImageStore(videoStore);
    Recorder recorder;
    ShortcutSetup shortcuts;
    Navigation navigation;
    OmarchyTheme theme(nullptr, temp.filePath("theme"), temp.filePath("theme-config"), false);
    engine.rootContext()->setContextProperty("studio", &studio);
    engine.rootContext()->setContextProperty("video", &video);
    engine.rootContext()->setContextProperty("recorder", &recorder);
    engine.rootContext()->setContextProperty("shortcuts", &shortcuts);
    engine.rootContext()->setContextProperty("navigation", &navigation);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("captureAtStartup", true);
    prepare(studio);
    QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../qml/Main.qml")));
    QTRY_VERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QQuickWindow> window(qobject_cast<QQuickWindow *>(component.create()));
    QVERIFY2(window, qPrintable(component.errorString()));
    studio.openEditor();
    QVERIFY(window->property("editing").toBool());
    window->show();
    window->requestActivate();
    QVERIFY(QTest::qWaitForWindowExposed(window.get()));
    QTRY_VERIFY(window->isActive());
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QSignalSpy dismissed(&studio, &Studio::dismissRequested);
    const auto item = [&](const QString &name) {
      QQuickItem *found = nullptr;
      std::function<void(QQuickItem *)> find = [&](QQuickItem *child) {
        if (child->objectName() == name) found = child;
        for (auto *next : child->childItems()) find(next);
      };
      find(window->contentItem());
      return found;
    };
    // With automatic saving off, the main button copies and a second one saves.
    QVERIFY(item("editorFinishButton") && item("editorSaveButton"));
    QCOMPARE(item("editorFinishButton")->property("text").toString(),
             autoSave ? QString("Copy and save") : QString("Copy"));
    QCOMPARE(item("editorSaveButton")->isVisible(), !autoSave);
    studio.marks()->edit("arrow", .1, .1, .4, .3);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.marks()->selectedAnnotation().value("type").toString(), QString("arrow"));

    // Ctrl+C and Ctrl+V copy the mark. The screenshot stays open and nothing
    // reaches the clipboard or the save folder.
    QTest::keyClick(window.get(), Qt::Key_C, Qt::ControlModifier);
    QVERIFY(studio.marks()->canPaste());
    QTest::keyClick(window.get(), Qt::Key_V, Qt::ControlModifier);
    QTest::keyClick(window.get(), Qt::Key_V, Qt::ControlModifier);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    const auto &edits = studio.marks()->edits();
    QCOMPARE(edits.size(), 3);
    for (const auto &edit : edits) {
      QCOMPARE(edit.type, QString("arrow"));
      QCOMPARE(edit.to - edit.from, edits[0].to - edits[0].from); // Same size.
    }
    QVERIFY(edits[1].from != edits[0].from && edits[2].from != edits[1].from);
    QCOMPARE(studio.marks()->selectedAnnotation().value("index").toInt(), 2);
    QTest::keyClick(window.get(), Qt::Key_X, Qt::ControlModifier);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QCOMPARE(studio.marks()->edits().size(), 2);
    QVERIFY(studio.marks()->canPaste());
    // With nothing selected, Ctrl+C explains how to finish instead.
    studio.marks()->clearSelection();
    QTest::keyClick(window.get(), Qt::Key_C, Qt::ControlModifier);
    QVERIFY(window->property("currentStatus").toString().startsWith("Select a mark"));
    QTest::qWait(100);
    QCOMPARE(dismissed.count(), 0);
    QCOMPARE(studio.quickState(), QString("editing"));
    QVERIFY(!QFileInfo::exists(clipboard()));
    QVERIFY(!QDir(temp.filePath("output")).exists());

    if (failFirst) {
      // A failed copy keeps the editor and edits open, and Copy retries.
      qputenv("COPY_TEST_FAIL", "1");
      QTest::keyClick(window.get(), Qt::Key_Return, Qt::ControlModifier);
      QTRY_COMPARE_WITH_TIMEOUT(studio.quickState(), QString("copy-failed"), 15000);
      QVERIFY(studio.status().contains("Press Copy to retry"));
      QCOMPARE(dismissed.count(), 0);
      QVERIFY(window->property("editing").toBool());
      QCOMPARE(studio.marks()->edits().size(), 2);
      qunsetenv("COPY_TEST_FAIL");
      QFile::remove(clipboard());
    }
    // Ctrl+Enter finishes the way the setting says; Ctrl+S always saves.
    if (ctrlS) QTest::keyClick(window.get(), Qt::Key_S, Qt::ControlModifier);
    else QTest::keyClick(window.get(), Qt::Key_Return, Qt::ControlModifier);
    QTRY_COMPARE_WITH_TIMEOUT(dismissed.count(), 1, 15000);
    const bool saved = autoSave || ctrlS;
    QCOMPARE(studio.quickState(), saved ? QString("done") : QString("copied"));
    QVERIFY(QFileInfo::exists(clipboard()));
    QCOMPARE(QDir(temp.filePath("output")).exists(), saved);
    QCOMPARE(studio.savedPath().isEmpty(), !saved);
    QCOMPARE(QImage(clipboard()).size(),
             Frame::compose(Frame::applyEdits(captureImage(), studio.marks()->edits()),
                            {studio.style(), studio.padding(), studio.aspect()}).size());
  }
  void copiedTextUsesCurrentPixels() {
    QFETCH(QString, action);
    qputenv("COPY_TEST_TEXT", "1");
    QImage image(160, 100, QImage::Format_RGB32);
    image.fill(Qt::white);
    const QString input = temp.filePath("text.png");
    QVERIFY(image.save(input));
    ImageStore store;
    Studio studio(&store, false);
    studio.open(QUrl::fromLocalFile(input));
    QTRY_VERIFY_WITH_TIMEOUT(studio.secretCount() == 1, 5000);
    if (action == "pending") studio.copyText();
    if (action == "crop")
      QVERIFY(studio.marks()->cropCurrentView(0, .45, 1, 1));
    else
      studio.hideSecrets();
    if (action != "pending") studio.copyText();
    QTRY_COMPARE_WITH_TIMEOUT(studio.textNote(), QString("Copied the text."), 5000);
    const QByteArray copied = contents(clipboard());
    QVERIFY(copied.contains("Public status"));
    QVERIFY(!copied.contains("ghp_"));
  }
  void detectorFailureCannotPublishStaleText() {
    qputenv("COPY_TEST_TEXT", "1");
    const QString marker = temp.filePath("detector-started");
    QFile::remove(marker);
    QFile::remove(marker + ".release");
    qputenv("COPY_TEST_FIRST_READ_FAIL", QFile::encodeName(marker));
    QImage image(160, 100, QImage::Format_RGB32);
    image.fill(Qt::white);
    const QString input = temp.filePath("failed-detector.png");
    QVERIFY(image.save(input));
    ImageStore store;
    Studio studio(&store, false);
    studio.open(QUrl::fromLocalFile(input));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(marker), 5000);
    QVERIFY(studio.canReadText());
    studio.copyText();
    QFile release(marker + ".release");
    QVERIFY(release.open(QIODevice::WriteOnly));
    release.close();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.canReadText(), 5000);
    // Copy OCR is still pending after original-image detection fails.
    studio.marks()->edit("redact", .05, .05, .75, .4);
    QTRY_COMPARE_WITH_TIMEOUT(studio.textNote(), QString("Copied the text."), 5000);
    QVERIFY(contents(clipboard()).contains("Public status"));
    QVERIFY(!contents(clipboard()).contains("ghp_"));
  }
  void newerPngCopyCancelsPendingText_data() {
    QTest::addColumn<bool>("save");
    QTest::newRow("clipboard-only") << false;
    QTest::newRow("save-and-copy") << true;
  }
  void newerPngCopyCancelsPendingText() {
    QFETCH(bool, save);
    qputenv("COPY_TEST_TEXT", "1");
    ImageStore store;
    Studio studio(&store, false);
    prepare(studio);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 5000);
    QVERIFY(studio.canReadText());
    studio.copyText();
    QCOMPARE(studio.textNote(), QString("Reading text…"));
    if (save) studio.accept();
    else studio.copyQuick();
    QTRY_VERIFY_WITH_TIMEOUT(!studio.busy(), 5000);
    QVERIFY(!QImage::fromData(contents(clipboard())).isNull());
    QTest::qWait(500); // Let an uncancelled delayed text worker finish.
    QVERIFY(!QImage::fromData(contents(clipboard())).isNull());
  }
  void chooserActions_data() {
    QTest::addColumn<QString>("action");
    QTest::addColumn<bool>("autoSave");
    for (const QString action : {"escape", "empty", "capture-error", "close", "background",
                                 "retry", "number", "card-click", "card-double-click",
                                 "focused-card-enter", "enter", "card-edit",
                                 "save-button", "clipboard-button", "ctrl-c", "focus-ctrl-c",
                                 "modified-c"})
      for (bool autoSave : {false, true})
        QTest::newRow(qPrintable(action + (autoSave ? "-save" : "-copy"))) << action << autoSave;
  }
  void chooserActions() {
    QFETCH(QString, action);
    QFETCH(bool, autoSave);
    QQmlEngine engine;
    auto *store = new ImageStore;
    engine.addImageProvider("frames", store);
    Studio studio(store, false);
    studio.setAutoSaveScreenshots(autoSave);
    OmarchyTheme theme(nullptr, temp.filePath("theme"), temp.filePath("theme-config"), false);
    if (action != "empty")
      prepare(studio);
    else
      for (int i = 0; i < 9; ++i)
        store->put(QString("style%1").arg(i), captureImage());
    if (action == "capture-error")
      studio.scrollFailed("Capture failed with an older image still open.");
    engine.rootContext()->setContextProperty("studio", &studio);
    engine.rootContext()->setContextProperty("theme", &theme);
    QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../qml/FinishChooser.qml")));
    QTRY_VERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QQuickWindow> chooser(qobject_cast<QQuickWindow *>(component.create()));
    QVERIFY2(chooser, qPrintable(component.errorString()));
    chooser->resize(1100, 800);
    chooser->show();
    chooser->requestActivate();
    QVERIFY(QTest::qWaitForWindowExposed(chooser.get()));
    QTest::qWait(50); // Let the offscreen scene polish its anchored layout.
    QTRY_VERIFY(chooser->activeFocusItem());
    QSignalSpy dismissed(&studio, &Studio::dismissRequested);
    const auto item = [&](const QString &name) {
      QQuickItem *found = nullptr;
      std::function<void(QQuickItem *)> find = [&](QQuickItem *child) {
        if (child->objectName() == name) found = child;
        for (auto *next : child->childItems()) find(next);
      };
      find(chooser->contentItem());
      return found;
    };
    const auto click = [&](QQuickItem *control) {
      QVERIFY(control);
      QVERIFY(control->isVisible());
      QVERIFY(control->isEnabled());
      const QPoint center = control->mapToScene(QPointF(control->width() / 2,
                                                        control->height() / 2)).toPoint();
      QTest::mouseClick(chooser.get(), Qt::LeftButton, Qt::NoModifier, center);
    };
    int expectedStyle = studio.style();
    if (action == "close")
      chooser->close();
    else if (action == "background") {
      // Clicking outside the panel no longer cancels; Esc still does.
      QTest::mouseClick(chooser.get(), Qt::LeftButton, Qt::NoModifier, QPoint(2, 2));
      QTest::qWait(100);
      QCOMPARE(dismissed.count(), 0);
      QCOMPARE(studio.quickState(), QString("choosing"));
      QTest::keyClick(chooser.get(), Qt::Key_Escape);
    } else if (action == "number") {
      expectedStyle = 1;
      QTest::keyClick(chooser.get(), Qt::Key_2);
    } else if (action == "card-click" || action == "card-edit") {
      // A single click only selects. Nothing is copied, saved or closed until
      // the selection is used.
      const int before = studio.style();
      click(item("finish3"));
      QTest::qWait(400); // Longer than a double-click interval.
      QCOMPARE(dismissed.count(), 0);
      QCOMPARE(studio.quickState(), QString("choosing"));
      QCOMPARE(studio.style(), before);
      QCOMPARE(chooser->property("selected").toInt(), 3);
      QVERIFY(!QFileInfo::exists(clipboard()));
      expectedStyle = 3;
      if (action == "card-edit") {
        QSignalSpy editor(&studio, &Studio::editorRequested);
        QTest::keyClick(chooser.get(), Qt::Key_E);
        QCOMPARE(editor.count(), 1);
        QCOMPARE(studio.quickState(), QString("editing"));
        QCOMPARE(studio.style(), expectedStyle);
        QCOMPARE(dismissed.count(), 0);
        QVERIFY(!QFileInfo::exists(clipboard()));
        return;
      }
      QTest::keyClick(chooser.get(), Qt::Key_Return);
    } else if (action == "card-double-click") {
      expectedStyle = 3;
      QQuickItem *card = item("finish3");
      QVERIFY(card);
      const QPoint center = card->mapToScene(QPointF(card->width() / 2,
                                                     card->height() / 2)).toPoint();
      QTest::mouseDClick(chooser.get(), Qt::LeftButton, Qt::NoModifier, center);
    } else if (action == "focused-card-enter") {
      expectedStyle = 3;
      QVERIFY(item("finish3"));
      item("finish3")->forceActiveFocus();
      QTest::keyClick(chooser.get(), Qt::Key_Return);
    } else if (action == "enter")
      QTest::keyClick(chooser.get(), Qt::Key_Return);
    else if (action == "clipboard-button")
      click(item("clipboardButton"));
    else if (action == "save-button")
      click(item("saveButton"));
    else if (action == "modified-c") {
      QTest::keyClick(chooser.get(), Qt::Key_C, Qt::ControlModifier | Qt::AltModifier);
      QTest::qWait(100);
      QCOMPARE(dismissed.count(), 0);
      QCOMPARE(studio.quickState(), QString("choosing"));
      QVERIFY(!QFileInfo::exists(clipboard()));
      QTest::keyClick(chooser.get(), Qt::Key_Escape);
    } else if (action == "ctrl-c" || action == "focus-ctrl-c" || action == "retry") {
      if (action == "focus-ctrl-c") {
        // Keyboard focus selects the card, so Ctrl+C copies that finish.
        QVERIFY(item("finish3"));
        item("finish3")->forceActiveFocus();
        expectedStyle = 3;
      }
      if (action == "retry")
        qputenv("COPY_TEST_FAIL", "1");
      QTest::keyClick(chooser.get(), Qt::Key_C, Qt::ControlModifier);
    } else
      QTest::keyClick(chooser.get(), Qt::Key_Escape);
    if (action == "retry") {
      QTRY_VERIFY_WITH_TIMEOUT(!studio.busy(), 15000);
      QCOMPARE(dismissed.count(), 0);
      QCOMPARE(studio.quickState(), QString("copy-failed"));
      QVERIFY(chooser->isVisible());
      bool visibleError = false;
      for (auto *child : chooser->findChildren<QQuickItem *>())
        visibleError |= child->isVisible() && child->property("text").toString() == studio.status();
      QVERIFY2(visibleError, "The copy failure must be visible in the chooser.");
      qunsetenv("COPY_TEST_FAIL");
      QTest::keyClick(chooser.get(), Qt::Key_C, Qt::ControlModifier);
    }
    QTRY_COMPARE_WITH_TIMEOUT(dismissed.count(), 1, 15000);
    const bool defaultAction = action == "number" || action == "card-click" ||
                               action == "card-double-click" ||
                               action == "focused-card-enter" || action == "enter";
    const bool saved = action == "save-button" || (autoSave && defaultAction);
    const bool copied = defaultAction || saved || action == "retry" || action == "ctrl-c" ||
                        action == "focus-ctrl-c" || action == "clipboard-button";
    QCOMPARE(studio.quickState(),
             saved ? QString("done") : copied ? QString("copied") : QString("cancelled"));
    QCOMPARE(studio.style(), expectedStyle);
    QCOMPARE(QFileInfo::exists(clipboard()), copied);
    QCOMPARE(QDir(temp.filePath("output")).exists(), saved);
    QCOMPARE(studio.savedPath().isEmpty(), !saved);
    QCOMPARE(studio.autoSaveScreenshots(), autoSave); // Overrides never change the preference.
    if (saved)
      QCOMPARE(contents(clipboard()), contents(studio.savedPath()));
    if (copied) {
      const QImage expected = Frame::compose(captureImage(), {expectedStyle, studio.padding(), studio.aspect()});
      QCOMPARE(QImage(clipboard()), expected.convertToFormat(QImage::Format_ARGB32));
      QCOMPARE(contents(clipboard() + ".calls"),
               action == "retry" ? QByteArray("copy\ncopy\n") : QByteArray("copy\n"));
    }
  }
  void copiesTheChosenFinish() {
    ImageStore store;
    Studio studio(&store, false);
    prepare(studio);
    studio.setStyle(1); // A decorated finish, not Raw.
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 15000);
    const QImage expected =
        Frame::compose(captureImage(), {studio.style(), studio.padding(), studio.aspect()});
    QVERIFY(expected.size() != captureImage().size());
    QSignalSpy dismissed(&studio, &Studio::dismissRequested);
    QVERIFY(QMetaObject::invokeMethod(&studio, "copyQuick"));
    QTRY_COMPARE_WITH_TIMEOUT(dismissed.count(), 1, 15000);
    QCOMPARE(studio.quickState(), QString("copied"));
    const QImage copied(clipboard());
    QCOMPARE(copied.size(), expected.size());
    for (int y = 0; y < copied.height(); ++y)
      for (int x = 0; x < copied.width(); ++x)
        QCOMPARE(copied.pixelColor(x, y), expected.pixelColor(x, y));
    QVERIFY(studio.savedPath().isEmpty());
    QVERIFY(!QDir(studio.outputDirectory()).exists());
  }
  void copyFailureStaysOpenAndCanRetry() {
    ImageStore store;
    Studio studio(&store, false);
    prepare(studio);
    QSettings().sync();
    const auto before = filesUnder(QDir::homePath());
    QFile oldClipboard(clipboard());
    QVERIFY(oldClipboard.open(QIODevice::WriteOnly));
    oldClipboard.write("previous clipboard");
    oldClipboard.close();
    QSignalSpy dismissed(&studio, &Studio::dismissRequested);
    qputenv("COPY_TEST_FAIL", "1");
    QVERIFY(QMetaObject::invokeMethod(&studio, "copyQuick"));
    QTRY_VERIFY_WITH_TIMEOUT(!studio.busy(), 15000);
    QCOMPARE(dismissed.count(), 0);
    QCOMPARE(studio.quickState(), QString("copy-failed"));
    QVERIFY(studio.status().contains("wl-copy"));
    QVERIFY(studio.status().contains("Ctrl+C to retry"));
    QVERIFY(studio.savedPath().isEmpty());
    QVERIFY(studio.recoveryAction().isEmpty());
    QVERIFY(!QDir(studio.outputDirectory()).exists());
    QCOMPARE(contents(clipboard()), QByteArray("previous clipboard"));
    QCOMPARE(filesUnder(QDir::homePath()), before);
    qunsetenv("COPY_TEST_FAIL");
    QVERIFY(QMetaObject::invokeMethod(&studio, "copyQuick"));
    QTRY_COMPARE_WITH_TIMEOUT(dismissed.count(), 1, 15000);
    QCOMPARE(studio.quickState(), QString("copied"));
    QVERIFY(!QImage(clipboard()).isNull());
    QCOMPARE(filesUnder(QDir::homePath()), before);
  }
  void normalFinishStillWorksAfterCopyFailure() {
    ImageStore store;
    Studio studio(&store, false);
    prepare(studio);
    qputenv("COPY_TEST_FAIL", "1");
    QVERIFY(QMetaObject::invokeMethod(&studio, "copyQuick"));
    QTRY_VERIFY_WITH_TIMEOUT(!studio.busy(), 15000);
    QCOMPARE(studio.quickState(), QString("copy-failed"));
    qunsetenv("COPY_TEST_FAIL");
    studio.chooseFinish(0);
    QTRY_COMPARE_WITH_TIMEOUT(studio.quickState(), QString("done"), 15000);
    QVERIFY(QFileInfo::exists(studio.savedPath()));
    QCOMPARE(contents(clipboard()), contents(studio.savedPath()));
  }
  void duplicateBusyAndTerminalCallsDoNothing() {
    ImageStore store;
    Studio studio(&store, false);
    prepare(studio);
    QSignalSpy dismissed(&studio, &Studio::dismissRequested);
    QVERIFY(QMetaObject::invokeMethod(&studio, "copyQuick"));
    QVERIFY(studio.busy());
    QVERIFY(QMetaObject::invokeMethod(&studio, "copyQuick"));
    studio.chooseFinish(0);
    studio.dismissQuick();
    QTRY_COMPARE_WITH_TIMEOUT(dismissed.count(), 1, 15000);
    QVERIFY(QMetaObject::invokeMethod(&studio, "copyQuick"));
    studio.chooseFinish(0);
    QTest::qWait(350);
    QCOMPARE(dismissed.count(), 1);
    QCOMPARE(contents(clipboard() + ".calls"), QByteArray("copy\n"));
    QVERIFY(studio.savedPath().isEmpty());
  }
  void emptyBusyCaptureErrorAndCancelledAreGuarded() {
    ImageStore store;
    Studio studio(&store, false);
    QSignalSpy dismissed(&studio, &Studio::dismissRequested);
    QVERIFY(QMetaObject::invokeMethod(&studio, "copyQuick"));
    QCOMPARE(studio.quickState(), QString("idle"));
    studio.capture(false);
    QVERIFY(QMetaObject::invokeMethod(&studio, "copyQuick"));
    QCOMPARE(studio.quickState(), QString("capturing"));
    QTRY_COMPARE(studio.quickState(), QString("capture-error"));
    QVERIFY(QMetaObject::invokeMethod(&studio, "copyQuick"));
    QCOMPARE(dismissed.count(), 0);
    studio.scrollFinished(captureImage(), false, true);
    studio.scrollFailed("Capture failed; an older image is still open.");
    QVERIFY(studio.hasImage());
    QVERIFY(QMetaObject::invokeMethod(&studio, "copyQuick"));
    QCOMPARE(studio.quickState(), QString("capture-error"));
    studio.dismissQuick();
    QCOMPARE(dismissed.count(), 1);
    QVERIFY(QMetaObject::invokeMethod(&studio, "copyQuick"));
    QTest::qWait(350);
    QCOMPARE(studio.quickState(), QString("cancelled"));
    QCOMPARE(dismissed.count(), 1);
    QVERIFY(!QFileInfo::exists(clipboard()));
  }
  void editsAreCopiedWithoutFlushingADraft() {
    ImageStore store;
    Studio studio(&store, false);
    prepare(studio);
    studio.setStyle(8); // Raw, so pixel positions match the source.
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 15000);
    studio.openEditor();
    studio.setEditing(true);
    studio.marks()->edit("redact", 0.5, 0, 1, 1);
    studio.marks()->edit("crop", 0.25, 0, 0.75, 1);
    studio.showFinishes();
    QSettings().sync();
    const auto before = filesUnder(QDir::homePath());
    QSignalSpy dismissed(&studio, &Studio::dismissRequested);
    QVERIFY(QMetaObject::invokeMethod(&studio, "copyQuick"));
    studio.saveDraftNow(); // A shutdown flush must not write while copying.
    QTRY_COMPARE_WITH_TIMEOUT(dismissed.count(), 1, 15000);
    const QImage copied(clipboard());
    QCOMPARE(copied.size(), QSize(80, 100));
    QCOMPARE(copied.pixelColor(60, 50), QColor("#151a20"));
    QCOMPARE(copied.pixelColor(10, 50), captureImage().pixelColor(50, 50));
    QVERIFY(copied.textKeys().isEmpty());
    studio.saveDraftNow();
    QTest::qWait(350);
    QCOMPARE(filesUnder(QDir::homePath()), before);
    QVERIFY(studio.drafts().isEmpty());
    QVERIFY(studio.savedPath().isEmpty());
    QVERIFY(!QDir(studio.outputDirectory()).exists());
  }
  void existingDraftIsLeftUntouched() {
    ImageStore store;
    Studio studio(&store, false);
    prepare(studio);
    studio.setStyle(8); // Raw, so pixel positions match the source.
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 15000);
    studio.marks()->edit("text", 0.1, 0.1, 0.1, 0.1, "Draft");
    studio.saveDraftNow();
    QCOMPARE(studio.drafts().size(), 1);
    studio.marks()->edit("redact", 0.5, 0, 1, 1);
    QSettings().sync();
    const auto before = filesUnder(QDir::homePath());
    QVERIFY(QMetaObject::invokeMethod(&studio, "copyQuick"));
    QTRY_COMPARE_WITH_TIMEOUT(studio.quickState(), QString("copied"), 15000);
    studio.saveDraftNow();
    QCOMPARE(filesUnder(QDir::homePath()), before);
    QCOMPARE(studio.drafts().size(), 1);
    QCOMPARE(QImage(clipboard()).pixelColor(120, 50), QColor("#151a20"));
  }
  void normalAcceptSavesAndCopiesRenderedEdits() {
    ImageStore store;
    Studio studio(&store, false);
    prepare(studio);
    studio.setAutoSaveScreenshots(false); // Explicit editor saves still save.
    studio.openEditor();
    studio.marks()->edit("redact", 0.5, 0, 1, 1);
    studio.marks()->edit("crop", 0.25, 0, 0.75, 1);
    studio.setStyle(1);
    studio.setKeepOriginals(true);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 15000);
    const auto expected = Frame::compose(Frame::applyEdits(captureImage(), studio.marks()->edits()),
                                         {studio.style(), studio.padding(), studio.aspect()});
    QSignalSpy dismissed(&studio, &Studio::dismissRequested);
    studio.accept();
    QTRY_COMPARE_WITH_TIMEOUT(dismissed.count(), 1, 15000);
    QCOMPARE(studio.quickState(), QString("done"));
    QVERIFY(QFileInfo::exists(studio.savedPath()));
    const QImage saved(studio.savedPath());
    QCOMPARE(saved.size(), expected.size());
    QVERIFY(saved.size() != QSize(80, 100)); // A decorative finish still applies.
    for (int y = 0; y < saved.height(); ++y)
      for (int x = 0; x < saved.width(); ++x)
        QCOMPARE(saved.pixelColor(x, y), expected.pixelColor(x, y));
    QCOMPARE(contents(clipboard()), contents(studio.savedPath()));
    QVERIFY(saved.textKeys().isEmpty());
    QCOMPARE(studio.originalsCount(), 1);
    QVERIFY(!studio.drafts().isEmpty());
  }
  void pendingFinishIsNotOverriddenByCopy() {
    ImageStore store;
    Studio studio(&store, false);
    prepare(studio);
    studio.chooseFinish(1); // Queued behind rendering.
    QVERIFY(studio.rendering());
    QVERIFY(QMetaObject::invokeMethod(&studio, "copyQuick"));
    QTRY_COMPARE_WITH_TIMEOUT(studio.quickState(), QString("done"), 15000);
    QCOMPARE(contents(clipboard() + ".calls"), QByteArray("copy\n"));
    QVERIFY(QFileInfo::exists(studio.savedPath()));
    QCOMPARE(contents(clipboard()), contents(studio.savedPath()));
  }
  void copyDoesNotDropAPendingRetry() {
    qputenv("COPY_TEST_SECRET", "1");
    ImageStore store;
    Studio studio(&store, false);
    prepare(studio);
    QTRY_COMPARE(studio.secretCount(), 1);
    studio.setKeepOriginals(true);
    // A file where the originals folder belongs makes the private backup fail
    // after the finished PNG is saved, leaving "Retry backup" to recover.
    const QString originals =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/originals";
    QVERIFY(QDir().mkpath(QFileInfo(originals).absolutePath()));
    QFile blocker(originals);
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    studio.chooseFinish(0); // May queue behind the first render, so wait on the state.
    QTRY_COMPARE_WITH_TIMEOUT(studio.quickState(), QString("failed"), 15000);
    QVERIFY(!studio.busy());
    QCOMPARE(studio.recoveryAction(), QString("Retry backup"));
    const QString saved = studio.savedPath();
    QVERIFY(QFileInfo::exists(saved));
    const QByteArray callsBefore = contents(clipboard() + ".calls");
    QSignalSpy dismissed(&studio, &Studio::dismissRequested);
    QVERIFY(QMetaObject::invokeMethod(&studio, "copyQuick"));
    QTest::qWait(350);
    QVERIFY(!studio.busy());
    QCOMPARE(dismissed.count(), 0);
    QCOMPARE(studio.quickState(), QString("failed"));
    QCOMPARE(studio.recoveryAction(), QString("Retry backup"));
    QCOMPARE(studio.savedPath(), saved);
    QCOMPARE(contents(clipboard() + ".calls"), callsBefore);
    // Changing the preference or finish must not discard an outstanding backup.
    studio.setAutoSaveScreenshots(false);
    studio.setStyle(1);
    QCOMPARE(studio.style(), 0);
    studio.chooseFinish(1);
    studio.copyQuick();
    studio.openEditor();
    studio.hideSecrets();
    QCOMPARE(studio.secretCount(), 1);
    QVERIFY(studio.marks()->edits().isEmpty());
    QCOMPARE(studio.quickState(), QString("failed"));
    QCOMPARE(studio.recoveryAction(), QString("Retry backup"));
    QCOMPARE(studio.savedPath(), saved);
    QCOMPARE(contents(clipboard() + ".calls"), callsBefore);
    QVERIFY(blocker.remove());
    studio.chooseFinish(0); // The already-requested save is completed, even with the setting off.
    QTRY_COMPARE_WITH_TIMEOUT(studio.quickState(), QString("done"), 15000);
    QCOMPARE(studio.savedPath(), saved);
    QVERIFY(QFileInfo::exists(originals));
  }
  void pendingSavedCopyCannotBeReplacedByCopyOnly() {
    ImageStore store;
    Studio studio(&store, false);
    prepare(studio);
    qputenv("COPY_TEST_FAIL", "1");
    studio.chooseFinish(0);
    QTRY_COMPARE_WITH_TIMEOUT(studio.quickState(), QString("failed"), 15000);
    QCOMPARE(studio.recoveryAction(), QString("Retry copy"));
    const QString saved = studio.savedPath();
    studio.copyQuick();
    QCOMPARE(studio.savedPath(), saved);
    QCOMPARE(studio.quickState(), QString("failed"));
    qunsetenv("COPY_TEST_FAIL");
    studio.setAutoSaveScreenshots(false);
    studio.chooseFinish(0);
    QTRY_COMPARE_WITH_TIMEOUT(studio.quickState(), QString("done"), 15000);
    QCOMPARE(studio.savedPath(), saved);
    QCOMPARE(contents(clipboard()), contents(saved));
  }
  void copyRejectsOversizedCanvasAndCanRecover() {
    ImageStore store;
    Studio studio(&store, false);
    prepare(studio);
    QImage tall(160, 12000, QImage::Format_ARGB32_Premultiplied);
    tall.fill(QColor("#446688"));
    studio.scrollFinished(tall, false, true);
    studio.setAspect(1); // Small source, but a square finish would exceed 144 megapixels.
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 15000);
    studio.copyQuick();
    QTRY_COMPARE_WITH_TIMEOUT(studio.quickState(), QString("copy-failed"), 15000);
    QVERIFY(studio.status().contains("80 megapixels"));
    QVERIFY(!QFileInfo::exists(clipboard()));
    QVERIFY(!QDir(studio.outputDirectory()).exists());
    QVERIFY(studio.savedPath().isEmpty());
    studio.setAspect(0);
    studio.copyQuick();
    QTRY_COMPARE_WITH_TIMEOUT(studio.quickState(), QString("copied"), 15000);
    QVERIFY(!QImage(clipboard()).isNull());
  }
  void missingClipboardToolStaysOpen() {
    ImageStore store;
    Studio studio(&store, false);
    prepare(studio);
    const QString stub = temp.filePath("bin/wl-copy");
    QVERIFY(QFile::remove(stub));
    QVERIFY(QMetaObject::invokeMethod(&studio, "copyQuick"));
    QTRY_VERIFY_WITH_TIMEOUT(!studio.busy(), 15000);
    QVERIFY(QFile::link(QCoreApplication::applicationFilePath(), stub));
    QCOMPARE(studio.quickState(), QString("copy-failed"));
    QVERIFY(studio.status().contains("installed"));
    QVERIFY(studio.status().contains("Ctrl+C to retry"));
    QVERIFY(studio.savedPath().isEmpty());
    QVERIFY(!QFileInfo::exists(clipboard()));
  }
  void noticeSaysWhetherTheScreenshotWasSaved() {
    ImageStore store;
    Studio copiedOnly(&store, false);
    QVERIFY(copiedOnly.finishNotice().summary.isEmpty()); // Nothing happened yet.
    prepare(copiedOnly);
    qputenv("COPY_TEST_FAIL", "1");
    QVERIFY(QMetaObject::invokeMethod(&copiedOnly, "copyQuick"));
    QTRY_VERIFY_WITH_TIMEOUT(!copiedOnly.busy(), 15000);
    QVERIFY(copiedOnly.finishNotice().summary.isEmpty()); // No stale success.
    qunsetenv("COPY_TEST_FAIL");
    QVERIFY(QMetaObject::invokeMethod(&copiedOnly, "copyQuick"));
    QTRY_COMPARE_WITH_TIMEOUT(copiedOnly.quickState(), QString("copied"), 15000);
    const auto copied = copiedOnly.finishNotice();
    QCOMPARE(copied.summary, QString("Screenshot copied"));
    QCOMPARE(copied.body, QString("Copied to clipboard"));
    // The preview looks like a saved screenshot's, but lives in the private
    // runtime folder (RAM on most systems), never under HOME.
    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    QVERIFY2(copied.image.startsWith(runtime + "/"), qPrintable(copied.image));
    QVERIFY(!copied.image.startsWith(QDir::homePath() + "/"));
    QCOMPARE(contents(copied.image), contents(clipboard()));
    QCOMPARE(QFileInfo(copied.image).permissions() & (QFile::ReadGroup | QFile::ReadOther),
             QFileDevice::Permissions());
    // A second copy replaces the preview instead of piling up files.
    const QString firstPreview = copied.image;
    Studio again(&store, false);
    prepare(again);
    QVERIFY(QMetaObject::invokeMethod(&again, "copyQuick"));
    QTRY_COMPARE_WITH_TIMEOUT(again.quickState(), QString("copied"), 15000);
    QCOMPARE(again.finishNotice().image, firstPreview);
    QCOMPARE(QDir(QFileInfo(firstPreview).absolutePath()).entryList(QDir::Files).size(), 1);

    Studio saved(&store, false);
    prepare(saved);
    saved.chooseFinish(0);
    QTRY_COMPARE_WITH_TIMEOUT(saved.quickState(), QString("done"), 15000);
    const auto finished = saved.finishNotice();
    QCOMPARE(finished.summary, QString("Screenshot copied"));
    QCOMPARE(finished.body,
             "Saved in " + QFileInfo(saved.savedPath()).absolutePath().replace(QDir::homePath(), "~"));
    QCOMPARE(finished.image, saved.savedPath());

    Studio cancelled(&store, false);
    prepare(cancelled);
    cancelled.dismissQuick();
    QVERIFY(cancelled.finishNotice().summary.isEmpty());
  }
  void copyWithoutNotificationDoesNotWriteAPreview_data() {
    QTest::addColumn<bool>("enabled");
    QTest::addColumn<bool>("available");
    QTest::newRow("disabled") << false << true;
    QTest::newRow("unavailable") << true << false;
    QTest::newRow("disabled-and-unavailable") << false << false;
  }
  void copyWithoutNotificationDoesNotWriteAPreview() {
    QFETCH(bool, enabled);
    QFETCH(bool, available);
    const QString helper = temp.filePath("bin/notify-send");
    if (!available)
      QVERIFY(QFile::remove(helper));
    ImageStore store;
    Studio studio(&store, false);
    prepare(studio);
    studio.setNotifications(enabled);
    const auto runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    const auto before = filesUnder(runtime);
    studio.copyQuick();
    QTRY_COMPARE_WITH_TIMEOUT(studio.quickState(), QString("copied"), 15000);
    QVERIFY(studio.finishNotice().image.isEmpty());
    QCOMPARE(filesUnder(runtime), before);
    if (!available)
      QVERIFY(QFile::link(QCoreApplication::applicationFilePath(), helper));
  }
  void copiesRawPixelsWithoutSaving_data() {
    QTest::addColumn<bool>("keepOriginals");
    QTest::addColumn<bool>("automatic");
    for (bool keepOriginals : {false, true})
      for (bool automatic : {false, true})
        QTest::newRow(qPrintable(QString("originals-%1-automatic-%2").arg(keepOriginals).arg(automatic)))
            << keepOriginals << automatic;
  }
  void copiesRawPixelsWithoutSaving() {
    QFETCH(bool, keepOriginals);
    QFETCH(bool, automatic);
    ImageStore store;
    Studio studio(&store, false);
    prepare(studio);
    studio.setStyle(8); // Raw: the copy is exactly the source pixels.
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 15000);
    studio.setKeepOriginals(keepOriginals);
    if (automatic)
      studio.setAutoSaveScreenshots(false);
    QSettings().sync();
    const auto before = filesUnder(QDir::homePath());
    QSignalSpy dismissed(&studio, &Studio::dismissRequested);
    if (automatic) {
      studio.chooseFinish(8);
    } else
      studio.copyQuick();
    QTRY_COMPARE_WITH_TIMEOUT(dismissed.count(), 1, 15000);
    QCOMPARE(studio.quickState(), QString("copied"));
    QVERIFY(!studio.busy());
    QVERIFY(studio.savedPath().isEmpty());
    QVERIFY(studio.recoveryAction().isEmpty());
    const QByteArray png = contents(clipboard());
    QVERIFY(png.startsWith("\x89PNG\r\n\x1a\n"));
    const QImage copied = QImage::fromData(png, "png");
    QCOMPARE(copied.size(), captureImage().size());
    const QImage source = captureImage();
    for (int y = 0; y < copied.height(); ++y)
      for (int x = 0; x < copied.width(); ++x)
        QCOMPARE(copied.pixelColor(x, y), source.pixelColor(x, y));
    QVERIFY(copied.textKeys().isEmpty());
    QVERIFY(!copied.colorSpace().isValid());
    studio.saveDraftNow(); // The real main also flushes drafts at shutdown.
    QTest::qWait(350);
    QVERIFY(studio.drafts().isEmpty());
    QCOMPARE(studio.originalsCount(), 0);
    QVERIFY(!QDir(studio.outputDirectory()).exists());
    QCOMPARE(filesUnder(QDir::homePath()), before);
  }
};

int main(int argc, char **argv) {
  if (QFileInfo(QString::fromLocal8Bit(argv[0])).fileName() == "tesseract") {
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly))
      return 2;
    const QImage image = QImage::fromData(input.readAll());
    if (qEnvironmentVariableIsSet("COPY_TEST_TEXT")) {
      if (image.isNull()) return 3;
      const QString marker = qEnvironmentVariable("COPY_TEST_FIRST_READ_FAIL");
      if (!marker.isEmpty()) {
        QFile first(marker);
        if (first.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
          first.close();
          QElapsedTimer waiting;
          waiting.start();
          while (!QFileInfo::exists(marker + ".release") && waiting.elapsed() < 5000)
            QThread::msleep(10);
          return 1;
        }
      }
      QThread::msleep(marker.isEmpty() ? 200 : 1500);
      if (image.height() > 110 && qGray(image.pixel(40, 40)) > 80)
        std::fputs("5\t1\t1\t1\t1\t1\t20\t20\t200\t40\t99\t"
                   "ghp_R8x2KqLm4Vn7Pz9Wt3Ys6Bd1Fh5Jc0Ae2Gk\n", stdout);
      std::fprintf(stdout, "5\t1\t1\t1\t2\t1\t20\t%d\t200\t40\t99\tPublic status\n",
                   image.height() > 110 ? 120 : 10);
      return 0;
    }
    if (qEnvironmentVariableIsSet("COPY_TEST_SECRET"))
      std::fputs("5\t1\t1\t1\t1\t1\t10\t10\t100\t20\t99\t"
                 "ghp_R8x2KqLm4Vn7Pz9Wt3Ys6Bd1Fh5Jc0Ae2Gk\n", stdout);
    return 0;
  }
  if (argc > 1 && QByteArray(argv[1]) == "--type")
    return clipboardStub(argc, argv);
  QTemporaryDir home, runtime;
  if (!home.isValid() || !runtime.isValid())
    return 2;
  qputenv("XDG_RUNTIME_DIR", QFile::encodeName(runtime.path()));
  qputenv("HOME", QFile::encodeName(home.path()));
  qputenv("XDG_CONFIG_HOME", QFile::encodeName(home.filePath("config")));
  qputenv("XDG_DATA_HOME", QFile::encodeName(home.filePath("data")));
  qputenv("XDG_CACHE_HOME", QFile::encodeName(home.filePath("cache")));
  qputenv("QT_QPA_PLATFORM", "offscreen");
  qputenv("QT_QUICK_BACKEND", "software");
  // Desktop platform themes such as gtk3 try to open a display.
  qunsetenv("QT_QPA_PLATFORMTHEME");
  qunsetenv("WAYLAND_DISPLAY");
  qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
  QGuiApplication app(argc, argv);
  QCoreApplication::setOrganizationName("Omaframe-test");
  QCoreApplication::setApplicationName("QuickCopy");
  QuickCopyTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "quick-copy-test.moc"
