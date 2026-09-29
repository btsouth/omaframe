#include "navigation.hpp"
#include <QSignalSpy>
#include <QTest>

class NavigationTest : public QObject {
  Q_OBJECT
private slots:
  void cleanRequestsProceed_data() {
    QTest::addColumn<QString>("command");
    for (const auto *cmd :
         {"open", "capture", "screen", "repeat", "record", "review", "quit"})
      QTest::newRow(cmd) << QString(cmd);
  }
  void cleanRequestsProceed() {
    QFETCH(QString, command);
    Navigation nav;
    QSignalSpy proceed(&nav, &Navigation::proceed);
    QSignalSpy prompt(&nav, &Navigation::confirmationRequested);
    const QUrl file("file:///tmp/next.mp4");
    nav.request(command, file);
    QCOMPARE(proceed.count(), 1);
    QCOMPARE(proceed.first().at(0).toString(), command);
    QCOMPARE(proceed.first().at(1).toUrl(), file);
    QCOMPARE(prompt.count(), 0);
    QVERIFY(!nav.pending());
  }
  void cancelKeepsEditsAndAllowsAnotherRequest() {
    Navigation nav;
    nav.setDirty(true);
    QSignalSpy proceed(&nav, &Navigation::proceed);
    QSignalSpy prompt(&nav, &Navigation::confirmationRequested);
    nav.request("quit");
    QVERIFY(nav.pending());
    QCOMPARE(prompt.count(), 1);
    nav.cancel();
    QVERIFY(!nav.pending());
    QVERIFY(nav.dirty());
    QCOMPARE(proceed.count(), 0);
    nav.request("capture");
    QCOMPARE(prompt.count(), 2);
  }
  void repeatedCommandsKeepTheFirstDestination() {
    Navigation nav;
    nav.setDirty(true);
    QSignalSpy proceed(&nav, &Navigation::proceed);
    QSignalSpy prompt(&nav, &Navigation::confirmationRequested);
    const QUrl first("file:///tmp/first.mp4");
    nav.request("open", first);
    nav.request("capture");
    nav.request("open", QUrl("file:///tmp/second.mp4"));
    QCOMPARE(prompt.count(), 1);
    nav.discard();
    QCOMPARE(proceed.count(), 1);
    QCOMPARE(proceed.first().at(0).toString(), QString("open"));
    QCOMPARE(proceed.first().at(1).toUrl(), first);
    QVERIFY(!nav.pending());
    nav.discard();
    QCOMPARE(proceed.count(), 1);
  }
  void saveWaitsForSuccess() {
    Navigation nav;
    nav.setDirty(true);
    QSignalSpy proceed(&nav, &Navigation::proceed);
    QSignalSpy save(&nav, &Navigation::saveRequested);
    nav.request("quit");
    nav.save();
    nav.save();
    nav.discard();
    nav.cancel();
    nav.request("open");
    QCOMPARE(save.count(), 1);
    QCOMPARE(proceed.count(), 0);
    QVERIFY(nav.pending() && nav.saving());
    nav.setDirty(false);
    nav.saveSucceeded();
    QCOMPARE(proceed.count(), 1);
    QCOMPARE(proceed.first().at(0).toString(), QString("quit"));
    QVERIFY(!nav.pending() && !nav.saving());
    nav.saveSucceeded();
    QCOMPARE(proceed.count(), 1);
  }
  void failedSavePreservesDestinationAndAllowsRetry() {
    Navigation nav;
    nav.setDirty(true);
    QSignalSpy proceed(&nav, &Navigation::proceed);
    QSignalSpy prompt(&nav, &Navigation::confirmationRequested);
    nav.request("record");
    nav.save();
    nav.saveFailed();
    QCOMPARE(prompt.count(), 2);
    QCOMPARE(proceed.count(), 0);
    QVERIFY(nav.pending() && nav.dirty() && !nav.saving());
    nav.save();
    nav.saveSucceeded();
    QCOMPARE(proceed.count(), 1);
    QCOMPARE(proceed.first().at(0).toString(), QString("record"));
  }
  void failedSaveCanBeCancelled() {
    Navigation nav;
    nav.setDirty(true);
    QSignalSpy proceed(&nav, &Navigation::proceed);
    nav.request("screen");
    nav.save();
    nav.saveFailed();
    nav.cancel();
    nav.saveSucceeded();
    QCOMPARE(proceed.count(), 0);
    QVERIFY(!nav.pending() && nav.dirty());
  }
};

QTEST_GUILESS_MAIN(NavigationTest)
#include "navigation-test.moc"
