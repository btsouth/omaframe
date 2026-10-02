#include "navigation.hpp"
#include "omarchy-theme.hpp"
#include "recording.hpp"
#include "shortcuts.hpp"
#include "studio.hpp"
#include "video.hpp"
#include <LayerShellQt/Window>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFont>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QPalette>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickWindow>
#include <QScreen>
#include <QStandardPaths>
#include <QThreadPool>
#include <QThread>
#include <QTimer>
#include <QtConcurrent>
#include <QDir>
#include <QSettings>
#include <algorithm>
#include <cstdio>
#include <memory>

int main(int argc, char **argv) {
  QElapsedTimer startup;
  startup.start();
  const bool profile = qEnvironmentVariableIsSet("OMAFRAME_PROFILE_STARTUP");
  auto mark = [&](const char *phase) {
    if (profile)
      fprintf(stderr, "startup %lld ms: %s\n", startup.elapsed(), phase);
  };
  QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
  qInstallMessageHandler(
      [](QtMsgType, const QMessageLogContext &, const QString &message) {
        fprintf(stderr, "%s\n", qPrintable(message));
      });
  QGuiApplication app(argc, argv);
  mark("GUI application ready");
  app.setOrganizationName("Omaframe");
  app.setApplicationName("Omaframe");
  app.setApplicationVersion("0.7.3");
  app.setDesktopFileName("io.github.btsouth.omaframe");
  app.setQuitOnLastWindowClosed(false);
  QThreadPool::globalInstance()->setMaxThreadCount(2);
  QCommandLineParser parser;
  parser.setApplicationDescription(
      "Screenshots and screen recordings for Omarchy. With no option, "
      "select an area, window or display to capture.");
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addOption(
      {"record",
       "Select an area, window or display to record, or stop the current "
       "Omaframe recording."});
  parser.addOption({"stop-recording",
                    "Stop an Omaframe recording; fail if none is active."});
  parser.addOption(
      {"pause-recording", "Pause the current Omaframe recording."});
  parser.addOption(
      {"resume-recording", "Resume the current Omaframe recording."});
  parser.addOption({"toggle-recording-pause",
                    "Pause or resume the current Omaframe recording."});
  parser.addOption({"studio", "Open the Omaframe window."});
  parser.addOption({"capture", "Capture a region immediately."});
  parser.addOption({"repeat", "Capture the last selected screen area again."});
  parser.addOption({"screen", "Capture the active monitor immediately."});
  parser.addOption({"scroll",
                    "Select a window or area to capture as one tall "
                    "scrolling image."});
  parser.addPositionalArgument("file", "Image or video to open.", "[file]");
  parser.process(app);
  const bool captureStartup =
      parser.positionalArguments().isEmpty() && !parser.isSet("studio");
  const QString file =
      parser.positionalArguments().isEmpty()
          ? QString()
          : QFileInfo(parser.positionalArguments().first()).absoluteFilePath();
  const QString command =
      parser.isSet("stop-recording")           ? "stop-recording"
      : parser.isSet("pause-recording")        ? "pause-recording"
      : parser.isSet("resume-recording")       ? "resume-recording"
      : parser.isSet("toggle-recording-pause") ? "toggle-recording-pause"
      : parser.isSet("record")                 ? "record"
      : !file.isEmpty()                        ? "open"
      : parser.isSet("studio")                 ? "studio"
      : parser.isSet("repeat")                 ? "repeat"
      : parser.isSet("screen")                 ? "screen"
      : parser.isSet("scroll")                 ? "scroll"
                                               : "capture";
  const bool recordingControl =
      command == "stop-recording" || command == "pause-recording" ||
      command == "resume-recording" || command == "toggle-recording-pause";
  const QString socketName =
      QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) +
      "/omaframe-" +
      QString::fromLatin1(QCryptographicHash::hash(qgetenv("WAYLAND_DISPLAY"),
                                                   QCryptographicHash::Sha256)
                              .toHex()
                              .left(12));
  QLockFile lock(socketName + ".lock");
  if (!lock.tryLock(0)) {
    QLocalSocket client;
    client.connectToServer(socketName);
    if (!client.waitForConnected(1500)) {
      fprintf(stderr, "Omaframe is already starting. Try again in a moment.\n");
      return 1;
    }
    client.write(
        QJsonDocument(QJsonObject{{"command", command}, {"file", file}})
            .toJson(QJsonDocument::Compact) +
        '\n');
    client.waitForBytesWritten(1000);
    if (recordingControl) {
      if (client.bytesAvailable() == 0 && !client.waitForReadyRead(4000))
        return 1;
      return client.readAll().trimmed() == "ok" ? 0 : 1;
    }
    return 0;
  }
  if (recordingControl)
    return 1;
  QLocalServer::removeServer(socketName);
  QLocalServer server;
  server.setSocketOptions(QLocalServer::UserAccessOption);
  if (!server.listen(socketName))
    return 1;
  auto *store = new ImageStore;
  // The studio opens on its own start screen. The sample image is optional.
  Studio studio(store, false);
  QObject::connect(&app, &QCoreApplication::aboutToQuit, &studio,
                   &Studio::saveDraftNow);
  // The engine owns both stores once they are added.
  auto *videoMarks = new ImageStore;
  Video video;
  Navigation navigation;
  video.setImageStore(videoMarks);
  Recorder recorder;
  ShortcutSetup shortcuts;
  // Setting up the recording shortcut also gives the recorder its stop key.
  QObject::connect(&shortcuts, &ShortcutSetup::changed, &recorder, [&] {
    if (shortcuts.available() && !shortcuts.checking()) {
      recorder.setStopKey(shortcuts.recordKey());
      recorder.setPauseKey(shortcuts.pauseKey());
    }
  });
  QObject::connect(&studio, &Studio::videoRequested, &video, &Video::open);
  // Chrome follows the live Omarchy theme and the `monospace` font alias,
  // like the Omarchy shell. Rendered output keeps its own palette.
  OmarchyTheme theme;
  app.setFont(QFont(theme.fontFamily(), 10));
  // Stock controls in every window (tooltips, dialogs, text fields, scroll
  // bars) read the application palette, so keep it in step with the theme.
  auto applyPalette = [&theme] {
    const QColor bg = theme.alpha(theme.background(), 1), text = theme.text();
    QPalette palette;
    palette.setColor(QPalette::Window, bg);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, theme.well());
    palette.setColor(QPalette::AlternateBase, theme.mix(theme.well(), text, 0.04));
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, theme.mix(bg, text, 0.08));
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::Highlight, theme.accent());
    palette.setColor(QPalette::HighlightedText, theme.onAccent());
    palette.setColor(QPalette::Light, theme.mix(bg, text, 0.06));
    palette.setColor(QPalette::Mid, theme.mix(bg, text, 0.18));
    palette.setColor(QPalette::Dark, theme.mix(bg, text, 0.3));
    palette.setColor(QPalette::PlaceholderText, theme.faint());
    palette.setColor(QPalette::ToolTipBase, bg);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::Link, theme.accent());
    palette.setColor(QPalette::Disabled, QPalette::Text, theme.faint());
    palette.setColor(QPalette::Disabled, QPalette::WindowText, theme.faint());
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, theme.faint());
    QGuiApplication::setPalette(palette);
  };
  applyPalette();
  QObject::connect(&theme, &OmarchyTheme::changed, &app, applyPalette);
  QQmlApplicationEngine engine;
  engine.addImageProvider("frames", store);
  engine.addImageProvider("videomarks", videoMarks);
  engine.rootContext()->setContextProperty("theme", &theme);
  engine.rootContext()->setContextProperty("studio", &studio);
  engine.rootContext()->setContextProperty("video", &video);
  engine.rootContext()->setContextProperty("navigation", &navigation);
  engine.rootContext()->setContextProperty("recorder", &recorder);
  engine.rootContext()->setContextProperty("shortcuts", &shortcuts);
  engine.rootContext()->setContextProperty("captureAtStartup", captureStartup);
  // The capture path only creates a selection surface. Load the chooser
  // after selection, and the full editor only when explicitly requested.
  QQuickWindow *window = nullptr;
  QQuickWindow *chooser = nullptr;
  QQuickWindow *recordSetup = nullptr;
  QQuickWindow *recordControl = nullptr;
  QQuickWindow *scrollControl = nullptr;
  bool quickRecordingReview = false, reviewReturnsToStudio = false;
  QObject::connect(&video, &Video::opening, &app,
                   [&] { quickRecordingReview = false; });
  auto ensureWindow = [&]() -> bool {
    if (window)
      return true;
    const auto count = engine.rootObjects().size();
    engine.load(QUrl("qrc:/qml/Main.qml"));
    if (engine.rootObjects().size() == count)
      return false;
    window = qobject_cast<QQuickWindow *>(engine.rootObjects().last());
    return window != nullptr;
  };
  auto ensureChooser = [&]() -> bool {
    if (chooser)
      return true;
    const auto count = engine.rootObjects().size();
    engine.load(QUrl("qrc:/qml/FinishChooser.qml"));
    if (engine.rootObjects().size() == count)
      return false;
    chooser = qobject_cast<QQuickWindow *>(engine.rootObjects().last());
    return chooser != nullptr;
  };
  auto requestNavigation = [&](const QString &cmd, const QUrl &path = QUrl()) {
    if (window)
      QMetaObject::invokeMethod(window, "requestNavigation",
                               Q_ARG(QVariant, cmd), Q_ARG(QVariant, path));
    else
      navigation.request(cmd, path);
  };
  QList<QQuickWindow *> selections;
  auto screenFor = [](const QString &name) {
    for (auto *screen : QGuiApplication::screens())
      if (screen->name() == name)
        return screen;
    return QGuiApplication::primaryScreen();
  };
  // A quick edit or a recording review is a short task. Float it at a
  // comfortable size on the display it came from instead of squeezing it
  // into a tile. Hyprland only; other compositors keep their own placement.
  // Omarchy keeps image and video tools opaque; Omaframe's editor is one, so
  // the window behind never shows through a screenshot being edited. A quick
  // edit or review also floats at a comfortable size on its display.
  auto adjustWindow = [&](QQuickWindow *surface, bool floating) {
    if (!surface)
      return;
    const qint64 pid = QCoreApplication::applicationPid();
    const QString title = surface->title();
    (void)QtConcurrent::run([pid, title, floating] {
      auto hyprctl = [](const QStringList &args) {
        QProcess p;
        p.start("hyprctl", args);
        if (!p.waitForFinished(800)) {
          p.kill();
          p.waitForFinished();
          return QByteArray();
        }
        return p.exitCode() == 0 ? p.readAllStandardOutput() : QByteArray();
      };
      auto dispatch = [&](const QString &lua, const QStringList &legacy) {
        const QByteArray out = hyprctl({"dispatch", lua});
        if (out.isEmpty() || out.trimmed() != "ok")
          hyprctl(QStringList{"dispatch"} + legacy);
      };
      for (int attempt = 0; attempt < 30; ++attempt) {
        const auto clients = QJsonDocument::fromJson(hyprctl({"-j", "clients"})).array();
        for (const auto &value : clients) {
          const auto client = value.toObject();
          if (client.value("pid").toInteger() != pid ||
              client.value("title").toString() != title)
            continue;
          // Size it for the display Hyprland put it on, which is where the
          // user is working, minus the space its bar reserves.
          int width = 1320, height = 760;
          QString workspace;
          for (const auto &item : QJsonDocument::fromJson(hyprctl({"-j", "monitors"})).array()) {
            const auto m = item.toObject();
            if (m.value("id").toInt(-1) != client.value("monitor").toInt(-2))
              continue;
            workspace = QString::number(m.value("activeWorkspace").toObject().value("id").toInt());
            const double scale = std::max(0.1, m.value("scale").toDouble(1));
            int w = m.value("width").toInt(), h = m.value("height").toInt();
            if (m.value("transform").toInt() % 2)
              std::swap(w, h);
            const auto reserved = m.value("reserved").toArray();
            const int usableW = qRound(w / scale) - (reserved.size() == 4 ? reserved[0].toInt() + reserved[2].toInt() : 0);
            const int usableH = qRound(h / scale) - (reserved.size() == 4 ? reserved[1].toInt() + reserved[3].toInt() : 0);
            width = std::min(1560, qRound(usableW * 0.86));
            height = std::min(1020, qRound(usableH * 0.88));
          }
          const QString target = "address:" + client.value("address").toString();
          dispatch(QString("hl.dsp.window.set_prop({ window = \"%1\", prop = \"opaque\", value = \"on\" })")
                       .arg(target),
                   {"setprop", target, "opaque", "1"});
          if (!floating)
            return;
          // A window that opens while a special workspace is shown (Omarchy's
          // screensaver, a scratchpad) would stay hidden there once it closes.
          if (client.value("workspace").toObject().value("name").toString().startsWith("special:") &&
              !workspace.isEmpty() && workspace != "0") {
            dispatch(QString("hl.dsp.window.move({ window = \"%1\", workspace = \"%2\" })")
                         .arg(target, workspace),
                     {"movetoworkspace", workspace + "," + target});
            dispatch(QString("hl.dsp.focus({ window = \"%1\" })").arg(target),
                     {"focuswindow", target});
          }
          if (!client.value("floating").toBool())
            dispatch(QString("hl.dsp.window.float({ window = \"%1\", action = \"enable\" })")
                         .arg(target),
                     {"setfloating", target});
          dispatch(QString("hl.dsp.window.resize({ window = \"%1\", x = %2, y = %3 })")
                       .arg(target).arg(width).arg(height),
                   {"resizewindowpixel", QString("exact %1 %2,%3").arg(width).arg(height).arg(target)});
          dispatch(QString("hl.dsp.window.center({ window = \"%1\" })").arg(target),
                   {"centerwindow"});
          return;
        }
        QThread::msleep(50);
      }
    });
  };
  auto notify = [](const QString &summary, const QString &body,
                   const QString &image) {
    if (!QSettings().value("notifications", true).toBool() ||
        QStandardPaths::findExecutable("notify-send").isEmpty())
      return;
    // Without an installed icon, daemons show a placeholder; send none.
    const QString icon =
        !image.isEmpty() ? image
                         : QStandardPaths::locate(
                               QStandardPaths::GenericDataLocation,
                               "icons/hicolor/scalable/apps/omaframe.svg");
    QStringList args{"--app-name=Omaframe"};
    if (!icon.isEmpty())
      args << "--icon" << icon;
    QProcess::startDetached("notify-send", args << summary << body);
  };
  // Recording review ends like a quick screenshot: the video goes on the
  // clipboard, the window closes (or returns to the studio when the recording
  // started there), and a notification says where the file is. A failed copy
  // or export keeps the review open. Videos opened in the studio stay open.
  auto finishReview = [&] {
    const bool original = window && window->property("videoUnchanged").toBool() &&
                          !window->property("videoSavedCurrent").toBool();
    if (navigation.saving() || !quickRecordingReview || !window ||
        !video.copyFile(original))
      return;
    const bool edited = !original && !video.savedPath().isEmpty();
    quickRecordingReview = false;
    const QString folder =
        QFileInfo(original || video.savedPath().isEmpty()
                      ? video.source().toLocalFile()
                      : video.savedPath())
            .absolutePath()
            .replace(QDir::homePath(), "~");
    notify(edited ? "Edited video copied" : "Video copied", "Saved in " + folder,
           QString());
    if (reviewReturnsToStudio) {
      reviewReturnsToStudio = false;
      window->setProperty("recordingReview", false);
      window->setProperty("videoMode", false);
      return;
    }
    window->close();
  };
  QObject::connect(&video, &Video::originalAccepted, &app,
                   [&](const QUrl &) { finishReview(); });
  QObject::connect(&video, &Video::exported, &app,
                   [&](const QUrl &) {
                     // QML records the saved edit signature before a normal
                     // quick review closes. Save-and-continue owns its exit.
                     if (!navigation.saving())
                       QTimer::singleShot(0, &app, finishReview);
                   });
  auto placeLayer = [](QQuickWindow *surface, QScreen *screen,
                       const QString &scope) {
    if (!screen)
      return;
    auto *layer = LayerShellQt::Window::get(surface);
    layer->setScope(scope);
    layer->setLayer(LayerShellQt::Window::LayerOverlay);
    layer->setExclusiveZone(-1);
    layer->setAnchors(LayerShellQt::Window::Anchors::fromInt(
        LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom |
        LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
    layer->setKeyboardInteractivity(
        LayerShellQt::Window::KeyboardInteractivityExclusive);
    surface->setScreen(screen);
    layer->setScreen(screen);
    surface->setGeometry(screen->geometry());
  };
  auto clearSelections = [&] {
    for (auto *selection : selections) {
      selection->hide();
      selection->deleteLater();
    }
    selections.clear();
  };
  auto hideCaptureSurfaces = [&] {
    if (window)
      window->hide();
    if (chooser)
      chooser->hide();
    clearSelections();
  };
  // The recorder hides only its own surfaces. A screenshot can be in progress
  // during a recording, and clearing its selector here would strand it.
  auto hideRecorderSurfaces = [&] {
    if (recordSetup)
      recordSetup->hide();
    if (recordControl)
      recordControl->hide();
  };
  auto hideAll = [&] {
    hideCaptureSurfaces();
    if (recordSetup)
      recordSetup->hide();
    if (recordControl)
      recordControl->hide();
  };
  auto showStudioWindow = [&] {
    if (!ensureWindow()) {
      app.exit(1);
      return;
    }
    const bool wasVisible = window->isVisible();
    window->show();
    window->requestActivate();
    if (!wasVisible)
      adjustWindow(window, false);
  };
  QObject::connect(&navigation, &Navigation::confirmationRequested, &app,
                   showStudioWindow);
  // A finished or cancelled capture returns to the studio if it started
  // there, and otherwise ends this short-lived process.
  auto endQuickTask = [&] {
    if (recorder.active()) {
      studio.leaveQuickMode();
      return;
    }
    recorder.reset();
    if (studio.takeReturnToStudio()) {
      studio.leaveQuickMode();
      showStudioWindow();
      return;
    }
    QTimer::singleShot(0, &app, &QCoreApplication::quit);
  };
  auto showChooser = [&] {
    if (window)
      window->hide();
    if (!ensureChooser()) {
      app.exit(1);
      return;
    }
    clearSelections();
    placeLayer(chooser, screenFor(studio.captureMonitor()),
               "omaframe-finishes");
    chooser->show();
    chooser->requestActivate();
  };
  auto showRecordSetup = [&] {
    hideRecorderSurfaces();
    if (!studio.quickMode())
      hideCaptureSurfaces();
    if (!recordSetup) {
      QQmlComponent component(&engine, QUrl("qrc:/qml/RecordSetup.qml"));
      recordSetup = qobject_cast<QQuickWindow *>(component.create());
      if (!recordSetup) {
        app.exit(1);
        return;
      }
    }
    auto *screen = screenFor(studio.captureMonitor());
    if (!screen) {
      app.exit(1);
      return;
    }
    shortcuts.refresh();
    auto *layer = LayerShellQt::Window::get(recordSetup);
    layer->setScope("omaframe-record-setup");
    layer->setLayer(LayerShellQt::Window::LayerOverlay);
    layer->setExclusiveZone(-1);
    layer->setAnchors(LayerShellQt::Window::Anchors::fromInt(0));
    layer->setKeyboardInteractivity(
        LayerShellQt::Window::KeyboardInteractivityOnDemand);
    recordSetup->setScreen(screen);
    layer->setScreen(screen);
    recordSetup->resize(std::min(620, screen->geometry().width()),
                        std::min(recordSetup->property("wantedHeight").toInt(),
                                 screen->geometry().height()));
    recordSetup->show();
    recordSetup->requestActivate();
  };
  QObject::connect(&recorder, &Recorder::setupRequested, &app, showRecordSetup);
  QObject::connect(&recorder, &Recorder::hideRequested, &app,
                   hideRecorderSurfaces);
  QObject::connect(&recorder, &Recorder::selectionRequested, &app, [&] {
    studio.setRecordingSelection(true);
    studio.capture(true);
  });
  QObject::connect(&studio, &Studio::recordModeEntered, &app,
                   [&] { recorder.prepare(false); });
  QObject::connect(&studio, &Studio::recordOptionsRequested, &app,
                   [&] { recorder.showSetup(); });
  QObject::connect(&studio, &Studio::recordRegionSelected, &recorder,
                   [&](const QString &monitor, const QRectF &area) {
                     recorder.regionSelected(monitor, area, true);
                   });
  QObject::connect(&recorder, &Recorder::controlRequested, &app, [&] {
    const auto placement = recorder.visibleControl();
    auto *screen = screenFor(placement.display);
    if (placement.bounds.isEmpty() || !screen ||
        screen->name() != placement.display) {
      recorder.layoutChanged();
      return;
    }
    if (!recordControl) {
      QQmlComponent component(&engine, QUrl("qrc:/qml/RecordingControl.qml"));
      recordControl = qobject_cast<QQuickWindow *>(component.create());
      if (!recordControl) {
        recorder.stop();
        return;
      }
    }
    auto *layer = LayerShellQt::Window::get(recordControl);
    layer->setScope("omaframe-record-control");
    layer->setLayer(LayerShellQt::Window::LayerOverlay);
    layer->setExclusiveZone(-1);
    layer->setKeyboardInteractivity(
        LayerShellQt::Window::KeyboardInteractivityNone);
    layer->setAnchors(LayerShellQt::Window::Anchors::fromInt(
        LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorLeft));
    const auto origin =
        placement.bounds.topLeft() - screen->geometry().topLeft();
    layer->setMargins(QMargins(origin.x(), origin.y(), 0, 0));
    recordControl->setScreen(screen);
    layer->setScreen(screen);
    recordControl->resize(placement.bounds.size());
    recordControl->show();
  });
  // The scrolling capture's progress control appears only after the first
  // frame is taken, at the place Studio reserved for it: outside the capture
  // when there was room, otherwise just inside its top, which Plan.coverTop
  // keeps out of the stitch.
  auto showScrollControl = [&](const QString &monitor, const QRect &bounds) {
    auto *screen = screenFor(monitor);
    if (!screen || bounds.isEmpty()) {
      studio.scrollCapture()->cancel();
      return;
    }
    if (!scrollControl) {
      QQmlComponent component(&engine, QUrl("qrc:/qml/ScrollControl.qml"));
      scrollControl = qobject_cast<QQuickWindow *>(component.create());
      if (!scrollControl) {
        studio.scrollCapture()->cancel();
        return;
      }
    }
    auto *layer = LayerShellQt::Window::get(scrollControl);
    layer->setScope("omaframe-scroll-control");
    layer->setLayer(LayerShellQt::Window::LayerOverlay);
    layer->setExclusiveZone(-1);
    // Exclusive keyboard layers also capture pointer events on Hyprland,
    // which would swallow the page's wheel events after the first frame.
    layer->setKeyboardInteractivity(
        LayerShellQt::Window::KeyboardInteractivityOnDemand);
    layer->setAnchors(LayerShellQt::Window::Anchors::fromInt(
        LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorLeft));
    const QPoint origin = bounds.topLeft() - screen->geometry().topLeft();
    layer->setMargins(QMargins(origin.x(), origin.y(), 0, 0));
    scrollControl->setScreen(screen);
    layer->setScreen(screen);
    scrollControl->resize(bounds.size());
    scrollControl->show();
    scrollControl->requestActivate();
  };
  QObject::connect(&studio, &Studio::scrollRequested, &app, showScrollControl);
  QObject::connect(&studio, &Studio::scrollEnded, &app, [&] {
    if (scrollControl)
      scrollControl->hide();
  });
  auto openReview = [&](const QUrl &path, bool returnsToStudio) {
    hideAll();
    studio.leaveQuickMode();
    reviewReturnsToStudio = returnsToStudio;
    if (!ensureWindow()) {
      app.exit(1);
      return;
    }
    video.open(path);
    quickRecordingReview = true;
    window->setProperty("recordingReview", true);
    window->show();
    window->requestActivate();
    adjustWindow(window, true);
  };
  bool pendingReviewReturnsToStudio = false;
  auto requestReview = [&](const QUrl &path, bool returnsToStudio) {
    pendingReviewReturnsToStudio = returnsToStudio;
    requestNavigation("review", path);
  };
  // A recording stopped while a screenshot is being selected, finished or
  // edited opens its review once that screenshot is done.
  QUrl pendingReview;
  QObject::connect(&recorder, &Recorder::completed, &app,
                   [&](const QUrl &path) {
                     if (studio.quickMode()) {
                       pendingReview = path;
                       notify("Recording saved",
                              "Its review opens when you finish this screenshot.",
                              QString());
                       return;
                     }
                     requestReview(path, studio.takeReturnToStudio());
                   });
  QObject::connect(&recorder, &Recorder::dismissRequested, &app, [&] {
    hideRecorderSurfaces();
    if (studio.quickMode())
      return;
    hideCaptureSurfaces();
    endQuickTask();
  });
  for (auto *screen : QGuiApplication::screens())
    QObject::connect(screen, &QScreen::geometryChanged, &recorder,
                     [&](const QRect &) { recorder.layoutChanged(); });
  QObject::connect(
      &app, &QGuiApplication::screenAdded, &recorder, [&](QScreen *screen) {
        recorder.layoutChanged();
        QObject::connect(screen, &QScreen::geometryChanged, &recorder,
                         [&](const QRect &) { recorder.layoutChanged(); });
      });
  QObject::connect(&app, &QGuiApplication::screenRemoved, &recorder,
                   [&](QScreen *) { recorder.layoutChanged(); });
  QObject::connect(&studio, &Studio::hideStudio, &app, hideCaptureSurfaces);
  QObject::connect(&studio, &Studio::selectionDone, &app, clearSelections);
  QObject::connect(&studio, &Studio::chooserRequested, &app, showChooser);
  QObject::connect(&studio, &Studio::captureFailed, &app, showChooser);
  QObject::connect(&studio, &Studio::showStudio, &app, showStudioWindow);
  QObject::connect(&studio, &Studio::editorRequested, &app, [&] {
    if (chooser)
      chooser->hide();
    const bool wasVisible = window && window->isVisible();
    if (!ensureWindow()) {
      app.exit(1);
      return;
    }
    window->setProperty("editing", true);
    window->setProperty("videoMode", false);
    window->setScreen(screenFor(studio.captureMonitor()));
    window->show();
    window->requestActivate();
    if (!wasVisible)
      adjustWindow(window, studio.quickMode());
  });
  QObject::connect(&studio, &Studio::dismissRequested, &app, [&] {
    const bool saved = studio.quickState() == "done" && !studio.savedPath().isEmpty();
    const QString path = studio.savedPath();
    hideCaptureSurfaces();
    const bool returning = !recorder.active() && studio.takeReturnToStudio();
    if (!pendingReview.isEmpty() && !recorder.active()) {
      const QUrl review = pendingReview;
      pendingReview.clear();
      if (saved)
        notify("Screenshot copied",
               "Saved in " + QFileInfo(path).absolutePath().replace(QDir::homePath(), "~"),
               path);
      requestReview(review, returning);
      return;
    }
    if (returning) {
      studio.leaveQuickMode();
      recorder.reset();
      showStudioWindow();
      return;
    }
    if (saved)
      notify("Screenshot copied",
             "Saved in " + QFileInfo(path).absolutePath().replace(QDir::homePath(), "~"),
             path);
    endQuickTask();
  });
  QObject::connect(
      &studio, &Studio::selectionReady, &app, [&](const QStringList &names) {
        mark("capture pixels ready");
        for (const auto &name : names) {
          auto *screen = screenFor(name);
          if (!screen || screen->name() != name) {
            studio.cancelSelection();
            return;
          }
          QQmlComponent component(&engine, QUrl("qrc:/qml/Selection.qml"));
          auto *selection = qobject_cast<QQuickWindow *>(
              component.createWithInitialProperties({{"monitorName", name}}));
          if (!selection) {
            studio.cancelSelection();
            return;
          }
          selections.append(selection);
          placeLayer(selection, screenFor(name), "omaframe-selection");
          if (profile) {
            auto firstFrame = std::make_shared<bool>(true);
            QObject::connect(selection, &QQuickWindow::frameSwapped, &app,
                             [&, firstFrame] {
                               if (*firstFrame) {
                                 *firstFrame = false;
                                 mark("selector frame presented");
                               }
                             });
          }
          selection->show();
        }
      });
  QObject::connect(&app, &QGuiApplication::screenRemoved, &studio,
                   [&](QScreen *) {
                     if (studio.quickState() == "selecting")
                       studio.cancelSelection();
                   });
  QObject::connect(&navigation, &Navigation::proceed, &app,
                   [&](const QString &cmd, const QUrl &path) {
                     if (window) {
                       // Keep the current editor alive while an open is
                       // checked. A failed open must retain its edit state.
                       if (cmd != "open" && cmd != "review" &&
                           cmd != "video-draft")
                         window->setProperty("videoMode", false);
                       window->setProperty("recordingReview", false);
                     }
                     quickRecordingReview = false;
                     if (cmd == "quit") {
                       if (window)
                         window->setProperty("closingApproved", true);
                       QTimer::singleShot(0, &app, &QCoreApplication::quit);
                     } else if (cmd == "home") {
                       studio.closeImage();
                       if (window)
                         window->setProperty("operationStatus", "");
                       showStudioWindow();
                     } else if (cmd == "record")
                       studio.captureVideo();
                     else if (cmd == "repeat")
                       studio.repeatLastArea();
                     else if (cmd == "capture" || cmd == "screen")
                       studio.capture(cmd == "capture");
                     else if (cmd == "scroll")
                       studio.captureScroll();
                     else if (cmd == "review")
                       openReview(path, pendingReviewReturnsToStudio);
                     else if (cmd == "video-draft") {
                       video.resumeDraft(path.toString());
                       showStudioWindow();
                     } else if (cmd == "image-draft") {
                       studio.resumeDraft(path.toString());
                       showStudioWindow();
                     } else if (cmd == "open") {
                       if (!ensureWindow()) {
                         app.exit(1);
                         return;
                       }
                       studio.open(path);
                       showStudioWindow();
                     }
                   });
  QObject::connect(&server, &QLocalServer::newConnection, &app, [&] {
    while (auto *client = server.nextPendingConnection()) {
      QObject::connect(client, &QLocalSocket::disconnected, client,
                       &QObject::deleteLater);
      QObject::connect(client, &QLocalSocket::readyRead, &app, [&, client] {
        if (client->bytesAvailable() > 16384) {
          client->abort();
          return;
        }
        if (!client->canReadLine())
          return;
        const auto request =
            QJsonDocument::fromJson(client->readLine()).object();
        const auto cmd = request.value("command").toString();
        if (cmd == "pause-recording" || cmd == "resume-recording" ||
            cmd == "toggle-recording-pause") {
          QObject::connect(&recorder, &Recorder::pauseFinished, client,
                           [client](bool success) {
                             client->write(success ? "ok\n" : "unhandled\n");
                             client->disconnectFromServer();
                           });
          const bool paused =
              cmd == "pause-recording" ||
              (cmd == "toggle-recording-pause" && recorder.state() != "paused");
          if (!recorder.setPaused(paused)) {
            client->write("unhandled\n");
            client->disconnectFromServer();
          }
          return;
        }
        const bool stopping =
            cmd == "stop-recording" || (cmd == "record" && recorder.active());
        if (stopping) {
          const bool handled = recorder.active();
          client->write(handled ? "ok\n" : "unhandled\n");
          client->disconnectFromServer();
          if (handled)
            recorder.stop();
          return;
        }
        client->write("ok\n");
        client->disconnectFromServer();
        if (recorder.active()) {
          if (!studio.busy() && !video.busy() && !studio.quickMode()) {
            if (cmd == "repeat")
              requestNavigation(cmd);
            else if (cmd == "capture" || cmd == "screen" || cmd == "scroll")
              requestNavigation(cmd);
          }
          return;
        }
        if (studio.busy() || video.busy())
          return;
        if (studio.quickMode()) {
          // The recording shortcut switches an open selector to video.
          if (cmd == "record" && studio.quickState() == "selecting") {
            studio.recordInstead();
            return;
          }
          if (scrollControl && scrollControl->isVisible())
            scrollControl->requestActivate();
          else if (chooser && chooser->isVisible())
            chooser->requestActivate();
          else if (window && window->isVisible())
            window->requestActivate();
          return;
        }
        if (cmd == "open")
          requestNavigation(
              cmd, QUrl::fromLocalFile(request.value("file").toString()));
        else if (cmd == "record" || cmd == "repeat" || cmd == "capture" ||
                 cmd == "screen" || cmd == "scroll")
          requestNavigation(cmd);
        else
          showStudioWindow();
      });
    }
  });
  if (!captureStartup) {
    if (!ensureWindow())
      return 1;
    adjustWindow(window, false);
  }
  if (!file.isEmpty())
    studio.open(QUrl::fromLocalFile(file));
  if (captureStartup)
    QTimer::singleShot(0, &studio, [&] {
      if (command == "record")
        studio.captureVideo();
      else if (command == "repeat")
        studio.repeatLastArea();
      else if (command == "scroll")
        studio.captureScroll();
      else {
        mark("capture requested");
        studio.capture(command != "screen");
      }
    });
  const int result = app.exec();
  clearSelections();
  delete recordSetup;
  delete recordControl;
  delete scrollControl;
  return result;
}
