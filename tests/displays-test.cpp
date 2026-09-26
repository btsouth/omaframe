#include "displays.hpp"
#include <QTest>

class DisplaysTest : public QObject {
  Q_OBJECT
private slots:
  void twoMonitorsFromTheSameMakerAreToldApartByPosition() {
    // The same pair as `hyprctl -j monitors` reports on a real desk, listed
    // right monitor first.
    const auto names = Displays::describe(
        {{"HDMI-A-1", "Dell Inc.", "D2719HGF", {2048, 0, 1920, 1080}, {1920, 1080}},
         {"DP-2", "Dell Inc.", "DELL S2721DGF", {0, 0, 2048, 1152}, {2560, 1440}}});
    QCOMPARE(names[0].label, QString("Right · Dell D2719HGF · 1920 × 1080"));
    QCOMPARE(names[0].place, QString("right display"));
    QCOMPARE(names[1].label, QString("Left · Dell S2721DGF · 2560 × 1440"));
    QCOMPARE(names[1].place, QString("left display"));
  }
  void stackedAndTripleLayouts() {
    auto stacked = Displays::describe({{"DP-1", "LG Electronics", "LG ULTRAGEAR", {0, 1080, 2560, 1440}, {}},
                                       {"DP-2", "", "", {320, 0, 1920, 1080}, {}}});
    QCOMPARE(stacked[0].label, QString("Bottom · LG ULTRAGEAR"));
    QCOMPARE(stacked[1].label, QString("Top · DP-2"));
    auto triple = Displays::describe({{"C", "", "", {1920, 0, 1920, 1080}, {}},
                                      {"L", "", "", {0, 0, 1920, 1080}, {}},
                                      {"R", "", "", {3840, 0, 1920, 1080}, {}}});
    QCOMPARE(triple[0].place, QString("center display"));
    QCOMPARE(triple[1].place, QString("left display"));
    QCOMPARE(triple[2].place, QString("right display"));
  }
  void singleAndBuiltInDisplaysHaveNoPosition() {
    auto single = Displays::describe({{"eDP-1", "BOE", "0x0BCA", {0, 0, 1504, 1003}, {2256, 1504}}});
    QCOMPARE(single[0].label, QString("Built-in display · 2256 × 1504"));
    QCOMPARE(single[0].place, QString("built-in display"));
    QCOMPARE(Displays::describe({{"A", "Unknown", "Unknown", {0, 0, 10, 10}, {}}})[0].label,
             QString("A"));
  }
};
QTEST_APPLESS_MAIN(DisplaysTest)
#include "displays-test.moc"
