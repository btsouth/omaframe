#include "capture-dismissal.hpp"
#include "capture-request.hpp"
#include "delay-capture.hpp"
#include "navigation.hpp"
#include "omarchy-theme.hpp"
#include "recording.hpp"
#include "shortcuts.hpp"
#include "studio.hpp"
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
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
#include <QUuid>

struct FakeCompositor {
  using Reply = CaptureDismissal::Boundary::Reply;
  struct Call {
    QStringList args;
    std::function<void(Reply)> done;
    bool stopped = false;
  };
  QList<std::shared_ptr<Call>> calls;
  CaptureDismissal::Boundary::Transport transport() {
    return [this](const QStringList &args, auto done) {
      auto call = std::make_shared<Call>(Call{args, done});
      calls.append(call);
      return [call] { call->stopped = true; };
    };
  }
  void reply(int index, bool success = true, QByteArray output = "ok") {
    calls.at(index)->done({success, output});
  }
};

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
    QTest::newRow("capture")
        << QStringList{"--capture", "--delay", "30"} << true << 30;
    for (const auto *n : {"-1", "31", "1.5", "abc", "999999999999", ""})
      QTest::newRow(n) << QStringList{"--delay", n} << false << -1;
    for (const auto *mode :
         {"studio", "screen", "repeat", "scroll", "record", "stop-recording",
          "pause-recording", "resume-recording", "toggle-recording-pause"})
      QTest::newRow(mode) << QStringList{"--" + QString(mode), "--delay", "0"}
                          << false << -1;
    QTest::newRow("file") << QStringList{"--delay", "3", "picture.png"} << false
                          << -1;
    QTest::newRow("missing") << QStringList{"--delay"} << false << -1;
  }
  void cli() {
    QFETCH(QStringList, args);
    QFETCH(bool, valid);
    QFETCH(int, seconds);
    QCommandLineParser parser;
    parser.addOption({"delay", "Delay", "N"});
    for (const auto *mode : {"capture", "studio", "screen", "repeat", "scroll",
                             "record", "stop-recording", "pause-recording",
                             "resume-recording", "toggle-recording-pause"})
      parser.addOption({mode, mode});
    int actual;
    QString error;
    const bool result = parser.parse(QStringList{"omaframe"} + args) &&
                        CaptureRequest::cliDelay(parser, actual, error);
    QCOMPARE(result, valid);
    if (valid)
      QCOMPARE(actual, seconds);
  }
  void ipcContractAndPriority() {
    using namespace CaptureRequest;
    QString cmd;
    int seconds;
    QVERIFY(
        decode({{"command", "capture"}, {"delaySeconds", 5}}, cmd, seconds));
    QCOMPARE(seconds, 5);
    QVERIFY(decode({{"command", "capture"}}, cmd, seconds));
    QCOMPARE(seconds, -1);
    for (const auto &invalid : {QJsonValue("3"), QJsonValue(1.5),
                                QJsonValue(31), QJsonValue(-2), QJsonValue()})
      QVERIFY(!decode({{"command", "capture"}, {"delaySeconds", invalid}}, cmd,
                      seconds));
    QVERIFY(
        !decode({{"command", "repeat"}, {"delaySeconds", 3}}, cmd, seconds));
    QVERIFY(!decode({{"command", "bogus"}}, cmd, seconds));
    for (const auto *request : {"capture", "screen", "repeat"})
      QCOMPARE(handle(request, true, true), Handling::Cancel);
    for (const auto *request : {"scroll", "record", "open", "studio"})
      QCOMPARE(handle(request, true, true), Handling::Busy);
    QCOMPARE(handle("capture", false, true), Handling::Busy);
    QCOMPARE(handle("capture", false, false), Handling::Proceed);
  }
  void recordingPriorityDispatch_data() {
    QTest::addColumn<QString>("command");
    QTest::addColumn<bool>("active");
    QTest::addColumn<QString>("expected");
    for (bool active : {false, true})
      for (const auto *cmd :
           {"pause-recording", "resume-recording", "toggle-recording-pause",
            "stop-recording", "record", "capture", "scroll"}) {
        const QString expected =
            QString(cmd).contains("pause") || QString(cmd) == "resume-recording"
                ? "pause"
            : QString(cmd) == "stop-recording" ||
                    (QString(cmd) == "record" && active)
                ? "stop"
                : (QString(cmd) == "capture" ? "cancel" : "busy");
        QTest::newRow(qPrintable(QString(cmd) + (active ? "-active" : "-idle")))
            << QString(cmd) << active << expected;
      }
  }
  void recordingPriorityDispatch() {
    QFETCH(QString, command);
    QFETCH(bool, active);
    QFETCH(QString, expected);
    QStringList calls;
    // Same production dispatch boundary; controls must bypass delay and busy.
    if (!CaptureRequest::dispatchControl(
            command, active,
            [&](const QString &c) {
              QCOMPARE(c, command);
              calls << "pause";
            },
            [&] { calls << "stop"; })) {
      const auto result = CaptureRequest::handle(command, true, true);
      calls << (result == CaptureRequest::Handling::Cancel ? "cancel" : "busy");
    }
    QCOMPARE(calls, QStringList{expected});
  }
  void productionDesktopDismissalAndPlacement() {
    FakeCompositor fake;
    CaptureDismissal::Boundary boundary(nullptr, fake.transport());
    int hidden = 0, completed = 0, placed = 0;
    bool success = false;
    boundary.begin(
        7, nullptr, [&] { ++hidden; },
        [&](bool ok) {
          ++completed;
          success = ok;
        });
    QCOMPARE(fake.calls.size(), 1); // Recover before installing any rules.
    fake.reply(0);
    QCOMPARE(fake.calls.size(), 2);
    QVERIFY(fake.calls[1]->args[1].contains("^omaframe-(selection|finishes)$"));
    fake.reply(1);
    QCOMPARE(hidden, 0); // Wait for rule application, never hide synchronously.
    QTRY_COMPARE(hidden, 1);
    QTRY_COMPARE(fake.calls.size(), 3);
    QCOMPARE(fake.calls[2]->args, (QStringList{"-j", "layers"}));
    fake.reply(2, true, "{}");
    QCOMPARE(fake.calls[3]->args, (QStringList{"-j", "clients"}));
    fake.reply(3, true, "[]");
    QCOMPARE(completed, 0);
    QTRY_COMPARE(fake.calls.size(), 5);
    fake.reply(4);
    QCOMPARE(completed, 1);
    QVERIFY(success);
    boundary.placeBadge(7, [&](QScreen *screen) {
      QVERIFY(screen);
      ++placed;
    });
    QCOMPARE(placed, 0);
    fake.reply(5, true, R"({"x":0,"y":0})");
    QCOMPARE(placed, 1);
  }
  void productionBadgeDismissalRequiresRestore() {
    FakeCompositor fake;
    CaptureDismissal::Boundary boundary(nullptr, fake.transport());
    boundary.begin(1, nullptr, [] {}, [](bool) {});
    boundary.cancel([](bool) {});
    // Cancelled recovery is stopped; a new timer must restore first.
    boundary.begin(2, nullptr, [] {}, [](bool) {});
    fake.reply(1);
    fake.reply(2);
    fake.reply(3);
    QTRY_COMPARE(fake.calls.size(), 5);
    fake.reply(4, true, "{}");
    fake.reply(5, true, "[]");
    QTRY_COMPARE(fake.calls.size(), 7);
    fake.reply(6);
    QQuickWindow badge;
    badge.show();
    int done = 0;
    bool success = true;
    boundary.clear(2, &badge, [&](bool ok) {
      ++done;
      success = ok;
    });
    QCOMPARE(fake.calls.size(), 8);
    QVERIFY(badge.isVisible());
    fake.reply(7);
    QTRY_VERIFY(!badge.isVisible());
    QTRY_COMPARE(fake.calls.size(), 9);
    fake.reply(8, true, "{}");
    QCOMPARE(done, 0);
    QTRY_COMPARE(fake.calls.size(), 10);
    fake.reply(9, true, "error restoring rule");
    QCOMPARE(done, 1);
    QVERIFY(!success); // Production boundary cannot signal acquisition.
  }
  void productionFailures_data() {
    QTest::addColumn<int>("stage");
    QTest::addColumn<bool>("exitOk");
    QTest::addColumn<QByteArray>("output");
    QTest::newRow("startup-restore-exit") << 0 << false << QByteArray("ok");
    QTest::newRow("startup-restore-output") << 0 << true << QByteArray("error");
    QTest::newRow("prepare-exit") << 1 << false << QByteArray("ok");
    QTest::newRow("prepare-output") << 1 << true << QByteArray("error");
    QTest::newRow("layer-query-invalid") << 2 << true << QByteArray("broken");
    const QByteArray layer =
        QJsonDocument(
            QJsonObject{
                {"monitor",
                 QJsonObject{
                     {"levels",
                      QJsonObject{
                          {"3",
                           QJsonArray{QJsonObject{
                               {"pid", QCoreApplication::applicationPid()},
                               {"namespace", CaptureDismissal::scope}}}}}}}}})
            .toJson();
    QTest::newRow("layer-still-present") << 2 << true << layer;
    QTest::newRow("clients-invalid") << 3 << true << QByteArray("{}");
    QTest::newRow("final-restore-exit") << 4 << false << QByteArray("ok");
  }
  void productionFailures() {
    QFETCH(int, stage);
    QFETCH(bool, exitOk);
    QFETCH(QByteArray, output);
    FakeCompositor fake;
    CaptureDismissal::Boundary boundary(nullptr, fake.transport());
    int done = 0;
    bool success = true;
    boundary.begin(
        1, nullptr, [] {},
        [&](bool ok) {
          ++done;
          success = ok;
        });
    for (int i = 0; i <= stage; ++i) {
      QTRY_VERIFY(fake.calls.size() > i);
      fake.reply(i, i == stage ? exitOk : true,
                 i == stage ? output
                 : i == 2   ? "{}"
                 : i == 3   ? "[]"
                            : "ok");
    }
    if (stage > 0 && stage < 4) {
      QTRY_VERIFY(fake.calls.size() > stage + 1);
      fake.reply(stage + 1);
    }
    QTRY_COMPARE(done, 1);
    QVERIFY(!success);
  }
  void productionTimeouts_data() {
    QTest::addColumn<QString>("stage");
    for (const auto *stage :
         {"recover", "prepare", "layers", "restore", "cursor"})
      QTest::newRow(stage) << QString(stage);
  }
  void productionTimeouts() {
    QFETCH(QString, stage);
    FakeCompositor fake;
    CaptureDismissal::Boundary boundary(nullptr, fake.transport());
    int completed = 0;
    bool success = true;
    boundary.begin(
        1, nullptr, [] {},
        [&](bool ok) {
          ++completed;
          success = ok;
        });
    int timed = 0;
    if (stage != "recover") {
      fake.reply(0);
      timed = 1;
    }
    if (stage == "layers" || stage == "restore" || stage == "cursor") {
      fake.reply(1);
      QTRY_COMPARE(fake.calls.size(), 3);
      timed = 2;
    }
    if (stage == "restore" || stage == "cursor") {
      fake.reply(2, true, "{}");
      fake.reply(3, true, "[]");
      QTRY_COMPARE(fake.calls.size(), 5);
      timed = 4;
    }
    if (stage == "cursor") {
      fake.reply(4);
      boundary.placeBadge(1, [&](QScreen *screen) {
        QVERIFY(screen);
        ++completed;
      });
      timed = 5;
    }
    // The event loop keeps dispatching Cancel/IPC/control work while waiting.
    int heartbeat = 0;
    QTimer pulse;
    connect(&pulse, &QTimer::timeout, [&] { ++heartbeat; });
    pulse.start(10);
    QTRY_VERIFY_WITH_TIMEOUT(fake.calls[timed]->stopped, 1500);
    QVERIFY(heartbeat > 10);
    if (stage == "prepare" || stage == "layers")
      fake.reply(timed + 1); // Checked cleanup follows failed command.
    QCOMPARE(completed, stage == "cursor" ? 2 : 1);
    if (stage != "cursor")
      QVERIFY(!success);
    const int before = completed;
    fake.reply(timed); // Timeout's late success cannot run a continuation.
    QCOMPARE(completed, before);
  }
  void productionCancelMidPrepareAndLateReplies() {
    FakeCompositor fake;
    CaptureDismissal::Boundary boundary(nullptr, fake.transport());
    int hidden = 0, ready = 0, cancelled = 0;
    boundary.begin(1, nullptr, [&] { ++hidden; }, [&](bool) { ++ready; });
    fake.reply(0);
    QCOMPARE(fake.calls.size(), 2);
    boundary.cancel([&](bool ok) {
      QVERIFY(ok);
      ++cancelled;
    });
    QVERIFY(fake.calls[1]->stopped);
    QCOMPARE(fake.calls.size(), 3);
    QVERIFY(fake.calls[2]->args[1].contains("enabled = false"));
    fake.reply(1); // Delayed prepare reply after Cancel cannot hide the editor.
    fake.reply(2);
    QCOMPARE(cancelled, 1);
    QTest::qWait(80);
    QCOMPARE(hidden, 0);
    QCOMPARE(ready, 0);
    QCOMPARE(fake.calls.size(), 3);
  }
  void productionCancelDuringPlacementAndRestore() {
    FakeCompositor fake;
    CaptureDismissal::Boundary boundary(nullptr, fake.transport());
    boundary.begin(1, nullptr, [] {}, [](bool) {});
    fake.reply(0);
    fake.reply(1);
    QTRY_COMPARE(fake.calls.size(), 3);
    fake.reply(2, true, "{}");
    fake.reply(3, true, "[]");
    QTRY_COMPARE(fake.calls.size(), 5);
    fake.reply(4);
    int mapped = 0, cancelled = 0;
    boundary.placeBadge(1, [&](QScreen *) { ++mapped; });
    boundary.cancel([&](bool ok) {
      QVERIFY(!ok);
      ++cancelled;
    });
    QVERIFY(fake.calls[5]->stopped);
    fake.reply(5, true, R"({"x":0,"y":0})");
    boundary.placeBadge(1, [&](QScreen *) { ++mapped; });
    fake.reply(6, false);
    QCOMPARE(mapped, 0);
    QCOMPARE(cancelled, 1); // Cancel restoration errors are reported.
  }
  void cancelledCleanupSerializesCapture_data() {
    QTest::addColumn<bool>("timeout");
    QTest::addColumn<bool>("delayed");
    QTest::newRow("immediate-failed-restore") << false << false;
    QTest::newRow("immediate-timed-out-restore") << true << false;
    QTest::newRow("delayed-failed-restore") << false << true;
    QTest::newRow("delayed-timed-out-restore") << true << true;
  }
  void cancelledCleanupSerializesCapture() {
    QFETCH(bool, timeout);
    QFETCH(bool, delayed);
    ImageStore store;
    Studio studio(&store, false);
    FakeCompositor fake;
    CaptureDismissal::Boundary boundary(nullptr, fake.transport());
    QQuickWindow editor;
    editor.setTitle("Omaframe");
    editor.show();
    int cleanup = 0;
    std::atomic_int grabs = 0;
    studio.m_captureGrab = [&](const QString &, QImage &image, QString &) {
      ++grabs;
      image = QImage(40, 30, QImage::Format_RGB32);
      image.fill(Qt::blue);
      return true;
    };
    connect(&studio, &Studio::hideStudio, &editor, &QQuickWindow::hide);
    connect(&studio, &Studio::delayFailed, &editor, &QQuickWindow::show);
    connect(&studio, &Studio::delayHideRequested, &studio,
            [&](quint64 generation) {
              boundary.begin(generation, nullptr, [&] { editor.hide(); },
                             [&, generation](bool ok) {
                               studio.delayDesktopCleared(generation, ok);
                             });
            });
    connect(&studio, &Studio::delayCancelled, &studio, [&] {
      editor.hide();
      studio.leaveQuickMode();
      const auto request = studio.beginDelayCleanup();
      boundary.cancel([&, request](bool restored) {
        ++cleanup;
        studio.finishDelayCleanup(request, restored);
      });
    });
    QSignalSpy ready(&studio, &Studio::selectionReady);
    QSignalSpy failed(&studio, &Studio::delayFailed);
    QSignalSpy hiding(&studio, &Studio::delayHideRequested);
    studio.delayCapture(3);
    QCOMPARE(fake.calls.size(), 1);
    studio.cancelDelayedCapture();
    QCOMPARE(fake.calls.size(), 2);
    const auto cancelledRequest = studio.m_captureRequestGeneration;
    if (delayed)
      studio.delayCapture(3);
    else
      studio.capture(true);
    QVERIFY(!studio.currentCaptureRequest(cancelledRequest));
    QVERIFY(studio.busy());
    QTest::qWait(50);
    QCOMPARE(grabs.load(), 0);
    QCOMPARE(hiding.size(), 1);
    QCOMPARE(fake.calls.size(), 2);
    QVERIFY(!editor.isVisible());
    QTest::ignoreMessage(QtWarningMsg,
        "Could not restore Omaframe's dismissal animations after cancellation.");
    if (!timeout)
      fake.reply(1, false);
    QTRY_COMPARE(cleanup, 1); // The existing 500 ms deadline releases the wait.
    if (timeout)
      QVERIFY(fake.calls[1]->stopped);
    QCOMPARE(failed.size(), 0);
    QVERIFY(!editor.isVisible());
    QVERIFY(!studio.status().contains("Screenshot cancelled"));
    if (delayed) {
      QCOMPARE(hiding.size(), 2);
      QVERIFY(studio.delayedCapture());
      QCOMPARE(fake.calls.size(), 3); // Fresh delayed dismissal starts now.
    } else {
      QTRY_COMPARE(ready.size(), 1);
      QVERIFY(grabs.load() > 0);
      QCOMPARE(studio.quickState(), QString("selecting"));
      QCOMPARE(studio.m_frozen.constBegin().value().pixelColor(0, 0),
               QColor(Qt::blue));
    }
    fake.reply(0); // Late replies cannot map Studio into the new operation.
    fake.reply(1);
    QTest::qWait(50);
    QCOMPARE(cleanup, 1);
    QCOMPARE(failed.size(), 0);
    QVERIFY(!editor.isVisible());
    if (!delayed)
      QCOMPARE(ready.size(), 1);
  }
  void productionStartupCleanupOwnsOnlySavedRules() {
    FakeCompositor fake;
    CaptureDismissal::Boundary boundary(nullptr, fake.transport());
    int recovered = 0;
    boundary.recover([&](bool ok) {
      QVERIFY(ok);
      ++recovered;
    });
    const auto uuid =
        QSettings().value("screenshot/dismissalRuleId").toString();
    QVERIFY(!QUuid(uuid).isNull());
    const auto cleanup = fake.calls[0]->args[1];
    QVERIFY(cleanup.contains("omaframe-delay-" + uuid + "-window"));
    QVERIFY(cleanup.contains("omaframe-delay-" + uuid + "-selection"));
    QVERIFY(!cleanup.contains("countdown"));
    QVERIFY(!cleanup.contains("omaframe-capture-dismissal\""));
    fake.reply(0);
    QCOMPARE(recovered, 1);
    // Recreated after a crash: same UUID, same idempotent disable operation.
    FakeCompositor restarted;
    CaptureDismissal::Boundary next(nullptr, restarted.transport());
    next.recover([&](bool ok) {
      QVERIFY(ok);
      ++recovered;
    });
    QCOMPARE(restarted.calls[0]->args[1], cleanup);
    restarted.reply(0);
    QCOMPARE(recovered, 2);
  }
  void customDelayBindings_data() {
    QTest::addColumn<QString>("arg");
    QTest::addColumn<bool>("recognized");
    for (const auto *arg :
         {"omaframe --capture --delay 5", "omaframe --delay 5",
          "omaframe --delay 5 --capture", "omaframe --delay=10 --capture",
          "omaframe --capture --delayed-capture", "/opt/bin/omaframe --delay 3",
          "exec /opt/bin/omaframe --delay 3",
          "env TEST=1 omaframe --delay 5 --capture",
          "\"/opt/bin/omaframe\" --capture --delay 5",
          "'/opt/bin/omaframe' --delay 3"})
      QTest::newRow(arg) << QString(arg) << true;
    for (const auto *arg :
         {"omaframe --delay 31", "omaframe --delay -1", "omaframe --delay 1.5",
          "omaframe --screen --delay 5", "omaframe --record --delay 5",
          "omaframe --delay 5 photo.png", "omaframe --delay",
          "omaframe --capture", "other --delay 5", "echo omaframe --delay 5"})
      QTest::newRow(arg) << QString(arg) << false;
  }
  void customDelayBindings() {
    QFETCH(QString, arg);
    QFETCH(bool, recognized);
    QJsonObject bind{{"key", "s"},
                     {"modmask", 64 | 4},
                     {"dispatcher", "exec"},
                     {"arg", arg}};
    QCOMPARE(Shortcuts::runsOmaframe(bind, Shortcuts::Action::Delay),
             recognized);
    QCOMPARE(Shortcuts::omaframeKey({bind}, Shortcuts::Action::Delay),
             recognized ? QString("Super+Ctrl+S") : QString());
  }
  void navigationKeepsRequestLocalDelay() {
    Navigation nav;
    nav.setDirty(true);
    QSignalSpy proceed(&nav, &Navigation::proceed);
    nav.request("capture", {}, 10);
    nav.request("capture", {}, 3);
    nav.save();
    nav.saveFailed();
    nav.save();
    nav.saveSucceeded();
    QCOMPARE(proceed.size(), 1);
    QCOMPARE(proceed.first().at(2).toInt(), 10);
    nav.setDirty(false);
    nav.request("capture");
    QCOMPARE(proceed.last().at(2).toInt(), -1);
    nav.setDirty(true);
    nav.request("capture", {}, 5);
    nav.cancel();
    nav.request("capture", {}, 3);
    nav.discard();
    QCOMPARE(proceed.last().at(2).toInt(), 3);
  }
  void deadlineStartsAfterHideAndNeverGrabsEarly() {
    qint64 now = 0;
    DelayCapture delay(nullptr, [&] { return now; });
    QSignalSpy badge(&delay, &DelayCapture::badgeRequested);
    QSignalSpy clear(&delay, &DelayCapture::clearRequested);
    QSignalSpy grab(&delay, &DelayCapture::grabRequested);
    delay.begin(3);
    const auto gen = delay.generation();
    now = 9000;
    delay.tick();
    QCOMPARE(badge.size(), 0);
    QCOMPARE(grab.size(), 0);
    delay.desktopCleared(gen, true);
    QCOMPARE(delay.remaining(), 3);
    now = 11999;
    delay.tick();
    QCOMPARE(delay.remaining(), 1);
    QCOMPARE(clear.size(), 0);
    now = 12000;
    delay.tick();
    QCOMPARE(clear.size(), 1);
    QCOMPARE(grab.size(), 0);
    delay.badgeCleared(gen, true);
    QCOMPARE(grab.size(), 1);
    delay.badgeCleared(gen, true);
    QCOMPARE(grab.size(), 1);
    delay.complete(gen);
    QVERIFY(!delay.active());
  }
  void cancellationAtEveryPhase_data() {
    QTest::addColumn<int>("phase");
    for (int i = 0; i < 4; ++i)
      QTest::newRow(qPrintable(QString::number(i))) << i;
  }
  void cancellationAtEveryPhase() {
    QFETCH(int, phase);
    qint64 now = 0;
    DelayCapture delay(nullptr, [&] { return now; });
    QSignalSpy grab(&delay, &DelayCapture::grabRequested);
    delay.begin(3);
    const auto gen = delay.generation();
    if (phase >= 1)
      delay.desktopCleared(gen, true);
    if (phase >= 2) {
      now = 3000;
      delay.tick();
    }
    if (phase >= 3)
      delay.badgeCleared(gen, true);
    delay.cancel();
    QVERIFY(!delay.current(gen));
    QVERIFY(!delay.active());
    const int count = grab.size();
    delay.desktopCleared(gen, true);
    delay.badgeCleared(gen, true);
    delay.tick();
    delay.complete(gen);
    QCOMPARE(grab.size(), count);
    delay.begin(5);
    QVERIFY(delay.current(delay.generation()));
    QVERIFY(!delay.current(gen));
    delay.badgeCleared(gen, true);
    QCOMPARE(grab.size(), count);
  }
  void dismissalFailureNeverGrabs() {
    qint64 now = 0;
    DelayCapture delay(nullptr, [&] { return now; });
    QSignalSpy grab(&delay, &DelayCapture::grabRequested),
        failed(&delay, &DelayCapture::failed);
    delay.begin(3);
    delay.desktopCleared(delay.generation(), false);
    QVERIFY(!delay.active());
    QCOMPARE(failed.size(), 1);
    delay.begin(3);
    const auto gen = delay.generation();
    delay.desktopCleared(gen, true);
    now = 3000;
    delay.tick();
    delay.badgeCleared(gen, false);
    QCOMPARE(grab.size(), 0);
    QCOMPARE(failed.size(), 2);
  }
  void persistenceAndOrigin() {
    ImageStore store;
    Studio s(&store, false);
    QCOMPARE(s.delaySeconds(), 3);
    s.setDelaySeconds(5);
    s.setDelaySeconds(4);
    QCOMPARE(s.delaySeconds(), 5);
    Studio next(&store, false);
    QCOMPARE(next.delaySeconds(), 5);
    QSettings().setValue("record/countdown", 10);
    next.delayCapture(7);
    QVERIFY(next.delayedCapture());
    QCOMPARE(next.delaySeconds(), 5);
    QCOMPARE(QSettings().value("record/countdown").toInt(), 10);
    next.cancelDelayedCapture();
    QVERIFY(!next.busy());
    QVERIFY(!next.takeReturnToStudio());
    next.leaveQuickMode();
    next.loadDemo();
    next.marks()->edit("arrow", 0.1, 0.2, 0.7, 0.8);
    const auto original = next.m_original;
    const auto edits = next.marks()->edits();
    QQuickWindow editor;
    editor.setTitle("Omaframe");
    editor.show();
    next.delayCapture();
    next.cancelDelayedCapture();
    QVERIFY(next.takeReturnToStudio());
    QCOMPARE(next.m_original, original);
    QCOMPARE(next.marks()->edits(), edits);
    QVERIFY(next.marks()->canUndo());
    QCOMPARE(next.delaySeconds(), 5);
    QSettings().setValue("screenshot/delaySeconds", 30);
    Studio invalid(&store, false);
    QCOMPARE(invalid.delaySeconds(), 3);
  }
  void restorationFailureHasVisibleStudioStatus() {
    ImageStore store;
    Studio studio(&store, false);
    QSettings().setValue("notifications", false);
    studio.delayCapture(3);
    studio.delayDesktopCleared(studio.delayGeneration(), false);
    QVERIFY(!studio.delayedCapture());
    QVERIFY(studio.status().contains("Screenshot cancelled"));
    QVERIFY(studio.status().contains("restore Omaframe's animations"));
  }
  void staleWorkerResultIsDiscardedAndFreshPixelsSelected() {
    ImageStore store;
    Studio s(&store, false);
    const auto originStatus = s.status();
    qint64 now = 0;
    s.m_delay.m_clock = [&] { return now; };
    QSemaphore entered, release;
    s.m_captureGrab = [&](const QString &, QImage &image, QString &) {
      entered.release();
      release.acquire();
      image = QImage(40, 30, QImage::Format_RGB32);
      image.fill(Qt::green);
      return true;
    };
    QSignalSpy ready(&s, &Studio::selectionReady);
    s.delayCapture(3);
    auto gen = s.m_delay.generation();
    s.delayDesktopCleared(gen, true);
    now = 3000;
    s.m_delay.tick();
    s.delayBadgeCleared(gen, true);
    QTRY_VERIFY(entered.available() > 0);
    s.cancelDelayedCapture();
    QCOMPARE(s.status(), originStatus);
    release.release();
    QTRY_VERIFY(QThreadPool::globalInstance()->activeThreadCount() == 0);
    QCoreApplication::processEvents();
    QCOMPARE(ready.size(), 0);
    QVERIFY(s.m_frozen.isEmpty());
    s.leaveQuickMode();
    QColor fresh = Qt::red;
    s.m_captureGrab = [&](const QString &, QImage &image, QString &) {
      image = QImage(40, 30, QImage::Format_RGB32);
      image.fill(fresh);
      return true;
    };
    s.delayCapture(3);
    gen = s.m_delay.generation();
    s.delayDesktopCleared(gen, true);
    fresh = Qt::blue;
    now += 3000;
    s.m_delay.tick();
    s.delayBadgeCleared(gen, true);
    QTRY_COMPARE(ready.size(), 1);
    QCOMPARE(s.m_frozen.constBegin().value().pixelColor(0, 0),
             QColor(Qt::blue));
    QVERIFY(!s.delayedCapture());
    QVERIFY(s.savedPath().isEmpty());
  }
  void shortcutSetupAndRegeneration() {
    using Shortcuts::Action;
    const QJsonObject bind{{"key", "Print"},
                           {"modmask", 1},
                           {"dispatcher", "exec"},
                           {"arg", "omaframe --delayed-capture"}};
    QCOMPARE(Shortcuts::omaframeKey({bind}, Action::Delay),
             QString("Shift+Print"));
    QVERIFY(!Shortcuts::runsOmaframe(bind, Action::Screenshot));
    QCOMPARE(Shortcuts::defaultKeyState({bind}, Action::Delay),
             QString("omaframe"));
    const QString config = temp.filePath("hypr");
    QVERIFY(QDir().mkpath(config));
    QFile startup(config + "/hyprland.lua");
    QVERIFY(startup.open(QIODevice::WriteOnly));
    startup.write("require('hypr.bindings')\n");
    startup.close();
    QFile bindings(config + "/bindings.lua");
    QVERIFY(bindings.open(QIODevice::WriteOnly));
    bindings.write("-- User keys\n");
    bindings.close();
    QString error, backup;
    QVERIFY(Shortcuts::install(config, "/bin/true",
                               {Action::Screenshot, Action::Delay}, &error));
    QVERIFY(Shortcuts::install(config, "/bin/true", {Action::Pause}, &error));
    QVERIFY(bindings.open(QIODevice::ReadOnly));
    auto all = bindings.readAll();
    bindings.close();
    QVERIFY(all.contains("SHIFT + PRINT"));
    QVERIFY(all.contains("--delayed-capture"));
    QVERIFY(all.contains("Screenshot with Omaframe"));
    QVERIFY(all.contains("ALT + SHIFT + PRINT"));
    QVERIFY(Shortcuts::install(config, "/bin/true", {Action::Delay}, &error,
                               &backup));
    QVERIFY(backup.isEmpty());
    const QByteArray custom = "o.bind('SHIFT + PRINT', 'Custom', 'other')\n";
    QVERIFY(bindings.open(QIODevice::Append));
    bindings.write(custom);
    bindings.close();
    QVERIFY(!Shortcuts::install(config, "/bin/true", {Action::Delay}, &error));
    QVERIFY(error.contains("custom binding"));
    QVERIFY(Shortcuts::install(config, "/bin/true", {Action::Record}, &error));
    QVERIFY(bindings.open(QIODevice::ReadOnly));
    all = bindings.readAll();
    bindings.close();
    QVERIFY(all.contains(custom));
    QVERIFY(!all.contains("Delayed screenshot with Omaframe"));
    QVERIFY(all.contains("Screenshot with Omaframe"));
  }
  void selectionDelayRules() {
    QQmlEngine engine;
    auto *store = new ImageStore;
    engine.addImageProvider("frames", store);
    Studio studio(store, false);
    Recorder recorder;
    ShortcutSetup shortcuts;
    OmarchyTheme theme(nullptr, temp.filePath("theme"),
                       temp.filePath("theme-config"), false);
    engine.rootContext()->setContextProperty("studio", &studio);
    engine.rootContext()->setContextProperty("recorder", &recorder);
    engine.rootContext()->setContextProperty("shortcuts", &shortcuts);
    engine.rootContext()->setContextProperty("theme", &theme);
    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QFINDTESTDATA("../qml/Selection.qml")));
    std::unique_ptr<QObject> selector(component.create());
    QVERIFY2(selector, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(selector.get());
    QVERIFY(window);
    auto *key = selector->findChild<QObject *>("screenshotDelayKey");
    QVERIFY(key);
    auto *menu = selector->findChild<QObject *>("screenshotDelayMenu");
    QVERIFY(menu);
    QCOMPARE(key->property("autoRepeat").toBool(), false);
    studio.m_quickState = "selecting";
    studio.m_busy = true;
    studio.m_quickMode = true;
    emit studio.changed();
    window->resize(640, 480);
    window->show();
    QCoreApplication::processEvents();
    QVERIFY(key->property("enabled").toBool());
    studio.setCaptureBarHidden(true);
    QVERIFY(key->property("enabled").toBool());
    window->setProperty("dragging", true);
    QVERIFY(!key->property("enabled").toBool());
    window->setProperty("dragging", false);
    QMetaObject::invokeMethod(menu, "open");
    QCoreApplication::processEvents();
    QVERIFY(!key->property("enabled").toBool());
    QMetaObject::invokeMethod(menu, "close");
    studio.setRecordingSelection(true);
    QVERIFY(!key->property("enabled").toBool());
    studio.useScreenshotSelection();
    studio.scrollInstead();
    QVERIFY(!key->property("enabled").toBool());
    studio.useScreenshotSelection();
    QSignalSpy hide(&studio, &Studio::delayHideRequested);
    QMetaObject::invokeMethod(key, "activated");
    QCOMPARE(hide.size(), 1);
    studio.delayCapture();
    QCOMPARE(hide.size(), 1);
    studio.cancelDelayedCapture();
    QQmlComponent countdown(&engine, QUrl::fromLocalFile(QFINDTESTDATA(
                                         "../qml/CaptureCountdown.qml")));
    std::unique_ptr<QObject> badge(countdown.create());
    QVERIFY2(badge, qPrintable(countdown.errorString()));
    QVERIFY(badge->findChild<QObject *>("cancelScreenshotDelay"));
  }
};
QTEST_MAIN(DelayTest)
#include "delay-test.moc"
