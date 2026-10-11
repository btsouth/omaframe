#include "capture-request.hpp"
#include <QJsonObject>
#include <QTest>

using CaptureRequest::ChooserOverride;
Q_DECLARE_METATYPE(CaptureRequest::ChooserOverride)

class CaptureRequestTest : public QObject {
  Q_OBJECT
private slots:
  void skipsChooser_data() {
    QTest::addColumn<bool>("setting");
    QTest::addColumn<ChooserOverride>("override");
    QTest::addColumn<bool>("skips");
    QTest::newRow("off-default") << false << ChooserOverride::Default << false;
    QTest::newRow("off-direct") << false << ChooserOverride::Direct << true;
    QTest::newRow("off-chooser") << false << ChooserOverride::Chooser << false;
    QTest::newRow("on-default") << true << ChooserOverride::Default << true;
    QTest::newRow("on-direct") << true << ChooserOverride::Direct << true;
    QTest::newRow("on-chooser") << true << ChooserOverride::Chooser << false;
  }
  void skipsChooser() {
    QFETCH(bool, setting);
    QFETCH(ChooserOverride, override);
    QFETCH(bool, skips);
    QCOMPARE(CaptureRequest::skipsChooser(setting, override), skips);
  }
  void cliOverride_data() {
    QTest::addColumn<QStringList>("args");
    QTest::addColumn<bool>("valid");
    QTest::addColumn<ChooserOverride>("override");
    QTest::newRow("none") << QStringList{} << true << ChooserOverride::Default;
    QTest::newRow("capture-only")
        << QStringList{"--capture"} << true << ChooserOverride::Default;
    for (const auto *flag : {"direct", "chooser"}) {
      const QString option = "--" + QString(flag);
      const auto expected = QString(flag) == "direct" ? ChooserOverride::Direct
                                                      : ChooserOverride::Chooser;
      QTest::newRow(qPrintable(QString(flag) + "-default"))
          << QStringList{option} << true << expected;
      for (const auto *mode : {"capture", "screen", "repeat"})
        QTest::newRow(qPrintable(QString(flag) + "-" + mode))
            << QStringList{"--" + QString(mode), option} << true << expected;
      QTest::newRow(qPrintable(QString(flag) + "-delay"))
          << QStringList{"--capture", "--delay", "3", option} << true << expected;
      QTest::newRow(qPrintable(QString(flag) + "-delayed-capture"))
          << QStringList{"--delayed-capture", option} << true << expected;
      for (const auto *mode :
           {"studio", "history", "scroll", "record", "stop-recording",
            "pause-recording", "resume-recording", "toggle-recording-pause"})
        QTest::newRow(qPrintable(QString(flag) + "-" + mode))
            << QStringList{"--" + QString(mode), option} << false
            << ChooserOverride::Default;
      QTest::newRow(qPrintable(QString(flag) + "-file"))
          << QStringList{option, "picture.png"} << false << ChooserOverride::Default;
    }
    QTest::newRow("both") << QStringList{"--capture", "--direct", "--chooser"}
                          << false << ChooserOverride::Default;
  }
  void cliOverride() {
    QFETCH(QStringList, args);
    QFETCH(bool, valid);
    QFETCH(ChooserOverride, override);
    QCommandLineParser parser;
    parser.addOption({"delay", "Delay", "N"});
    for (const auto *mode :
         {"capture", "delayed-capture", "direct", "chooser", "studio", "history",
          "screen", "repeat", "scroll", "record", "stop-recording",
          "pause-recording", "resume-recording", "toggle-recording-pause"})
      parser.addOption({mode, mode});
    QVERIFY(parser.parse(QStringList{"omaframe"} + args));
    ChooserOverride actual;
    QString error;
    QCOMPARE(CaptureRequest::cliOverride(parser, actual, error), valid);
    QCOMPARE(error.isEmpty(), valid);
    if (valid)
      QCOMPARE(actual, override);
  }
  void namesRoundTrip() {
    for (auto override : {ChooserOverride::Default, ChooserOverride::Direct,
                          ChooserOverride::Chooser}) {
      ChooserOverride parsed;
      QVERIFY(CaptureRequest::parseChooser(CaptureRequest::chooserName(override),
                                           parsed));
      QCOMPARE(parsed, override);
    }
    ChooserOverride parsed;
    QVERIFY(!CaptureRequest::parseChooser("finish", parsed));
  }
  void ipcCarriesTheOverride() {
    using CaptureRequest::decode;
    QString cmd;
    int seconds;
    ChooserOverride override;
    QVERIFY(decode({{"command", "capture"}}, cmd, seconds, override));
    QCOMPARE(override, ChooserOverride::Default);
    QVERIFY(decode({{"command", "capture"}, {"chooser", "direct"}}, cmd, seconds,
                   override));
    QCOMPARE(override, ChooserOverride::Direct);
    QVERIFY(decode({{"command", "screen"}, {"chooser", "chooser"}}, cmd, seconds,
                   override));
    QCOMPARE(override, ChooserOverride::Chooser);
    QVERIFY(decode({{"command", "repeat"}, {"chooser", "direct"}}, cmd, seconds,
                   override));
    QCOMPARE(override, ChooserOverride::Direct);
    QVERIFY(decode({{"command", "capture"}, {"delaySeconds", 3}, {"chooser", "direct"}},
                   cmd, seconds, override));
    QCOMPARE(seconds, 3);
    QCOMPARE(override, ChooserOverride::Direct);
    for (const auto &invalid : {QJsonValue("finish"), QJsonValue(""), QJsonValue(1),
                                QJsonValue(true), QJsonValue()})
      QVERIFY(!decode({{"command", "capture"}, {"chooser", invalid}}, cmd, seconds,
                      override));
    for (const auto *command : {"scroll", "record", "studio", "history", "open"})
      QVERIFY(!decode({{"command", command}, {"chooser", "direct"}}, cmd, seconds,
                      override));
  }
};

QTEST_GUILESS_MAIN(CaptureRequestTest)
#include "capture-request-test.moc"
