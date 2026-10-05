#include "audio-levels.hpp"
#include "omarchy-theme.hpp"
#include "recording.hpp"
#include "shortcuts.hpp"
#include "video.hpp"
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

class NoAudio final : public AudioLevelBackend {
public:
  bool reportUnavailable = true;
  quint64 currentGeneration = 0;
  void open(const AudioSnapshot &s, quint64 generation) override {
    currentGeneration = generation;
    if (!reportUnavailable) return;
    if (!s.mic.isEmpty()) emit unavailable(0, generation);
    if (!s.sound.isEmpty()) emit unavailable(1, generation);
  }
  void close() override {}
};
struct MeterScene {
  QTemporaryDir temp;
  OmarchyTheme theme{nullptr, temp.path(), temp.path(), false};
  Recorder recorder;
  Video video;
  ShortcutSetup shortcuts;
  NoAudio *backend = new NoAudio;
  AudioLevels levels{std::unique_ptr<NoAudio>(backend)};
  QQmlEngine engine;
  QQmlComponent component{&engine}, mockComponent{&engine};
  std::unique_ptr<QObject> mock;
  std::unique_ptr<QQuickWindow> window;
  bool create(bool control, bool selection = false) {
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("audioLevels", &levels);
    engine.rootContext()->setContextProperty("shortcuts", &shortcuts);
    engine.rootContext()->setContextProperty("video", &video);
    if (control) {
      mockComponent.setData(R"(import QtQml
QtObject {
 property string state: "recording"
 property string status: "Recording"
 property bool active: true
 property bool countdownOnly: false
 property bool canForceStop: false
 property bool pausePending: false
 property int remaining: 0
 property string elapsed: "00:42"
 property string stopKey: "Alt+Print"
 property string pauseKey: "Alt+Shift+Print"
 function stop() {}
 function togglePause() {}
})", QUrl());
      mock.reset(mockComponent.create());
      if (!mock) return false;
      engine.rootContext()->setContextProperty("recorder", mock.get());
      levels.configure({}, {"fixture-mic", "fixture-monitor", "Fixture mic", "Fixture output"}, "recording", true, true);
    } else engine.rootContext()->setContextProperty("recorder", &recorder);
    if (selection) {
      mockComponent.setData(R"(import QtQml
QtObject {
 property bool recordingSelection: true
 property bool scrollSelection: false
 property bool captureBarHidden: false
 property var windowTargets: []
 property string pointerMonitor: ""
 property int revision: 0
 function cancelSelection() {}
})", QUrl());
      mock.reset(mockComponent.create());
      if (!mock) return false;
      engine.rootContext()->setContextProperty("studio", mock.get());
      recorder.setDesktopAudio(true);
      recorder.setMicAudio(true);
      levels.configure({"fixture-mic", "fixture-monitor", "Fixture mic", "Fixture output"}, {}, "setup", true, true);
    }
    const auto path = selection ? QFINDTESTDATA("../qml/Selection.qml") : control ? QFINDTESTDATA("../qml/RecordingControl.qml") : QFINDTESTDATA("../qml/RecordSetup.qml");
    component.loadUrl(QUrl::fromLocalFile(path));
    window.reset(qobject_cast<QQuickWindow *>(component.create()));
    if (!window) { qWarning() << component.errors(); return false; }
    if (selection) window->resize(1920, 480);
    else if (!control) window->resize(620, 900);
    return true;
  }
};
class AudioMeterUiTest : public QObject {
  Q_OBJECT
private slots:
  void optionsLoads() {
    MeterScene scene; QVERIFY(scene.create(false));
    scene.window->show(); QTest::qWait(10);
    QVERIFY(scene.window->width() == 620);
  }
  void preflightFits_data() {
    QTest::addColumn<int>("width");
    QTest::newRow("normal") << 1920;
    QTest::newRow("narrow") << 640;
  }
  void preflightFits() {
    QFETCH(int, width);
    MeterScene scene; scene.backend->reportUnavailable = false;
    QVERIFY(scene.create(false, true));
    scene.window->resize(width, 480); scene.window->show(); QTest::qWait(10);
    auto *bar = scene.window->findChild<QQuickItem *>("captureBar"); QVERIFY(bar);
    QVERIFY(bar->x() >= 0); QVERIFY(bar->x() + bar->width() <= width);
    auto *row = scene.window->findChild<QQuickItem *>("captureBarRow"); QVERIFY(row);
    for (auto *item : row->childItems()) {
      if (!item->isVisible()) continue;
      const auto pos = item->mapToItem(bar, QPointF());
      QVERIFY2(pos.x() >= 0 && pos.x() + item->width() <= bar->width(), "A capture bar control was clipped");
    }
    for (const auto &name : {"soundToggle", "micToggle"}) {
      auto *toggle = scene.window->findChild<QQuickItem *>(name); QVERIFY(toggle);
      auto *rail = toggle->findChild<QQuickItem *>("toggleMeter"); QVERIFY(rail);
      auto *badge = toggle->findChild<QQuickItem *>("meterBadge"); QVERIFY(badge);
      QVERIFY(rail->isVisible()); QCOMPARE(rail->height(), 4.0);
      QVERIFY(rail->x() >= 0); QVERIFY(rail->x() + rail->width() <= toggle->width());
      QVERIFY(rail->y() + rail->height() <= toggle->height());
      const auto pos = toggle->mapToItem(bar, QPointF());
      QVERIFY(pos.x() >= 0); QVERIFY(pos.x() + toggle->width() <= bar->width());
      QVERIFY(badge->isVisible()); QCOMPARE(badge->property("text").toString(), QString("…"));
      QVERIFY(toggle->property("hint").toString().contains("Checking"));
      QVERIFY(toggle->property("meterDescription").toString().contains("Fixture"));
    }
    emit scene.backend->unavailable(0, scene.backend->currentGeneration);
    emit scene.backend->unavailable(1, scene.backend->currentGeneration);
    QTest::qWait(10);
    for (const auto &name : {"soundToggle", "micToggle"}) {
      auto *toggle = scene.window->findChild<QQuickItem *>(name);
      auto *badge = toggle->findChild<QQuickItem *>("meterBadge");
      QVERIFY(badge->isVisible()); QCOMPARE(badge->property("text").toString(), QString("!"));
      QVERIFY(toggle->property("hint").toString().contains("Unavailable"));
    }
    scene.window->hide(); QCOMPARE(scene.levels.sound()["state"].toString(), QString("Off"));
  }
  void controlFitsAndHintsHavePriority() {
    MeterScene scene; QVERIFY(scene.create(true));
    QCOMPARE(scene.window->size(), QSize(232, 96));
    scene.window->show(); QTest::qWait(10);
    auto *rows = scene.window->findChild<QQuickItem *>("meterRows");
    auto *hint = scene.window->findChild<QQuickItem *>("lowerHint");
    auto *plate = scene.window->findChild<QQuickItem *>("meterPlate");
    QVERIFY(rows); QVERIFY(hint); QVERIFY(plate); QVERIFY(rows->isVisible());
    QCOMPARE(plate->property("color").value<QColor>(), scene.theme.property("background").value<QColor>());
    QCOMPARE(plate->width(), 232.0);
    QVERIFY(plate->y() + plate->height() <= 96);
    QVERIFY(rows->y() + rows->height() <= plate->height());
    QCOMPARE(rows->childItems().size(), 2);
    for (auto *row : rows->childItems()) QCOMPARE(row->width(), 216.0);
    scene.mock->setProperty("pausePending", true);
    QVERIFY(!rows->isVisible()); QVERIFY(!plate->isVisible()); QCOMPARE(hint->property("text").toString(), QString("Waiting for recorder…"));
    scene.mock->setProperty("status", "Could not pause.");
    QCOMPARE(hint->property("text").toString(), QString("Could not pause."));
    scene.mock->setProperty("pausePending", false);
    scene.mock->setProperty("status", "Recording");
    QVERIFY(rows->isVisible());
    scene.window->hide();
    QCOMPARE(scene.levels.microphone()["state"].toString(), QString("Off"));
  }
};
int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  if (app.arguments().contains("--preview-control")) {
    MeterScene scene;
    if (!scene.create(true)) return 1;
    QQuickWindow background;
    background.setTitle("Meter light background");
    background.setColor(Qt::white);
    background.resize(640, 360);
    background.show();
    scene.window->show();
    return app.exec();
  }
  if (app.arguments().contains("--preview-bar")) {
    MeterScene scene;
    if (!scene.create(false, true)) return 1;
    scene.window->resize(app.arguments().contains("--narrow") ? 640 : 1920, 480);
    scene.window->show();
    return app.exec();
  }
  AudioMeterUiTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "audio-meter-ui-test.moc"
