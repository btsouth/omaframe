#include "omarchy-theme.hpp"
#include "recording.hpp"
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
#include <QTimer>
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
  QPalette palette;
  palette.setColor(QPalette::Window, QColor("#202821"));
  palette.setColor(QPalette::WindowText, QColor("#e4eadd"));
  palette.setColor(QPalette::Base, QColor("#161c18"));
  palette.setColor(QPalette::AlternateBase, QColor("#253021"));
  palette.setColor(QPalette::Text, QColor("#e4eadd"));
  palette.setColor(QPalette::Button, QColor("#303a30"));
  palette.setColor(QPalette::ButtonText, QColor("#e4eadd"));
  palette.setColor(QPalette::Highlight, QColor("#76955f"));
  palette.setColor(QPalette::HighlightedText, Qt::white);
  palette.setColor(QPalette::PlaceholderText, QColor("#84927c"));
  palette.setColor(QPalette::Mid, QColor("#43543b"));
  palette.setColor(QPalette::Light, QColor("#819576"));
  app.setPalette(palette);
  app.setOrganizationName("Omaframe");
  app.setApplicationName("Omaframe");
  app.setApplicationVersion("0.2.0");
  app.setDesktopFileName("io.github.btsouth.omaframe");
  app.setQuitOnLastWindowClosed(false);
  QThreadPool::globalInstance()->setMaxThreadCount(2);
  QCommandLineParser parser;
  parser.setApplicationDescription("A little finish. A better screenshot.");
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addOption(
      {"record",
       "Open recording setup, or stop the current Omaframe recording."});
  parser.addOption({"stop-recording",
                    "Stop an Omaframe recording; fail if none is active."});
  parser.addOption({"studio", "Open the full editor and media tools."});
  parser.addOption({"capture", "Capture a region immediately."});
  parser.addOption({"screen", "Capture the active monitor immediately."});
  parser.addPositionalArgument("image", "Image file to open.", "[image]");
  parser.process(app);
  const bool captureStartup =
      parser.positionalArguments().isEmpty() && !parser.isSet("studio");
  const QString file =
      parser.positionalArguments().isEmpty()
          ? QString()
          : QFileInfo(parser.positionalArguments().first()).absoluteFilePath();
  const QString command = parser.isSet("stop-recording") ? "stop-recording"
                          : parser.isSet("record")       ? "record"
                          : !file.isEmpty()              ? "open"
                          : parser.isSet("studio")       ? "studio"
                          : parser.isSet("screen")       ? "screen"
                                                         : "capture";
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
    if (command == "stop-recording") {
      if (client.bytesAvailable() == 0 && !client.waitForReadyRead(2000))
        return 1;
      return client.readAll().trimmed() == "ok" ? 0 : 1;
    }
    return 0;
  }
  if (command == "stop-recording")
    return 1;
  QLocalServer::removeServer(socketName);
  QLocalServer server;
  server.setSocketOptions(QLocalServer::UserAccessOption);
  if (!server.listen(socketName))
    return 1;
  auto *store = new ImageStore;
  Studio studio(store, !captureStartup && file.isEmpty());
  Video video;
  Recorder recorder;
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
  engine.rootContext()->setContextProperty("theme", &theme);
  engine.rootContext()->setContextProperty("studio", &studio);
  engine.rootContext()->setContextProperty("video", &video);
  engine.rootContext()->setContextProperty("recorder", &recorder);
  engine.rootContext()->setContextProperty("captureAtStartup", captureStartup);
  // The capture path only creates a selection surface. Load the chooser
  // after selection, and the full editor only when explicitly requested.
  QQuickWindow *window = nullptr;
  QQuickWindow *chooser = nullptr;
  QQuickWindow *recordSetup = nullptr;
  QQuickWindow *recordControl = nullptr;
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
  QList<QQuickWindow *> selections;
  auto screenFor = [](const QString &name) {
    for (auto *screen : QGuiApplication::screens())
      if (screen->name() == name)
        return screen;
    return QGuiApplication::primaryScreen();
  };
  // Recording setup opened by its hotkey appears where the user is working,
  // like other Omarchy surfaces: Hyprland's focused monitor.
  auto focusedMonitor = [] {
    QProcess process;
    process.start("hyprctl", {"-j", "monitors"});
    if (!process.waitForFinished(1000)) {
      process.kill();
      process.waitForFinished();
      return QString();
    }
    for (const auto &value : QJsonDocument::fromJson(process.readAllStandardOutput()).array())
      if (value.toObject().value("focused").toBool())
        return value.toObject().value("name").toString();
    return QString();
  };
  auto prepareRecording = [&] {
    if (const QString monitor = focusedMonitor(); !monitor.isEmpty())
      studio.setCaptureMonitor(monitor);
    recorder.prepare();
  };
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
  auto hideAll = [&] {
    if (window)
      window->hide();
    if (chooser)
      chooser->hide();
    clearSelections();
    if (recordSetup)
      recordSetup->hide();
    if (recordControl)
      recordControl->hide();
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
    hideAll();
    if (!recordSetup) {
      QQmlComponent component(&engine, QUrl("qrc:/qml/RecordSetup.qml"));
      recordSetup = qobject_cast<QQuickWindow *>(component.create());
      if (!recordSetup) {
        app.exit(1);
        return;
      }
    }
    placeLayer(recordSetup, screenFor(studio.captureMonitor()),
               "omaframe-record-setup");
    recordSetup->show();
    recordSetup->requestActivate();
  };
  QObject::connect(&recorder, &Recorder::setupRequested, &app, showRecordSetup);
  QObject::connect(&recorder, &Recorder::hideRequested, &app, hideAll);
  QObject::connect(&recorder, &Recorder::selectionRequested, &app, [&] {
    studio.setRecordingSelection(true);
    studio.capture(true);
  });
  QObject::connect(&studio, &Studio::recordRequested, &app,
                   [&] { recorder.prepare(); });
  QObject::connect(&studio, &Studio::recordRegionSelected, &recorder,
                   &Recorder::regionSelected);
  QObject::connect(&recorder, &Recorder::controlRequested, &app, [&] {
    const auto placement = recorder.control();
    auto *screen = screenFor(placement.display);
    if (!screen || screen->name() != placement.display) {
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
  QObject::connect(&recorder, &Recorder::completed, &app,
                   [&](const QUrl &path) {
                     hideAll();
                     studio.leaveQuickMode();
                     if (!ensureWindow()) {
                       app.exit(1);
                       return;
                     }
                     video.open(path);
                     window->show();
                     window->requestActivate();
                   });
  QObject::connect(&recorder, &Recorder::dismissRequested, &app, [&] {
    hideAll();
    QTimer::singleShot(0, &app, &QCoreApplication::quit);
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
  QObject::connect(&studio, &Studio::hideStudio, &app, hideAll);
  QObject::connect(&studio, &Studio::selectionDone, &app, clearSelections);
  QObject::connect(&studio, &Studio::chooserRequested, &app, showChooser);
  QObject::connect(&studio, &Studio::captureFailed, &app, showChooser);
  QObject::connect(&studio, &Studio::showStudio, &app, [&] {
    if (!ensureWindow()) {
      app.exit(1);
      return;
    }
    window->show();
    window->requestActivate();
  });
  QObject::connect(&studio, &Studio::editorRequested, &app, [&] {
    if (chooser)
      chooser->hide();
    if (!ensureWindow()) {
      app.exit(1);
      return;
    }
    window->setProperty("editing", true);
    window->setProperty("videoMode", false);
    window->setScreen(screenFor(studio.captureMonitor()));
    window->show();
    window->requestActivate();
  });
  QObject::connect(&studio, &Studio::dismissRequested, &app, [&] {
    hideAll();
    QTimer::singleShot(0, &app, &QCoreApplication::quit);
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
        if (recorder.active())
          return;
        if (cmd == "record") {
          if (!studio.busy() && !video.busy())
            prepareRecording();
          return;
        }
        if (studio.busy() || video.busy())
          return;
        if (studio.quickMode()) {
          if (chooser && chooser->isVisible())
            chooser->requestActivate();
          else if (window && window->isVisible())
            window->requestActivate();
          return;
        }
        if (cmd == "capture" || cmd == "screen")
          studio.capture(cmd == "capture");
        else {
          if (!ensureWindow()) {
            app.exit(1);
            return;
          }
          if (cmd == "open")
            studio.open(QUrl::fromLocalFile(request.value("file").toString()));
          window->show();
          window->requestActivate();
        }
      });
    }
  });
  if (!captureStartup && !ensureWindow())
    return 1;
  if (!file.isEmpty())
    studio.open(QUrl::fromLocalFile(file));
  if (captureStartup)
    QTimer::singleShot(0, &studio, [&] {
      if (command == "record")
        prepareRecording();
      else {
        mark("capture requested");
        studio.capture(command != "screen");
      }
    });
  const int result = app.exec();
  clearSelections();
  delete recordSetup;
  delete recordControl;
  return result;
}
