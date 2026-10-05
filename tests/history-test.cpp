#include "edit-json.hpp"
#include "history.hpp"
#include "recording.hpp"
#include <QClipboard>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QMimeData>
#include <QProcess>
#include <QScopeGuard>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <fcntl.h>
#include <unistd.h>

class HistoryTest : public QObject {
  Q_OBJECT
  QTemporaryDir temp;
  QString folder, data;
  std::shared_ptr<std::atomic_bool> cancel =
      std::make_shared<std::atomic_bool>(false);
  void write(const QString &path, const QByteArray &bytes = "fixture") {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QCOMPARE(f.write(bytes), bytes.size());
  }
  void json(const QString &path, const QJsonObject &doc) {
    write(path, QJsonDocument(doc).toJson());
  }
  QVector<History::Entry> scan() {
    return History::scan({folder}, data, cancel);
  }
  void old(const QString &path) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(QDateTime::currentDateTime().addSecs(-7200),
                             QFileDevice::FileModificationTime));
  }
private slots:
  void initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("omaframe-history-test");
    QCoreApplication::setApplicationName("history");
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope,
                       temp.path());
  }
  void init() {
    folder = temp.filePath(QUuid::createUuid().toString(QUuid::Id128));
    data = folder + "/data";
    QVERIFY(QDir().mkpath(folder));
    QVERIFY(QDir().mkpath(data + "/drafts"));
    QSettings().clear();
    *cancel = false;
  }
  void patterns_data() {
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("kind");
    for (const auto &s : {"Omaframe-2026.png", "Omaframe-a.png"})
      QTest::newRow(s) << QString(s) << QString("Screenshot");
    for (const auto &s :
         {"Recording-2026.mp4", "Recording-a-incomplete.mp4",
          "movie-edited.mp4", "movie-edited-2.gif", "movie-edited-10.mp4"})
      QTest::newRow(s) << QString(s) << QString("Recording");
    for (const auto &s :
         {".Omaframe-a.png", ".Recording-a.part.mp4",
          "Recording-a.mp4-webcam.mp4", "Recording-a.mp4.camera.json",
          "Recording-a.mp4.ts", "Recording-a.cleaning.mp4", "copied.png",
          "private.png", "movie.mp4", "movie-edited-1.mp4", "xOmaframe-a.png",
          "movie-edited.gif.part"})
      QTest::newRow(s) << QString(s) << QString();
  }
  void patterns() {
    QFETCH(QString, name);
    QFETCH(QString, kind);
    QCOMPARE(History::discoveredKind(name), kind);
  }
  void foldersAndExclusions() {
    write(folder + "/Omaframe-one.png");
    write(folder + "/copied.png");
    QVERIFY(QDir().mkpath(folder + "/nested"));
    write(folder + "/nested/Omaframe-hidden.png");
    QVERIFY(QFile::link(folder + "/Omaframe-one.png",
                        folder + "/Omaframe-link.png"));
    QCOMPARE(scan().size(), 1);
    History::rememberFolder(folder);
    History::rememberFolder(folder);
    QCOMPARE(History::previousFolders(), QStringList{folder});
    for (int i = 0; i < 10; ++i)
      History::rememberFolder(folder + QString::number(i));
    QCOMPARE(History::previousFolders().size(), 8);
    QVERIFY(!History::previousFolders().contains(folder));
    HistoryImages images;
    CaptureHistoryModel model(&images);
    QSettings().setValue("outputDirectory", folder + "/missing");
    QSettings().setValue("videoDirectory", folder + "/missing");
    History::rememberFolder(folder);
    model.refresh();
    QTRY_VERIFY(!model.busy());
    QCOMPARE(model.rowCount(), 1);
    model.removeFolder(folder);
    QTRY_VERIFY(!model.busy());
    QCOMPARE(model.rowCount(), 0);
  }
  void actionsRefuseChangedFiles_data() {
    QTest::addColumn<QString>("change");
    for (const auto *s :
         {"delete", "rename", "replace", "symlink", "parent-symlink"})
      QTest::newRow(s) << QString(s);
  }
  void actionsRefuseChangedFiles() {
    QFETCH(QString, change);
    const auto path = folder + "/Omaframe-one.png";
    write(path);
    QSettings().setValue("outputDirectory", folder);
    HistoryImages images;
    CaptureHistoryModel model(&images);
    model.refresh();
    QTRY_VERIFY(!model.busy());
    QCOMPARE(model.rowCount(), 1);
    QSignalSpy opened(&model, &CaptureHistoryModel::navigate);
    const auto key = model.keyAt(0);
    if (change == "parent-symlink") {
      QVERIFY(QDir().rename(folder, folder + "-moved"));
      QVERIFY(QFile::link(folder + "-moved", folder));
    } else {
      if (change == "rename")
        QVERIFY(QFile::rename(path, path + ".moved"));
      else
        QVERIFY(QFile::remove(path));
      if (change == "replace")
        write(path, "new contents");
      if (change == "symlink") {
        write(folder + "/target");
        QVERIFY(QFile::link(folder + "/target", path));
      }
    }
    QVERIFY(!model.action(0, "edit", key));
    QVERIFY(opened.isEmpty());
  }
  void draftLinkingAndRedactedPreview() {
    const auto output = folder + "/Omaframe-final.png";
    QImage source(100, 100, QImage::Format_RGB32);
    source.fill(Qt::red);
    const QString id(32, 'a');
    const auto png = data + "/drafts/" + id + ".png";
    QVERIFY(source.save(png));
    Frame::Edit redact;
    redact.type = "redact";
    redact.from = {0, 0};
    redact.to = {1, 1};
    const QJsonObject draft{{"version", 1},
                            {"name", "Private draft"},
                            {"savedPath", output},
                            {"style", 8},
                            {"edits", QJsonArray{Frame::editToJson(redact)}}};
    json(data + "/drafts/" + id + ".json", draft);
    auto rows = scan();
    QCOMPARE(rows.size(), 1);
    QVERIFY(rows[0].draft);
    const auto preview = History::draftPreview(rows[0]);
    QVERIFY(!preview.isNull());
    const auto pixel =
        preview.pixelColor(preview.width() / 2, preview.height() / 2);
    QVERIFY(pixel.red() < 100);
    QVERIFY(pixel != QColor(Qt::red));
    write(output);
    rows = scan();
    QCOMPARE(rows.size(), 1);
    QVERIFY(!rows[0].draft);
    QCOMPARE(rows[0].draftId, id);
    QFile::remove(output);
    QCOMPARE(scan().size(), 1);
    QVERIFY(scan()[0].draft);
    // Invalid edits must produce a neutral icon, never a raw-source preview.
    auto damaged = draft;
    damaged["edits"] = QJsonArray{QJsonObject{{"type", "unknown"}}};
    json(data + "/drafts/" + id + ".json", damaged);
    QVERIFY(History::draftPreview(scan()[0]).isNull());
  }
  void exactPngCopyAndFilters() {
    const auto path = folder + "/Omaframe-saved.png";
    QImage image(16, 16, QImage::Format_RGB32);
    image.fill(Qt::green);
    QVERIFY(image.save(path));
    write(folder + "/Recording-one-incomplete.mp4");
    QSettings().setValue("outputDirectory", folder);
    HistoryImages images;
    CaptureHistoryModel model(&images);
    QVERIFY(!model.busy());
    QCOMPARE(model.rowCount(), 0);
    model.refresh();
    QTRY_VERIFY(!model.busy());
    QCOMPARE(model.rowCount(), 2);
    model.setFilter("Screenshots");
    QCOMPARE(model.rowCount(), 1);
    QVERIFY(model.action(0, "copy", model.keyAt(0)));
    QFile saved(path);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QCOMPARE(QGuiApplication::clipboard()->mimeData()->data("image/png"),
             saved.readAll());
    model.setSearch("absent");
    QCOMPARE(model.rowCount(), 0);
    model.setSearch("");
    model.setFilter("Recordings");
    QCOMPARE(model.rowCount(), 1);
    QVERIFY(
        model.data(model.index(0), CaptureHistoryModel::Incomplete).toBool());
    QVERIFY(model.action(0, "copy", model.keyAt(0)));
    QCOMPARE(QGuiApplication::clipboard()->mimeData()->urls(),
             QList<QUrl>{QUrl::fromLocalFile(folder +
                                             "/Recording-one-incomplete.mp4")});
    QVERIFY(!model.action(0, "edit", "stale-row-key"));
  }
  void videoDraftLinking() {
    QVERIFY(QDir().mkpath(data + "/video-drafts"));
    const auto source = folder + "/source.mp4";
    write(source);
    const auto exported = folder + "/source-edited.gif";
    write(exported);
    const auto identity = History::identify(source);
    const QString id(64, 'b');
    json(data + "/video-drafts/" + id + ".json",
         {{"version", 1},
          {"name", "Video draft"},
          {"source", QUrl::fromLocalFile(source).toString()},
          {"sourceSize", double(identity.size)},
          {"sourceModified", double(identity.modified / 1000000)},
          {"savedPath", exported}});
    auto rows = scan();
    QCOMPARE(rows.size(), 1);
    QVERIFY(!rows[0].draft);
    QCOMPARE(rows[0].draftId, id);
    QFile::remove(exported);
    rows = scan();
    QCOMPARE(rows.size(), 1);
    QVERIFY(rows[0].draft);
    QVERIFY(History::draftPreview(rows[0]).isNull());
    write(source, "replaced");
    QCOMPARE(scan().size(), 0);
  }
  void cachePermissionsCapInvalidationAndCancellation() {
    const auto path = folder + "/Recording-fixture.mp4";
    QProcess p;
    p.start("ffmpeg",
            {"-v", "error", "-f", "lavfi", "-i", "color=c=blue:s=64x64:r=10",
             "-t", "0.2", "-c:v", "libx264", "-threads", "1", path});
    QVERIFY(p.waitForFinished(10000));
    QVERIFY2(p.exitCode() == 0, p.readAllStandardError().constData());
    auto e = scan().first();
    const auto cache = folder + "/cache";
    QVERIFY(!History::thumbnail(e, cache, cancel).isNull());
    const auto cached = cache + '/' + History::thumbnailKey(e) + ".jpg";
    QVERIFY(QFileInfo::exists(cached));
    const auto publicPermissions = QFile::ReadGroup | QFile::WriteGroup |
                                   QFile::ExeGroup | QFile::ReadOther |
                                   QFile::WriteOther | QFile::ExeOther;
    QVERIFY(!(QFileInfo(cache).permissions() & publicPermissions));
    QVERIFY(!(QFileInfo(cached).permissions() & publicPermissions));
    QFile append(path);
    QVERIFY(append.open(QIODevice::Append));
    append.write("change");
    append.close();
    QVERIFY(!History::unchanged(e));
    auto changed = scan().first();
    QVERIFY(History::thumbnailKey(e) != History::thumbnailKey(changed));
    QVERIFY(!History::thumbnail(changed, cache, cancel).isNull());
    QVERIFY(!QFileInfo::exists(cached));
    for (int i = 0; i < 4; ++i)
      write(cache + '/' + QString::number(i) + ".jpg", QByteArray(1024, 'x'));
    History::trimCache(cache, 2048);
    qint64 bytes = 0;
    for (const auto &f : QDir(cache).entryInfoList({"*.jpg"}, QDir::Files))
      bytes += f.size();
    QVERIFY(bytes <= 2048);
    *cancel = true;
    QVERIFY(
        History::thumbnail(changed, folder + "/cancelled", cancel).isNull());
    QVERIFY(!QFileInfo::exists(folder + "/cancelled"));
  }
  void publicationAndCameraNames() {
    const auto final = folder + "/Recording-2026-10-05_12-30-00-abcdef.mp4";
    const auto part = Recording::partPath(final);
    write(part);
    write(part + "-webcam.mp4");
    json(part + ".camera.json",
         {{"version", 1},
          {"file", QFileInfo(part + "-webcam.mp4").fileName()},
          {"duration", 1.0}});
    QVERIFY(!QFileInfo::exists(final));
    QCOMPARE(Recording::publishPart(part, final, false), final);
    QVERIFY(!QFileInfo::exists(part));
    QVERIFY(QFileInfo::exists(final + "-webcam.mp4"));
    QFile sidecar(final + ".camera.json");
    QVERIFY(sidecar.open(QIODevice::ReadOnly));
    QCOMPARE(
        QJsonDocument::fromJson(sidecar.readAll()).object()["file"].toString(),
        QFileInfo(final + "-webcam.mp4").fileName());
    write(part);
    QCOMPARE(Recording::publishPart(part, final, false), QString());
    QVERIFY(QFileInfo::exists(part));
    const auto incomplete = Recording::publishPart(part, final, true);
    QVERIFY(incomplete.endsWith("-incomplete.mp4"));
    const auto rows = scan();
    QCOMPARE(rows.size(), 2);
    QVERIFY(rows[0].incomplete || rows[1].incomplete);
  }
  void orphanRecoveryProtectsWritersAndRecentParts() {
    const auto final = folder + "/Recording-2026-10-05_12-30-00-abcdef.mp4";
    const auto part = Recording::partPath(final);
    write(part);
    Recording::recoverParts(folder);
    QVERIFY(QFileInfo::exists(part));
    old(part);
    QProcess writer;
    writer.start("bash", {"-c",
                          "exec -a gpu-screen-recorder python3 -c 'import "
                          "sys,time; f=open(sys.argv[1]); "
                          "print(\"ready\",flush=True); time.sleep(30)' \"$1\"",
                          "test", part});
    QVERIFY(writer.waitForReadyRead(5000));
    QVERIFY(writer.readAllStandardOutput().contains("ready"));
    Recording::recoverParts(folder);
    QVERIFY(QFileInfo::exists(part));
    writer.kill();
    QVERIFY(writer.waitForFinished(5000));
    Recording::recoverParts(folder);
    QVERIFY(!QFileInfo::exists(part));
    QVERIFY(
        QFileInfo::exists(final.left(final.size() - 4) + "-incomplete.mp4"));
  }
  void historyIpcRouting() {
    const auto runtime = folder + "/runtime";
    QVERIFY(QDir().mkpath(runtime));
    QVERIFY(QFile::setPermissions(
        runtime, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("HOME", folder);
    env.insert("XDG_RUNTIME_DIR", runtime);
    env.insert("XDG_CONFIG_HOME", folder + "/config");
    env.insert("XDG_DATA_HOME", folder + "/app-data");
    env.insert("XDG_CACHE_HOME", folder + "/app-cache");
    env.insert("WAYLAND_DISPLAY", "history-ipc-test");
    env.insert("QT_QPA_PLATFORM", "offscreen");
    env.insert("QT_QUICK_BACKEND", "software");
    env.insert("OMAFRAME_PROFILE_STARTUP", "1");
    QProcess app;
    app.setProcessEnvironment(env);
    app.start(HISTORY_APP_PATH, {"--studio"});
    QVERIFY(app.waitForStarted(5000));
    const auto cleanup = qScopeGuard([&] {
      app.kill();
      app.waitForFinished(5000);
    });
    const auto socket = runtime + "/omaframe-" +
                        QString::fromLatin1(QCryptographicHash::hash(
                                                QByteArray("history-ipc-test"),
                                                QCryptographicHash::Sha256)
                                                .toHex()
                                                .left(12));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(socket), 5000);
    QProcess command;
    command.setProcessEnvironment(env);
    command.start(HISTORY_APP_PATH, {"--history"});
    QVERIFY(command.waitForFinished(5000));
    QCOMPARE(command.exitCode(), 0);
    QByteArray log;
    QTRY_VERIFY_WITH_TIMEOUT(
        (log += app.readAllStandardError()).contains("history requested"),
        5000);
    QVERIFY2(!log.contains("failed to load") && !log.contains("is not defined"),
             log.constData());
  }
  void largeFolderBound() {
    for (int i = 0; i < 5000; ++i)
      write(folder + "/Omaframe-" + QString::number(i) + ".png");
    QElapsedTimer timer;
    timer.start();
    const auto rows = scan();
    QCOMPARE(rows.size(), 5000);
    QVERIFY2(timer.elapsed() < 5000,
             qPrintable(QString::number(timer.elapsed())));
    qInfo() << "5000 file scan ms:" << timer.elapsed();
  }
};
QTEST_MAIN(HistoryTest)
#include "history-test.moc"
