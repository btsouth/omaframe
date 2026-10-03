#include "studio.hpp"
#include "omarchy-theme.hpp"
#include <QColorSpace>
#include <QDirIterator>
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
      QByteArray(argv[2]) != "image/png")
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
    // PATH contains only the clipboard stub, so this starts quick mode but
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
  }
  void chooserActions_data() {
    QTest::addColumn<QString>("action");
    for (const QString action : {"escape", "empty", "capture-error", "close", "background", "retry",
                                 "pick-then-escape", "click-then-escape", "enter-saves",
                                 "save-button", "clipboard-button"})
      QTest::newRow(qPrintable(action)) << action;
  }
  void chooserActions() {
    QFETCH(QString, action);
    QQmlEngine engine;
    auto *store = new ImageStore;
    engine.addImageProvider("frames", store);
    Studio studio(store, false);
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
    const auto button = [&](const char *name) {
      auto *item = chooser->findChild<QQuickItem *>(name);
      return item && item->isVisible() ? item : nullptr;
    };
    const auto pickedWithoutSaving = [&](int style) {
      // Picking a finish only selects it. Nothing closes, copies or saves.
      QTRY_COMPARE(studio.style(), style);
      QTest::qWait(200);
      QCOMPARE(dismissed.count(), 0);
      QVERIFY(chooser->isVisible());
      QCOMPARE(studio.quickState(), QString("choosing"));
      QVERIFY(!QFileInfo::exists(clipboard()));
      QVERIFY(!QDir(temp.filePath("output")).exists());
    };
    if (action == "close")
      chooser->close();
    else if (action == "background")
      QTest::mouseClick(chooser.get(), Qt::LeftButton, Qt::NoModifier, QPoint(2, 2));
    else if (action == "pick-then-escape") {
      QTest::keyClick(chooser.get(), Qt::Key_2);
      pickedWithoutSaving(1);
      QTest::keyClick(chooser.get(), Qt::Key_Escape);
    } else if (action == "click-then-escape") {
      // Repeater delegates are visual children only, so walk the item tree.
      QQuickItem *card = nullptr;
      std::function<void(QQuickItem *)> find = [&](QQuickItem *item) {
        if (item->objectName() == "finish3")
          card = item;
        for (auto *child : item->childItems())
          find(child);
      };
      find(chooser->contentItem());
      QVERIFY(card);
      QVERIFY(QMetaObject::invokeMethod(card, "clicked"));
      pickedWithoutSaving(3);
      QTest::keyClick(chooser.get(), Qt::Key_Escape);
    } else if (action == "enter-saves") {
      QTest::keyClick(chooser.get(), Qt::Key_2);
      pickedWithoutSaving(1);
      QTest::keyClick(chooser.get(), Qt::Key_Return);
    } else if (action == "clipboard-button") {
      QVERIFY2(button("clipboardButton"), "The chooser needs a visible Clipboard button.");
      QVERIFY(QMetaObject::invokeMethod(button("clipboardButton"), "clicked"));
    } else if (action == "save-button") {
      QVERIFY2(button("saveButton"), "The chooser needs a visible Save and copy button.");
      QVERIFY(QMetaObject::invokeMethod(button("saveButton"), "clicked"));
    } else {
      if (action == "retry")
        qputenv("COPY_TEST_FAIL", "1");
      QTest::keyClick(chooser.get(), Qt::Key_Escape);
    }
    if (action == "retry") {
      QTRY_VERIFY_WITH_TIMEOUT(!studio.busy(), 15000);
      QCOMPARE(dismissed.count(), 0);
      QCOMPARE(studio.quickState(), QString("copy-failed"));
      QVERIFY(chooser->isVisible());
      bool visibleError = false;
      for (auto *item : chooser->findChildren<QQuickItem *>())
        visibleError |= item->isVisible() && item->property("text").toString() == studio.status();
      QVERIFY2(visibleError, "The copy failure must be visible in the chooser.");
      qunsetenv("COPY_TEST_FAIL");
      QTest::keyClick(chooser.get(), Qt::Key_Escape);
    }
    QTRY_COMPARE_WITH_TIMEOUT(dismissed.count(), 1, 15000);
    const bool saved = action == "enter-saves" || action == "save-button";
    const bool copied = saved || action == "escape" || action == "retry" ||
                        action == "pick-then-escape" || action == "click-then-escape" ||
                        action == "clipboard-button";
    QCOMPARE(studio.quickState(),
             saved ? QString("done") : copied ? QString("copied") : QString("cancelled"));
    QCOMPARE(QFileInfo::exists(clipboard()), copied);
    QCOMPARE(QDir(temp.filePath("output")).exists(), saved);
    QCOMPARE(studio.savedPath().isEmpty(), !saved);
    if (saved)
      QCOMPARE(contents(clipboard()), contents(studio.savedPath()));
  }
  void escCopiesTheChosenFinish() {
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
    QVERIFY(studio.status().contains("Esc to retry"));
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
    QVERIFY(studio.status().contains("Esc to retry"));
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
    QCOMPARE(copied.body, QString("Saved in clipboard"));
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
  void copiesRawPixelsWithoutSaving_data() {
    QTest::addColumn<bool>("keepOriginals");
    QTest::newRow("no-private-backup") << false;
    QTest::newRow("private-backup-enabled") << true;
  }
  void copiesRawPixelsWithoutSaving() {
    QFETCH(bool, keepOriginals);
    ImageStore store;
    Studio studio(&store, false);
    prepare(studio);
    studio.setStyle(8); // Raw: the copy is exactly the source pixels.
    QTRY_VERIFY_WITH_TIMEOUT(!studio.rendering(), 15000);
    studio.setKeepOriginals(keepOriginals);
    QSettings().sync();
    const auto before = filesUnder(QDir::homePath());
    QSignalSpy dismissed(&studio, &Studio::dismissRequested);
    QVERIFY(QMetaObject::invokeMethod(&studio, "copyQuick"));
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
