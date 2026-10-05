#include "audio-levels.hpp"
#include "recording.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QtTest>

class AudioPulseTest : public QObject {
  Q_OBJECT
  QByteArray pactl(const QStringList &args) {
    QProcess process; process.start("pactl", args);
    if (!process.waitForFinished(3000) || process.exitCode() != 0) {
      qWarning().noquote() << "pactl" << args << process.readAllStandardError();
      return {};
    }
    return process.readAllStandardOutput().trimmed();
  }
  int streams() {
    const auto bytes = pactl({"-f", "json", "list", "source-outputs"});
    if (bytes.isEmpty()) return -1;
    return QJsonDocument::fromJson(bytes).array().size();
  }
private slots:
  void toneSilenceRemovalAndTeardown() {
    const auto tone = pactl({"load-module", "module-sine-source", "source_name=meter_tone", "frequency=440"});
    QVERIFY2(!tone.isEmpty(), "A real Pulse server and module-sine-source are required");
    const auto silence = pactl({"load-module", "module-null-sink", "sink_name=meter_silence"});
    QVERIFY(!silence.isEmpty());
    pactl({"set-default-source", "meter_tone"});
    QCOMPARE(pactl({"get-default-source"}), QByteArray("meter_tone"));
    pactl({"set-default-sink", "meter_silence"});
    QCOMPARE(pactl({"get-default-sink"}), QByteArray("meter_silence"));
    Recorder recorder;
    recorder.prepare(false);
    QTRY_COMPARE_WITH_TIMEOUT(recorder.state(), QString("setup"), 5000);
    const auto resolved = recorder.audioPreview();
    QCOMPARE(resolved.mic, QString("meter_tone"));
    QCOMPARE(resolved.sound, QString("meter_silence.monitor"));
    const auto args = Recording::arguments("fixture", "/tmp/fixture.mp4", resolved, true);
    QCOMPARE(args[args.indexOf("-a") + 1], resolved.sound + "|" + resolved.mic);
    {
      AudioLevels levels;
      levels.configure(resolved, {}, "setup", true, true);
      QCOMPARE(levels.sound()["source"].toString(), resolved.sound);
      levels.setSurface("options", "options", true);
      QTRY_COMPARE_WITH_TIMEOUT(levels.microphone()["state"].toString(), QString("Level"), 5000);
      const double db = levels.microphone()["db"].toDouble();
      QVERIFY2(db > -40 && db <= 0, qPrintable(QString("Implausible tone: %1 dBFS").arg(db)));
      QTRY_COMPARE_WITH_TIMEOUT(levels.sound()["state"].toString(), QString("Quiet"), 5000);
      QTRY_COMPARE_WITH_TIMEOUT(streams(), 2, 3000);
      const auto outputs = QJsonDocument::fromJson(pactl({"-f", "json", "list", "source-outputs"})).array();
      const auto sources = QJsonDocument::fromJson(pactl({"-f", "json", "list", "sources"})).array();
      for (const auto &name : {resolved.mic, resolved.sound}) {
        int index = -1;
        for (const auto &source : sources)
          if (source.toObject()["name"].toString() == name) index = source.toObject()["index"].toInt();
        QVERIFY(index >= 0);
        bool pinned = false;
        for (const auto &output : outputs) pinned |= output.toObject()["source"].toInt() == index;
        QVERIFY(pinned);
      }
      qInfo() << "Real pactl discovery and GSR args share pinned sources:" << resolved.mic << resolved.sound;
      qInfo().noquote() << "Synthetic tone:" << db << "dBFS; silent monitor: Quiet; streams:" << streams();
      pactl({"unload-module", QString::fromUtf8(tone)});
      QTRY_COMPARE_WITH_TIMEOUT(levels.microphone()["state"].toString(), QString("Unavailable"), 5000);
      qInfo() << "Removed pinned source: Unavailable";
      levels.setSurface("options", "options", false);
      QTRY_COMPARE_WITH_TIMEOUT(streams(), 0, 3000);
      // Reopen and destroy with a live stream to exercise asynchronous teardown.
      levels.configure({{}, "meter_silence.monitor", {}, "Silent output"}, {}, "setup", false, true);
      levels.setSurface("options", "options", true);
      QTRY_COMPARE_WITH_TIMEOUT(streams(), 1, 3000);
    }
    QTRY_COMPARE_WITH_TIMEOUT(streams(), 0, 3000);
    qInfo() << "Hide and destruction: zero leaked streams";
    pactl({"unload-module", QString::fromUtf8(silence)});
  }
};
QTEST_GUILESS_MAIN(AudioPulseTest)
#include "audio-pulse-test.moc"
