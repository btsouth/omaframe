#include "audio-levels.hpp"
#include "recording.hpp"
#include <QtTest>
#include <limits>

class FakeLevels final : public AudioLevelBackend {
public:
  int opens = 0, closes = 0;
  quint64 generation = 0;
  AudioSnapshot snapshot;
  void open(const AudioSnapshot &s, quint64 g) override { ++opens; snapshot = s; generation = g; }
  void close() override { ++closes; }
  void send(int i, QList<float> values) { emit samples(i, generation, values); }
};
class AudioLevelsTest : public QObject {
  Q_OBJECT
  const AudioSnapshot preview{"chosen_mic", "actual_monitor", "Chosen mic", "Output"};
  const AudioSnapshot session{"session_mic", "session_monitor", "Session mic", "Session output"};
private slots:
  void math() {
    QCOMPARE(AudioLevels::decibels({0}), -60.0);
    QCOMPARE(AudioLevels::decibels({0.00001f}), -60.0);
    QCOMPARE(AudioLevels::decibels({2}), 0.0);
    QCOMPARE(AudioLevels::decibels({-1}), 0.0);
    QVERIFY(std::abs(AudioLevels::decibels({0.1f, -0.5f}) + 6.0206) < 0.001);
    QCOMPARE(AudioLevels::decibels({std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}), -60.0);
    QVERIFY(std::abs(AudioLevels::decibels({std::numeric_limits<float>::quiet_NaN(), 0.5f}) + 6.0206) < 0.001);
  }
  void smoothingHoldClipAndStale() {
    auto backend = std::make_unique<FakeLevels>(); auto *fake = backend.get();
    AudioLevels levels(std::move(backend), nullptr, false);
    levels.configure(preview, session, "setup", true, true);
    levels.setSurface("options", "options", true);
    QCOMPARE(levels.microphone()["state"].toString(), "Checking");
    fake->send(0, {1}); levels.advance(40);
    QVERIFY(levels.microphone()["clip"].toBool());
    QCOMPARE(levels.microphone()["db"].toDouble(), 0.0);
    fake->send(0, {0}); levels.advance(80);
    QCOMPARE(levels.microphone()["db"].toDouble(), -8.0);
    QCOMPARE(levels.microphone()["held"].toDouble(), 0.0);
    for (int t = 120; t <= 1000; t += 40) { fake->send(0, {0}); levels.advance(t); }
    QCOMPARE(levels.microphone()["state"].toString(), "Quiet");
    QCOMPARE(levels.microphone()["held"].toDouble(), 0.0);
    fake->send(0, {0}); levels.advance(1040);
    QVERIFY(levels.microphone()["held"].toDouble() < 0);
    for (int t = 1080; t <= 2040; t += 40) { fake->send(0, {0}); levels.advance(t); }
    QVERIFY(!levels.microphone()["clip"].toBool());
    fake->send(1, {0}); levels.advance(2080);
    QCOMPARE(levels.sound()["state"].toString(), "Quiet");
    levels.advance(3200);
    QCOMPARE(levels.sound()["state"].toString(), "Unavailable");
  }
  void invalidIsNotSilenceAndGenerationIsDiscarded() {
    auto backend = std::make_unique<FakeLevels>(); auto *fake = backend.get();
    AudioLevels levels(std::move(backend), nullptr, false);
    levels.configure(preview, session, "setup", true, false);
    levels.setSurface("options", "options", true);
    fake->send(0, {std::numeric_limits<float>::quiet_NaN()}); levels.advance(40);
    QCOMPARE(levels.microphone()["state"].toString(), "Checking");
    levels.advance(2040);
    QCOMPARE(levels.microphone()["state"].toString(), "Unavailable");
    const auto old = fake->generation;
    levels.setSurface("options", "options", false);
    levels.setSurface("options", "options", true);
    emit fake->samples(0, old, {1}); levels.advance(2080);
    QCOMPARE(levels.microphone()["state"].toString(), "Checking");
    emit fake->unavailable(0, old);
    QCOMPARE(levels.microphone()["state"].toString(), "Checking");
    emit fake->unavailable(0, fake->generation);
    QCOMPARE(levels.microphone()["state"].toString(), "Unavailable");
  }
  void lifecycle_data() {
    QTest::addColumn<QString>("state"); QTest::addColumn<QString>("surface"); QTest::addColumn<bool>("open");
    for (const auto &state : {"idle", "loading", "setup", "selecting", "countdown", "starting", "recording", "pending", "paused", "stopping", "saved", "failed"})
      for (const auto &surface : {"bar", "options", "control"}) {
        const bool allowed = QString(state) == "recording" ? QString(surface) == "control" :
          (QString(state) == "loading" || QString(state) == "setup" || QString(state) == "selecting") && QString(surface) != "control";
        QTest::newRow(qPrintable(QString(state) + ":" + surface)) << QString(state) << QString(surface) << allowed;
      }
  }
  void lifecycle() {
    QFETCH(QString, state); QFETCH(QString, surface); QFETCH(bool, open);
    auto backend = std::make_unique<FakeLevels>(); auto *fake = backend.get();
    AudioLevels levels(std::move(backend), nullptr, false);
    levels.configure(preview, session, state, true, true);
    QCOMPARE(fake->opens, 0);
    levels.setSurface("first", surface, true);
    QCOMPARE(fake->opens, open ? 1 : 0);
    if (open) {
      QCOMPARE(fake->snapshot, state == "recording" ? session : preview);
      levels.setSurface("second", surface, true);
      levels.setSurface("first", surface, false);
      QCOMPARE(fake->opens, 1);
      levels.configure(preview, session, state, false, true);
      QVERIFY(fake->snapshot.mic.isEmpty()); QCOMPARE(fake->snapshot.sound, (state == "recording" ? session : preview).sound);
      levels.configure(preview, session, state, false, false);
      QCOMPARE(levels.microphone()["state"].toString(), "Off");
      QCOMPARE(levels.sound()["state"].toString(), "Off");
      levels.configure(preview, session, state, true, true);
      const int closed = fake->closes;
      levels.setSurface("second", surface, false);
      QCOMPARE(fake->closes, closed + 1);
    }
  }
  void stateTransitionsCloseAndFrozenNamesMatchArguments() {
    auto backend = std::make_unique<FakeLevels>(); auto *fake = backend.get();
    AudioLevels levels(std::move(backend), nullptr, false);
    levels.setSurface("bar", "bar", true); levels.setSurface("control", "control", true);
    levels.configure(preview, session, "setup", true, true);
    levels.configure(preview, session, "countdown", true, true);
    QCOMPARE(levels.microphone()["state"].toString(), "Off");
    levels.configure(preview, session, "starting", true, true);
    levels.configure(preview, session, "recording", true, true);
    QCOMPARE(fake->snapshot, session);
    const auto args = Recording::arguments("screen", "file.mp4", fake->snapshot, true);
    QCOMPARE(args.value(args.indexOf("-a") + 1), session.sound + "|" + session.mic);
    AudioSnapshot changed = preview; changed.sound = "new_default";
    const int opens = fake->opens;
    levels.configure(changed, session, "recording", true, true);
    QCOMPARE(fake->opens, opens); QCOMPARE(fake->snapshot, session);
    for (const auto &state : {"pending", "paused", "stopping", "failed", "saved", "idle"}) {
      levels.configure(preview, session, "recording", true, true);
      const int closed = fake->closes;
      levels.configure(preview, session, state, true, true);
      QCOMPARE(fake->closes, closed + 1);
    }
  }
};
QTEST_GUILESS_MAIN(AudioLevelsTest)
#include "audio-levels-test.moc"
