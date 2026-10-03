#include "omarchy-theme.hpp"
#include "studio.hpp"
#include "video.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJSValue>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

namespace {
QVariant value(QObject *object, const char *name) {
  const auto property = object->property(name);
  return property.metaType() == QMetaType::fromType<QJSValue>()
             ? property.value<QJSValue>().toVariant()
             : property;
}
QVariantList removedParts() {
  return {QVariantMap{{"start", 1.5}, {"end", 2.0}}};
}
QVariantMap restoredState() {
  return {{"clipStart", 0.5},
          {"clipEnd", 3.5},
          {"muted", true},
          {"cuts", removedParts()},
          {"signature", "restored-camera-edit"}};
}
struct PaneScene {
  QQmlEngine engine;
  Video video;
  OmarchyTheme theme;
  QQmlComponent component;
  QQuickWindow window;
  std::unique_ptr<QQuickItem> pane;

  explicit PaneScene(const QTemporaryDir &temp)
      : theme(nullptr, temp.filePath("theme"), temp.filePath("theme-config"),
              false),
        component(&engine) {
    auto *store = new ImageStore;
    engine.addImageProvider("frames", store);
    video.setImageStore(store);
    engine.rootContext()->setContextProperty("video", &video);
    engine.rootContext()->setContextProperty("theme", &theme);
    component.loadUrl(
        QUrl::fromLocalFile(QFINDTESTDATA("../qml/VideoPane.qml")));
  }
  bool create() {
    pane.reset(qobject_cast<QQuickItem *>(component.create()));
    if (!pane)
      return false;
    window.resize(1100, 750);
    pane->setParentItem(window.contentItem());
    pane->setSize(window.size());
    return true;
  }
};
} // namespace

// Observe the actual backend at each reactive QML assignment. Final values
// alone would miss a temporary write of a half-restored edit.
class EditStateObserver : public QObject {
  Q_OBJECT
public:
  Video *video = nullptr;
  QList<QVariantMap> states;
public slots:
  void record() { states.append(video->editState()); }
};

