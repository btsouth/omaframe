#include "capture-request.hpp"
#include "delay-capture.hpp"
#include "navigation.hpp"
#include "omarchy-theme.hpp"
#include "recording.hpp"
#include "shortcuts.hpp"
#include "studio.hpp"
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSemaphore>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class DelayTest : public QObject {
  Q_OBJECT
  QTemporaryDir temp;
private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName("OmaframeDelayTests");
    QCoreApplication::setApplicationName("OmaframeDelayTests");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
  }
  void init() { QSettings().clear(); }
  void cli_data() {
    QTest::addColumn<QStringList>("args");
    QTest::addColumn<bool>("valid");
    QTest::addColumn<int>("seconds");
    QTest::newRow("normal") << QStringList{} << true << -1;
    QTest::newRow("zero") << QStringList{"--delay", "0"} << true << 0;
    QTest::newRow("capture") << QStringList{"--capture", "--delay", "30"} << true << 30;
    for (const auto *n : {"-1", "31", "1.5", "abc", "999999999999", ""})
      QTest::newRow(n) << QStringList{"--delay", n} << false << -1;
    for (const auto *mode : {"studio", "screen", "repeat", "scroll", "record",
                            "stop-recording", "pause-recording", "resume-recording",
                            "toggle-recording-pause"})
      QTest::newRow(mode) << QStringList{"--" + QString(mode), "--delay", "0"} << false << -1;
    QTest::newRow("file") << QStringList{"--delay", "3", "picture.png"} << false << -1;
    QTest::newRow("missing") << QStringList{"--delay"} << false << -1;
  }
  void cli() {
    QFETCH(QStringList, args); QFETCH(bool, valid); QFETCH(int, seconds);
    QCommandLineParser parser;
    parser.addOption({"delay", "Delay", "N"});
    for (const auto *mode : {"capture", "studio", "screen", "repeat", "scroll", "record",
                            "stop-recording", "pause-recording", "resume-recording",
                            "toggle-recording-pause"}) parser.addOption({mode, mode});
    int actual; QString error;
    const bool result = parser.parse(QStringList{"omaframe"} + args) &&
                        CaptureRequest::cliDelay(parser, actual, error);
    QCOMPARE(result, valid);
    if (valid) QCOMPARE(actual, seconds);
  }
  void ipcContractAndPriority() {
    using namespace CaptureRequest;
    QString cmd; int seconds;
    QVERIFY(decode({{"command", "capture"}, {"delaySeconds", 5}}, cmd, seconds));
    QCOMPARE(seconds, 5);
    QVERIFY(decode({{"command", "capture"}}, cmd, seconds)); QCOMPARE(seconds, -1);
    for (const auto &invalid : {QJsonValue("3"), QJsonValue(1.5), QJsonValue(31), QJsonValue(-2), QJsonValue()})
      QVERIFY(!decode({{"command", "capture"}, {"delaySeconds", invalid}}, cmd, seconds));
    QVERIFY(!decode({{"command", "repeat"}, {"delaySeconds", 3}}, cmd, seconds));
    QVERIFY(!decode({{"command", "bogus"}}, cmd, seconds));
    for (const auto *request : {"capture", "screen", "repeat"})
      QCOMPARE(handle(request, true, true), Handling::Cancel);
    for (const auto *request : {"scroll", "record", "open", "studio"})
      QCOMPARE(handle(request, true, true), Handling::Busy);
    QCOMPARE(handle("capture", false, true), Handling::Busy);
    QCOMPARE(handle("capture", false, false), Handling::Proceed);
    // Recording controls are dispatched before the delay gate in main.cpp.
    QFile main(QFINDTESTDATA("../src/main.cpp")); QVERIFY(main.open(QIODevice::ReadOnly));
    const auto code = main.readAll();
    QVERIFY(code.indexOf("const bool stopping") < code.indexOf("CaptureRequest::handle"));
    QVERIFY(code.indexOf("Recorder::pauseFinished, client") < code.indexOf("CaptureRequest::handle"));
  }
  void navigationKeepsRequestLocalDelay() {
    Navigation nav; nav.setDirty(true);
    QSignalSpy proceed(&nav, &Navigation::proceed);
    nav.request("capture", {}, 10);
    nav.request("capture", {}, 3);
    nav.save(); nav.saveFailed(); nav.save(); nav.saveSucceeded();
    QCOMPARE(proceed.size(), 1); QCOMPARE(proceed.first().at(2).toInt(), 10);
    nav.setDirty(false); nav.request("capture");
    QCOMPARE(proceed.last().at(2).toInt(), -1);
    nav.setDirty(true); nav.request("capture", {}, 5); nav.cancel();
    nav.request("capture", {}, 3); nav.discard();
    QCOMPARE(proceed.last().at(2).toInt(), 3);
  }
  void deadlineStartsAfterHideAndNeverGrabsEarly() {
    qint64 now = 0; DelayCapture delay(nullptr, [&] { return now; });
    QSignalSpy badge(&delay, &DelayCapture::badgeRequested);
    QSignalSpy clear(&delay, &DelayCapture::clearRequested);
    QSignalSpy grab(&delay, &DelayCapture::grabRequested);
    delay.begin(3); const auto gen = delay.generation();
    now = 9000; delay.tick(); QCOMPARE(badge.size(), 0); QCOMPARE(grab.size(), 0);
    delay.desktopCleared(gen, true); QCOMPARE(delay.remaining(), 3);
    now = 11999; delay.tick(); QCOMPARE(delay.remaining(), 1); QCOMPARE(clear.size(), 0);
    now = 12000; delay.tick(); QCOMPARE(clear.size(), 1); QCOMPARE(grab.size(), 0);
    delay.badgeCleared(gen, true); QCOMPARE(grab.size(), 1);
    delay.badgeCleared(gen, true); QCOMPARE(grab.size(), 1);
    delay.complete(gen); QVERIFY(!delay.active());
  }
  void cancellationAtEveryPhase_data() {
    QTest::addColumn<int>("phase");
    for (int i = 0; i < 4; ++i) QTest::newRow(qPrintable(QString::number(i))) << i;
  }
  void cancellationAtEveryPhase() {
    QFETCH(int, phase);
    qint64 now = 0; DelayCapture delay(nullptr, [&] { return now; });
    QSignalSpy grab(&delay, &DelayCapture::grabRequested);
    delay.begin(3); const auto gen = delay.generation();
    if (phase >= 1) delay.desktopCleared(gen, true);
    if (phase >= 2) { now = 3000; delay.tick(); }
    if (phase >= 3) delay.badgeCleared(gen, true);
    delay.cancel(); QVERIFY(!delay.current(gen)); QVERIFY(!delay.active());
    const int count = grab.size();
    delay.desktopCleared(gen, true); delay.badgeCleared(gen, true); delay.tick(); delay.complete(gen);
    QCOMPARE(grab.size(), count);
    delay.begin(5); QVERIFY(delay.current(delay.generation())); QVERIFY(!delay.current(gen));
    delay.badgeCleared(gen, true); QCOMPARE(grab.size(), count);
  }
  void dismissalFailureNeverGrabs() {
    qint64 now = 0; DelayCapture delay(nullptr, [&] { return now; });
    QSignalSpy grab(&delay, &DelayCapture::grabRequested), failed(&delay, &DelayCapture::failed);
    delay.begin(3); delay.desktopCleared(delay.generation(), false);
    QVERIFY(!delay.active()); QCOMPARE(failed.size(), 1);
    delay.begin(3); const auto gen = delay.generation(); delay.desktopCleared(gen, true);
    now = 3000; delay.tick(); delay.badgeCleared(gen, false);
    QCOMPARE(grab.size(), 0); QCOMPARE(failed.size(), 2);
  }
  void persistenceAndOrigin() {
    ImageStore store; Studio s(&store, false);
    QCOMPARE(s.delaySeconds(), 3); s.setDelaySeconds(5); s.setDelaySeconds(4);
    QCOMPARE(s.delaySeconds(), 5);
    Studio next(&store, false); QCOMPARE(next.delaySeconds(), 5);
    QSettings().setValue("record/countdown", 10);
    next.delayCapture(7); QVERIFY(next.delayedCapture());
    QCOMPARE(next.delaySeconds(), 5); QCOMPARE(QSettings().value("record/countdown").toInt(), 10);
    next.cancelDelayedCapture(); QVERIFY(!next.busy()); QVERIFY(!next.takeReturnToStudio());
    QQuickWindow editor; editor.setTitle("Omaframe"); editor.show();
    next.delayCapture(); next.cancelDelayedCapture(); QVERIFY(next.takeReturnToStudio());
    QCOMPARE(next.delaySeconds(), 5);
    QSettings().setValue("screenshot/delaySeconds", 30);
    Studio invalid(&store, false); QCOMPARE(invalid.delaySeconds(), 3);
  }
  void staleWorkerResultIsDiscardedAndFreshPixelsSelected() {
    ImageStore store; Studio s(&store, false);
    qint64 now = 0; s.m_delay.m_clock = [&] { return now; };
    QSemaphore entered, release;
    s.m_captureGrab = [&](const QString &, QImage &image, QString &) {
      entered.release(); release.acquire(); image = QImage(40, 30, QImage::Format_RGB32);
      image.fill(Qt::green); return true;
    };
    QSignalSpy ready(&s, &Studio::selectionReady);
    s.delayCapture(3); auto gen = s.m_delay.generation(); s.delayDesktopCleared(gen, true);
    now = 3000; s.m_delay.tick(); s.delayBadgeCleared(gen, true);
    QTRY_VERIFY(entered.available() > 0);
    s.cancelDelayedCapture(); release.release();
    QTRY_VERIFY(QThreadPool::globalInstance()->activeThreadCount() == 0);
    QCoreApplication::processEvents(); QCOMPARE(ready.size(), 0); QVERIFY(s.m_frozen.isEmpty());
    s.leaveQuickMode();
    QColor fresh = Qt::red;
    s.m_captureGrab = [&](const QString &, QImage &image, QString &) {
      image = QImage(40, 30, QImage::Format_RGB32); image.fill(fresh); return true;
    };
    s.delayCapture(3); gen = s.m_delay.generation(); s.delayDesktopCleared(gen, true);
    fresh = Qt::blue; now += 3000; s.m_delay.tick(); s.delayBadgeCleared(gen, true);
    QTRY_COMPARE(ready.size(), 1);
    QCOMPARE(s.m_frozen.constBegin().value().pixelColor(0, 0), QColor(Qt::blue));
    QVERIFY(!s.delayedCapture()); QVERIFY(s.savedPath().isEmpty());
  }
  void shortcutSetupAndRegeneration() {
    using Shortcuts::Action;
    const QJsonObject bind{{"key", "Print"}, {"modmask", 1}, {"dispatcher", "exec"},
                           {"arg", "omaframe --delayed-capture"}};
    QCOMPARE(Shortcuts::omaframeKey({bind}, Action::Delay), QString("Shift+Print"));
    QVERIFY(!Shortcuts::runsOmaframe(bind, Action::Screenshot));
    QCOMPARE(Shortcuts::defaultKeyState({bind}, Action::Delay), QString("omaframe"));
    const QString config = temp.filePath("hypr"); QVERIFY(QDir().mkpath(config));
    QFile startup(config + "/hyprland.lua"); QVERIFY(startup.open(QIODevice::WriteOnly));
    startup.write("require('hypr.bindings')\n"); startup.close();
    QFile bindings(config + "/bindings.lua"); QVERIFY(bindings.open(QIODevice::WriteOnly));
    bindings.write("-- User keys\n"); bindings.close();
    QString error, backup;
    QVERIFY(Shortcuts::install(config, "/bin/true", {Action::Screenshot, Action::Delay}, &error));
    QVERIFY(Shortcuts::install(config, "/bin/true", {Action::Pause}, &error));
    QVERIFY(bindings.open(QIODevice::ReadOnly)); auto all = bindings.readAll(); bindings.close();
    QVERIFY(all.contains("SHIFT + PRINT")); QVERIFY(all.contains("--delayed-capture"));
    QVERIFY(all.contains("Screenshot with Omaframe")); QVERIFY(all.contains("ALT + SHIFT + PRINT"));
    QVERIFY(Shortcuts::install(config, "/bin/true", {Action::Delay}, &error, &backup)); QVERIFY(backup.isEmpty());
    const QByteArray custom = "o.bind('SHIFT + PRINT', 'Custom', 'other')\n";
    QVERIFY(bindings.open(QIODevice::Append)); bindings.write(custom); bindings.close();
    QVERIFY(!Shortcuts::install(config, "/bin/true", {Action::Delay}, &error));
    QVERIFY(error.contains("custom binding"));
    QVERIFY(Shortcuts::install(config, "/bin/true", {Action::Record}, &error));
    QVERIFY(bindings.open(QIODevice::ReadOnly)); all = bindings.readAll(); bindings.close();
    QVERIFY(all.contains(custom)); QVERIFY(!all.contains("Delayed screenshot with Omaframe"));
    QVERIFY(all.contains("Screenshot with Omaframe"));
  }
  void selectionDelayRules() {
    QQmlEngine engine; auto *store = new ImageStore; engine.addImageProvider("frames", store);
    Studio studio(store, false); Recorder recorder; ShortcutSetup shortcuts;
    OmarchyTheme theme(nullptr, temp.filePath("theme"), temp.filePath("theme-config"), false);
    engine.rootContext()->setContextProperty("studio", &studio);
    engine.rootContext()->setContextProperty("recorder", &recorder);
    engine.rootContext()->setContextProperty("shortcuts", &shortcuts);
    engine.rootContext()->setContextProperty("theme", &theme);
    QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../qml/Selection.qml")));
    std::unique_ptr<QObject> selector(component.create()); QVERIFY2(selector, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(selector.get()); QVERIFY(window);
    auto *key = selector->findChild<QObject *>("screenshotDelayKey"); QVERIFY(key);
    auto *menu = selector->findChild<QObject *>("screenshotDelayMenu"); QVERIFY(menu);
    QCOMPARE(key->property("autoRepeat").toBool(), false);
    studio.m_quickState = "selecting"; studio.m_busy = true; studio.m_quickMode = true; emit studio.changed();
    window->resize(640, 480); window->show(); QCoreApplication::processEvents();
    QVERIFY(key->property("enabled").toBool());
    studio.setCaptureBarHidden(true); QVERIFY(key->property("enabled").toBool());
    window->setProperty("dragging", true); QVERIFY(!key->property("enabled").toBool());
    window->setProperty("dragging", false);
    QMetaObject::invokeMethod(menu, "open"); QCoreApplication::processEvents();
    QVERIFY(!key->property("enabled").toBool()); QMetaObject::invokeMethod(menu, "close");
    studio.setRecordingSelection(true); QVERIFY(!key->property("enabled").toBool());
    studio.useScreenshotSelection(); studio.scrollInstead(); QVERIFY(!key->property("enabled").toBool());
    studio.useScreenshotSelection();
    QSignalSpy hide(&studio, &Studio::delayHideRequested);
    QMetaObject::invokeMethod(key, "activated"); QCOMPARE(hide.size(), 1);
    studio.delayCapture(); QCOMPARE(hide.size(), 1); studio.cancelDelayedCapture();
    QQmlComponent countdown(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../qml/CaptureCountdown.qml")));
    std::unique_ptr<QObject> badge(countdown.create()); QVERIFY2(badge, qPrintable(countdown.errorString()));
    QVERIFY(badge->findChild<QObject *>("cancelScreenshotDelay"));
  }
};
QTEST_MAIN(DelayTest)
#include "delay-test.moc"
