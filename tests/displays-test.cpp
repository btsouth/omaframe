#include "displays.hpp"
#include "window-targets.hpp"
#include <QJsonDocument>
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
  void visibleWindowsAreClippedToTheirDisplay() {
    const auto monitors = QJsonDocument::fromJson(R"([
      {"id":0,"name":"LEFT","x":0,"y":0,"width":1504,"height":1003,"activeWorkspace":{"id":1}},
      {"id":1,"name":"RIGHT","x":1504,"y":0,"width":1920,"height":1080,"activeWorkspace":{"id":2}}
    ])").array();
    const auto clients = QJsonDocument::fromJson(R"([
      {"monitor":0,"at":[1000,100],"size":[800,600],"mapped":true,"visible":true,"hidden":false,"workspace":{"id":1},"focusHistoryID":1},
      {"monitor":0,"at":[100,100],"size":[300,300],"mapped":true,"visible":true,"hidden":false,"workspace":{"id":3},"focusHistoryID":0},
      {"monitor":1,"at":[1600,120],"size":[300,300],"mapped":true,"visible":true,"hidden":false,"workspace":{"id":2},"focusHistoryID":0}
    ])").array();
    const auto targets = WindowTargets::fromHyprland(monitors, clients, {"LEFT", "RIGHT"});
    QCOMPARE(targets.size(), 2);
    const auto right = targets[0].toMap();
    QCOMPARE(right.value("monitor").toString(), QString("RIGHT"));
    const auto left = targets[1].toMap();
    QCOMPARE(left.value("monitor").toString(), QString("LEFT"));
    QCOMPARE(left.value("x").toDouble(), 1000.0 / 1504.0);
    QCOMPARE(left.value("w").toDouble(), 504.0 / 1504.0);
  }
  void fractionalScaleUsesLogicalWindowCoordinates() {
    const auto monitors = QJsonDocument::fromJson(R"([
      {"id":0,"name":"RIGHT","x":2048,"y":0,"width":1920,"height":1080,"scale":1.25,"activeWorkspace":{"id":2}},
      {"id":1,"name":"LEFT","x":0,"y":0,"width":2560,"height":1440,"scale":1.25,"activeWorkspace":{"id":1}}
    ])").array();
    const auto clients = QJsonDocument::fromJson(R"([
      {"monitor":1,"at":[7,31],"size":[2034,1114],"mapped":true,"visible":true,"hidden":false,"workspace":{"id":1},"focusHistoryID":0},
      {"monitor":0,"at":[2055,31],"size":[1522,826],"mapped":true,"visible":true,"hidden":false,"workspace":{"id":2},"focusHistoryID":1}
    ])").array();
    const auto targets = WindowTargets::fromHyprland(monitors, clients, {"LEFT", "RIGHT"});
    QCOMPARE(targets.size(), 2);
    const auto left = targets[0].toMap();
    QCOMPARE(left.value("monitor").toString(), QString("LEFT"));
    QCOMPARE(left.value("x").toDouble(), 7.0 / 2048.0);
    QCOMPARE(left.value("y").toDouble(), 31.0 / 1152.0);
    QCOMPARE(left.value("w").toDouble(), 2034.0 / 2048.0);
    QCOMPARE(left.value("h").toDouble(), 1114.0 / 1152.0);
    const auto right = targets[1].toMap();
    QCOMPARE(right.value("x").toDouble(), 7.0 / 1536.0);
    QCOMPARE(right.value("w").toDouble(), 1522.0 / 1536.0);
  }
  void poweredOffDisplaysAreLeftOut() {
    // A desk monitor turned off while streaming from a virtual display.
    const auto monitors = QJsonDocument::fromJson(R"([
      {"id":1,"name":"DP-1","x":0,"y":0,"width":3440,"height":1440,"dpmsStatus":false},
      {"id":2,"name":"SUNVD","x":20000,"y":0,"width":2560,"height":1080,"dpmsStatus":true},
      {"id":3,"name":"HDMI-A-1","x":3440,"y":0,"width":1920,"height":1080}
    ])").array();
    QCOMPARE(WindowTargets::awake(monitors, {"DP-1", "SUNVD", "HDMI-A-1"}),
             QStringList({"SUNVD", "HDMI-A-1"}));
    QCOMPARE(WindowTargets::awake(monitors, {"DP-1"}), QStringList({"DP-1"}));
    QCOMPARE(WindowTargets::awake({}, {"DP-1"}), QStringList({"DP-1"}));
    // A repeat on DP-1 knows to select again elsewhere.
    QCOMPARE(WindowTargets::dark(monitors), QStringList({"DP-1"}));
    QCOMPARE(WindowTargets::dark({}), QStringList());
  }
};
QTEST_APPLESS_MAIN(DisplaysTest)
#include "displays-test.moc"