class VideoPaneTest : public QObject {
  Q_OBJECT
  QTemporaryDir temp;
  QString plain, cameraClip, camera;

private slots:
  void initTestCase() {
    QVERIFY(temp.isValid());
    plain = temp.filePath("plain.mp4");
    cameraClip = temp.filePath("screen-camera.mp4");
    camera = temp.filePath("webcam.mp4");
    QProcess ffmpeg;
    ffmpeg.start("ffmpeg", {"-hide_banner", "-loglevel", "error", "-f", "lavfi",
                            "-i", "color=c=white:size=160x120:rate=10", "-t",
                            "4", "-c:v", "libx264", "-preset", "ultrafast",
                            "-pix_fmt", "yuv420p", plain});
    QVERIFY(ffmpeg.waitForFinished(10000));
    QCOMPARE(ffmpeg.exitCode(), 0);
    QVERIFY(QFile::copy(plain, cameraClip));
    QVERIFY(QFile::copy(plain, camera));
    QFile sidecar(cameraClip + ".camera.json");
    QVERIFY(sidecar.open(QIODevice::WriteOnly));
    const auto bytes =
        QJsonDocument(QJsonObject{{"version", 1},
                                  {"file", QFileInfo(camera).fileName()},
                                  {"duration", 4.0}})
            .toJson();
    QCOMPARE(sidecar.write(bytes), bytes.size());
  }
  void init() {
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .removeRecursively();
    QSettings().clear();
  }
  void cameraDraftRestoresThroughRealPane_data() {
    QTest::addColumn<QString>("previous");
    QTest::newRow("warm-switch") << QString("clip");
    QTest::newRow("cold-empty-pane") << QString("empty");
    QTest::newRow("pane-created-after-restore") << QString("restored");
  }
  void cameraDraftRestoresThroughRealPane() {
    QFETCH(QString, previous);
    QString id;
    QVariantMap layout;
    {
      Video seed;
      QSignalSpy loaded(&seed, &Video::loaded);
      seed.open(QUrl::fromLocalFile(cameraClip));
      QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 12000);
      QCOMPARE(seed.cameraSource(), QUrl::fromLocalFile(camera));
      seed.setCameraLayout(
          {{"visible", false}, {"width", 0.3}, {"x", 0.03}, {"y", 0.1}});
      layout = seed.cameraLayout();
      seed.setEditState(restoredState());
      QVERIFY(seed.saveDraftNow());
      QCOMPARE(seed.drafts().size(), 1);
      id = seed.drafts().first().toMap().value("id").toString();
    }
    QVERIFY(!id.isEmpty());
    PaneScene scene(temp);
    QTRY_VERIFY2(scene.component.isReady(),
                 qPrintable(scene.component.errorString()));
    QSignalSpy loaded(&scene.video, &Video::loaded);
    if (previous == "restored") {
      scene.video.resumeDraft(id);
      QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 12000);
    }
    QVERIFY2(scene.create(), qPrintable(scene.component.errorString()));
    if (previous == "clip") {
      scene.video.open(QUrl::fromLocalFile(plain));
      QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 12000);
      // The old edit is valid against the new duration. The new camera layout
      // emits changed before loaded, which used to save these stale values.
      QVERIFY(scene.pane->setProperty("clipStart", 0.2));
      QVERIFY(scene.pane->setProperty("clipEnd", 3.0));
      QVERIFY(!scene.pane->property("muted").toBool());
      QVERIFY(value(scene.pane.get(), "cuts").toList().isEmpty());
    }
    if (previous != "restored") {
      const int before = loaded.count();
      scene.video.resumeDraft(id);
      QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), before + 1, 12000);
    }
    QCOMPARE(scene.pane->property("clipStart").toDouble(), 0.5);
    QCOMPARE(scene.pane->property("clipEnd").toDouble(), 3.5);
    QVERIFY(scene.pane->property("muted").toBool());
    QCOMPARE(value(scene.pane.get(), "cuts").toList(), removedParts());
    QCOMPARE(scene.video.cameraLayout(), layout);
    QVERIFY(!scene.pane->property("loadingEditState").toBool());
    QVERIFY(scene.pane->property("editable").toBool());
    const auto state = scene.video.editState();
    QCOMPARE(state.value("clipStart").toDouble(), 0.5);
    QCOMPARE(state.value("clipEnd").toDouble(), 3.5);
    QVERIFY(state.value("muted").toBool());
    QCOMPARE(state.value("cuts").toList(), removedParts());
    QCOMPARE(state.value("signature").toString(),
             scene.pane->property("signature").toString());
    QVERIFY(scene.video.saveDraftNow());
  }
  void restorationDoesNotWriteIntermediateAssignments() {
    PaneScene scene(temp);
    QTRY_VERIFY2(scene.component.isReady(),
                 qPrintable(scene.component.errorString()));
    QVERIFY2(scene.create(), qPrintable(scene.component.errorString()));
    QSignalSpy loaded(&scene.video, &Video::loaded);
    scene.video.open(QUrl::fromLocalFile(plain));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 12000);
    const auto expected = restoredState();
    scene.video.setEditState(expected);
    EditStateObserver observer;
    observer.video = &scene.video;
    QVERIFY(QObject::connect(scene.pane.get(), SIGNAL(signatureChanged()),
                             &observer, SLOT(record())));
    QVERIFY(QMetaObject::invokeMethod(scene.pane.get(), "loadEditState"));
    QVERIFY(observer.states.size() >= 4); // Trim start/end, mute and cuts.
    for (const auto &state : observer.states)
      QCOMPARE(state, expected);
    QCOMPARE(scene.video.editState().value("cuts").toList(), removedParts());
    QCOMPARE(scene.video.editState().value("signature").toString(),
             scene.pane->property("signature").toString());
  }
  void failedOpenUnlocksExistingEditWithoutResettingIt() {
    PaneScene scene(temp);
    QTRY_VERIFY2(scene.component.isReady(),
                 qPrintable(scene.component.errorString()));
    QVERIFY2(scene.create(), qPrintable(scene.component.errorString()));
    QSignalSpy loaded(&scene.video, &Video::loaded);
    scene.video.open(QUrl::fromLocalFile(plain));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 12000);
    QVERIFY(QMetaObject::invokeMethod(scene.pane.get(), "toggleSound"));
    const auto undo = value(scene.pane.get(), "undoStack");
    scene.video.open(QUrl::fromLocalFile(temp.filePath("missing.mp4")));
    QVERIFY(scene.pane->property("loadingEditState").toBool());
    QTRY_VERIFY_WITH_TIMEOUT(scene.pane->property("editable").toBool(), 12000);
    QCOMPARE(loaded.count(), 1);
    QCOMPARE(scene.video.source(), QUrl::fromLocalFile(plain));
    QVERIFY(scene.pane->property("muted").toBool());
    QCOMPARE(value(scene.pane.get(), "undoStack"), undo);
    QVERIFY(QMetaObject::invokeMethod(scene.pane.get(), "toggleSound"));
    QVERIFY(!scene.video.editState().value("muted").toBool());
  }
  void markClipboardKeysJoinTheUndoStack() {
    PaneScene scene(temp);
    QTRY_VERIFY2(scene.component.isReady(),
                 qPrintable(scene.component.errorString()));
    QVERIFY2(scene.create(), qPrintable(scene.component.errorString()));
    scene.window.show();
    scene.window.requestActivate();
    scene.pane->forceActiveFocus();
    QVERIFY(QTest::qWaitForWindowExposed(&scene.window));
    QTRY_VERIFY(scene.window.activeFocusItem());
    QSignalSpy loaded(&scene.video, &Video::loaded);
    scene.video.open(QUrl::fromLocalFile(plain));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 12000);
    auto *player = scene.pane->findChild<QObject *>("videoPlayer");
    QVERIFY(player);
    QTRY_VERIFY_WITH_TIMEOUT(player->property("mediaStatus").toInt() ==
                                     QMediaPlayer::LoadedMedia ||
                                 player->property("mediaStatus").toInt() ==
                                     QMediaPlayer::BufferedMedia,
                             12000);
    QVERIFY(QMetaObject::invokeMethod(scene.pane.get(), "seek",
                                      Q_ARG(QVariant, 1.5)));
    QTRY_COMPARE(scene.pane->property("head").toDouble(), 1.5);
    scene.video.marks()->edit("arrow", 0.2, 0.2, 0.4, 0.4);
    QTRY_VERIFY(scene.pane->property("markSelected").toBool());
    const auto undoSteps = [&] {
      return scene.pane->property("undoStack").toList().size();
    };
    const int before = undoSteps();
    QTest::keyClick(&scene.window, Qt::Key_C, Qt::ControlModifier);
    QVERIFY(scene.video.marks()->canPaste());
    QCOMPARE(undoSteps(), before); // Copying changes nothing.
    QTest::keyClick(&scene.window, Qt::Key_V, Qt::ControlModifier);
    QCOMPARE(scene.video.marks()->edits().size(), 2);
    // The paste starts where the video is paused.
    QCOMPARE(scene.video.marks()->edits()[1].start, scene.video.marks()->playhead());
    QTest::keyClick(&scene.window, Qt::Key_D, Qt::ControlModifier);
    QCOMPARE(scene.video.marks()->edits().size(), 3);
    QTest::keyClick(&scene.window, Qt::Key_X, Qt::ControlModifier);
    QCOMPARE(scene.video.marks()->edits().size(), 2);
    QCOMPARE(undoSteps(), before + 3);
    // One Ctrl+Z each, newest first.
    QTest::keyClick(&scene.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(scene.video.marks()->edits().size(), 3);
    QTest::keyClick(&scene.window, Qt::Key_Z, Qt::ControlModifier);
    QTest::keyClick(&scene.window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(scene.video.marks()->edits().size(), 1);
    QCOMPARE(undoSteps(), before);
  }
  void timingDrag_data() {
    QTest::addColumn<bool>("start");
    QTest::addColumn<QString>("finish");
    QTest::newRow("extend-start") << true << QString("release");
    QTest::newRow("extend-end") << false << QString("release");
    QTest::newRow("cancel-mouse-grab") << true << QString("cancel");
    QTest::newRow("cancel-escape") << false << QString("escape");
    QTest::newRow("new-load") << false << QString("load");
  }
  void timingDrag() {
    QFETCH(bool, start);
    QFETCH(QString, finish);
    PaneScene scene(temp);
    QTRY_VERIFY2(scene.component.isReady(),
                 qPrintable(scene.component.errorString()));
    QVERIFY2(scene.create(), qPrintable(scene.component.errorString()));
    scene.window.show();
    scene.window.requestActivate();
    scene.pane->forceActiveFocus();
    QVERIFY(QTest::qWaitForWindowExposed(&scene.window));
    QTRY_VERIFY(scene.window.activeFocusItem());
    QSignalSpy loaded(&scene.video, &Video::loaded);
    scene.video.open(QUrl::fromLocalFile(plain));
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 12000);
    auto *player = scene.pane->findChild<QObject *>("videoPlayer");
    QVERIFY(player);
    QTRY_VERIFY_WITH_TIMEOUT(player->property("mediaStatus").toInt() ==
                                     QMediaPlayer::LoadedMedia ||
                                 player->property("mediaStatus").toInt() ==
                                     QMediaPlayer::BufferedMedia,
                             12000);
    QVERIFY(QMetaObject::invokeMethod(scene.pane.get(), "seek",
                                      Q_ARG(QVariant, 1.5)));
    QTRY_COMPARE(scene.pane->property("head").toDouble(), 1.5);
    scene.video.marks()->edit("redact", 0.25, 0.25, 0.5, 0.5);
    scene.video.marks()->setSelectedTimes(1, 2);
    scene.video.marks()->select(0);
    QVERIFY(scene.pane->setProperty("head", 1.5));
    auto *lane = scene.pane->findChild<QQuickItem *>("videoMarkLane");
    auto *track = scene.pane->findChild<QQuickItem *>("videoTimelineTrack");
    auto *handle = scene.pane->findChild<QQuickItem *>(
        start ? "videoMarkStartHandle" : "videoMarkEndHandle");
    QVERIFY(lane && track && handle);
    QTest::qWait(50); // Polish the actual pane's anchored timeline layout.
    QVERIFY(handle->isVisible());
    const int undoBefore = value(scene.pane.get(), "undoStack").toList().size();
    QSignalSpy edited(scene.video.marks(), &MarkDocument::edited);
    const double target = start ? 0.3 : 3.4;
    const QPoint press =
        handle->mapToScene(QPointF(handle->width() / 2, handle->height() / 2))
            .toPoint();
    const QPoint move =
        press + QPoint(qRound(track->width() * (target - (start ? 1 : 2)) /
                              scene.video.duration()),
                       0);
    QTest::mousePress(&scene.window, Qt::LeftButton, Qt::NoModifier, press);
    QVERIFY(lane->property("dragging").toBool());
    QCOMPARE(lane->property("draftMark").toInt(), 0);
    QTest::mouseMove(&scene.window, move);
    QCOMPARE(lane->property(start ? "draftStart" : "draftEnd").toDouble(),
             target);
    // Deliver a paused-player position update outside the OLD interval. The
    // guard must work regardless of when the multimedia backend seeks there.
    QVERIFY(scene.pane->setProperty("head", start ? target : target - 0.1));
    QVERIFY(scene.pane->property("markSelected").toBool());
    QCOMPARE(scene.video.marks()->selectedAnnotation().value("index").toInt(),
             0);
    QCOMPARE(scene.video.marks()->edits()[0].start, 1.0);
    QCOMPARE(scene.video.marks()->edits()[0].end, 2.0);
    QCOMPARE(edited.count(), 0);

    if (finish == "cancel") {
      auto *grab = handle->findChild<QQuickItem *>("trimHandleGrab");
      QVERIFY(grab);
      grab->ungrabMouse();
    } else if (finish == "escape") {
      QTest::keyClick(&scene.window, Qt::Key_Escape);
      QVERIFY(!lane->property("dragging").toBool());
    } else if (finish == "load") {
      scene.video.open(QUrl::fromLocalFile(cameraClip));
    }
    QTest::mouseRelease(&scene.window, Qt::LeftButton, Qt::NoModifier, move);
    QVERIFY(!lane->property("dragging").toBool());
    QCOMPARE(lane->property("draftMark").toInt(), -1);
    QCOMPARE(lane->property("draftStart").toDouble(), -1.0);
    QCOMPARE(lane->property("draftEnd").toDouble(), -1.0);
    if (finish == "load") {
      QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 2, 12000);
      QVERIFY(scene.video.marks()->edits().isEmpty());
      QCOMPARE(lane->property("draftMark").toInt(), -1);
      return;
    }
    const bool committed = finish == "release";
    QCOMPARE(scene.video.marks()->edits()[0].start,
             committed && start ? target : 1.0);
    QCOMPARE(scene.video.marks()->edits()[0].end,
             committed && !start ? target : 2.0);
    QCOMPARE(edited.count(), committed ? 1 : 0);
    QCOMPARE(value(scene.pane.get(), "undoStack").toList().size(),
             undoBefore + (committed ? 1 : 0));
    if (committed) {
      QVERIFY(QMetaObject::invokeMethod(scene.pane.get(), "undo"));
      QCOMPARE(scene.video.marks()->edits()[0].start, 1.0);
      QCOMPARE(scene.video.marks()->edits()[0].end, 2.0);
      QVERIFY(QMetaObject::invokeMethod(scene.pane.get(), "redo"));
      QCOMPARE(scene.video.marks()->edits()[0].start, start ? target : 1.0);
      QCOMPARE(scene.video.marks()->edits()[0].end, start ? 2.0 : target);
    }
    // Normal deselection resumes after the drag ends.
    scene.video.marks()->select(0);
    QVERIFY(scene.pane->setProperty("head", 3.9));
    QVERIFY(!scene.pane->property("markSelected").toBool());
  }
};

int main(int argc, char **argv) {
  QTemporaryDir home, runtime;
  if (!home.isValid() || !runtime.isValid())
    return 2;
  qputenv("XDG_RUNTIME_DIR", QFile::encodeName(runtime.path()));
  qputenv("HOME", QFile::encodeName(home.path()));
  qputenv("XDG_CONFIG_HOME", QFile::encodeName(home.filePath("config")));
  qputenv("XDG_DATA_HOME", QFile::encodeName(home.filePath("data")));
  qputenv("XDG_CACHE_HOME", QFile::encodeName(home.filePath("cache")));
  qputenv("QT_QPA_PLATFORM", "offscreen");
  qputenv("QT_QUICK_BACKEND", "software");
  qunsetenv("QT_QPA_PLATFORMTHEME");
  qunsetenv("WAYLAND_DISPLAY");
  qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
  QGuiApplication app(argc, argv);
  QCoreApplication::setOrganizationName("Omaframe-test");
  QCoreApplication::setApplicationName("VideoPane");
  VideoPaneTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "video-pane-test.moc"
