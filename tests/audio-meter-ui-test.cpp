#include "audio-levels.hpp"
#include "omarchy-theme.hpp"
#include "recording.hpp"
#include "shortcuts.hpp"
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
  void open(const AudioSnapshot &s, quint64 generation) override {
    if (!s.mic.isEmpty()) emit unavailable(0, generation);
    if (!s.sound.isEmpty()) emit unavailable(1, generation);
  }
  void close() override {}
};
struct MeterScene {
  QTemporaryDir temp;
  OmarchyTheme theme{nullptr, temp.path(), temp.path(), false};
  Recorder recorder;
  ShortcutSetup shortcuts;
  AudioLevels levels{std::make_unique<NoAudio>()};
  QQmlEngine engine;
  QQmlComponent component{&engine}, mockComponent{&engine};
  std::unique_ptr<QObject> mock;
  std::unique_ptr<QQuickWindow> window;
  bool create(bool control) {
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("audioLevels", &levels);
    engine.rootContext()->setContextProperty("shortcuts", &shortcuts);
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
    const auto path = control ? QFINDTESTDATA("../qml/RecordingControl.qml") : QFINDTESTDATA("../qml/RecordSetup.qml");
    component.loadUrl(QUrl::fromLocalFile(path));
    window.reset(qobject_cast<QQuickWindow *>(component.create()));
    if (!window) { qWarning() << component.errors(); return false; }
    if (!control) window->resize(620, 900);
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
  void controlFitsAndHintsHavePriority() {
    MeterScene scene; QVERIFY(scene.create(true));
    QCOMPARE(scene.window->size(), QSize(232, 96));
    scene.window->show(); QTest::qWait(10);
    auto *rows = scene.window->findChild<QQuickItem *>("meterRows");
    auto *hint = scene.window->findChild<QQuickItem *>("lowerHint");
    QVERIFY(rows); QVERIFY(hint); QVERIFY(rows->isVisible());
    QVERIFY(rows->y() + rows->height() <= 96);
    QCOMPARE(rows->childItems().size(), 2);
    for (auto *row : rows->childItems()) QCOMPARE(row->width(), 216.0);
    scene.mock->setProperty("pausePending", true);
    QVERIFY(!rows->isVisible()); QCOMPARE(hint->property("text").toString(), QString("Waiting for recorder…"));
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
    scene.window->show();
    return app.exec();
  }
  AudioMeterUiTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "audio-meter-ui-test.moc"
