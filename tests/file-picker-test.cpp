#include "file-picker.hpp"
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace FilePicks;

// A portal that answers from a script and records what it was asked.
class FakeBus : public PortalBus {
public:
  void call(const Request &request, Done done) override {
    requests << request;
    if (deferred)
      pending = done;
    else
      done(delivered, code, results);
  }
  void answer(uint answerCode, const QVariantMap &answerResults) {
    pending(true, answerCode, answerResults);
  }
  QList<Request> requests;
  bool delivered = true, deferred = false;
  uint code = 0;
  QVariantMap results;
  Done pending;
};

// The real portal service, played on a private session bus.
class FakePortal : public QObject, protected QDBusContext {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.FileChooser")
public:
  QString parent, title, folder;
  bool terminated = false;
  QStringList filterLabels;
  QVariantMap options;
  uint code = 0;
  QStringList uris;
public slots:
  QDBusObjectPath OpenFile(const QString &parentWindow, const QString &windowTitle,
                           const QVariantMap &opts) {
    parent = parentWindow;
    title = windowTitle;
    options = opts;
    // A byte string with its NUL; constData() reads up to it.
    const auto bytes = opts.value("current_folder").toByteArray();
    terminated = bytes.endsWith('\0');
    folder = QString::fromLocal8Bit(bytes.constData());
    const auto filters = qdbus_cast<QList<Filter>>(
        opts.value("filters").value<QDBusArgument>());
    for (const auto &filter : filters)
      filterLabels << filter.label;
    QString sender = message().service().mid(1);
    sender.replace('.', '_');
    const QString path = "/org/freedesktop/portal/desktop/request/" + sender +
                         "/" + opts.value("handle_token").toString();
    // The Response follows the reply, as it does from a real portal.
    auto connection = this->connection();
    const auto answerCode = code;
    const auto answerUris = uris;
    QMetaObject::invokeMethod(this, [connection, path, answerCode, answerUris] {
      auto signal = QDBusMessage::createSignal(
          path, "org.freedesktop.portal.Request", "Response");
      signal << answerCode << QVariantMap{{"uris", answerUris}};
      connection.send(signal);
    }, Qt::QueuedConnection);
    return QDBusObjectPath(path);
  }
};

class FilePickerTest : public QObject {
  Q_OBJECT
  QTemporaryDir temp;
  QString pictures, movies, home;
  QString made(const QString &name) {
    const QString path = temp.filePath(name);
    QDir().mkpath(path);
    return path;
  }
private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName("OmaframePickerTests");
    QCoreApplication::setApplicationName("OmaframePickerTests");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.filePath("config"));
    QStandardPaths::setTestModeEnabled(true);
  }
  void init() { QSettings().clear(); }

  void everyRecordingTypeOpensTheVideoEditor_data() {
    QTest::addColumn<QString>("name");
    QTest::addColumn<bool>("recording");
    QTest::newRow("mp4") << "a.mp4" << true;
    QTest::newRow("upper") << "A.MKV" << true;
    QTest::newRow("webm") << "a.webm" << true;
    QTest::newRow("mov") << "a.mov" << true;
    QTest::newRow("png") << "a.png" << false;
    QTest::newRow("gif") << "a.gif" << false;
    QTest::newRow("none") << "a" << false;
  }
  void everyRecordingTypeOpensTheVideoEditor() {
    QFETCH(QString, name);
    QFETCH(bool, recording);
    QCOMPARE(isRecording("/x/" + name), recording);
  }
  void filtersAreShortAndCoverEverythingOpenAccepts() {
    const auto filters = mediaFilters();
    QCOMPARE(filters.size(), 3);
    QCOMPARE(filters[0].label, QString("Images and recordings"));
    QCOMPARE(filters[1].label, QString("Images"));
    QCOMPARE(filters[2].label, QString("Recordings"));
    QStringList all;
    for (const auto &glob : filters[0].globs) {
      QCOMPARE(glob.type, 0u);
      all << glob.pattern;
    }
    for (const auto &suffix : imageSuffixes() + recordingSuffixes()) {
      QVERIFY2(all.contains("*." + suffix), qPrintable(suffix));
      QVERIFY2(all.contains("*." + suffix.toUpper()), qPrintable(suffix));
    }
    QCOMPARE(filters[1].globs.size() + filters[2].globs.size(), filters[0].globs.size());
    QCOMPARE(nameFilters().size(), 3);
    QVERIFY(nameFilters()[1].startsWith("Images (*.png "));
    QVERIFY(nameFilters()[2].endsWith("*.avi)"));
  }
  void portalSignaturesMatchTheSpecification() {
    registerPortalTypes();
    QCOMPARE(QString(QDBusMetaType::typeToSignature(QMetaType::fromType<QList<Filter>>())),
             QString("a(sa(us))"));
    QCOMPARE(QString(QDBusMetaType::typeToSignature(QMetaType::fromType<Filter>())),
             QString("(sa(us))"));
  }

  void startsInTheFirstFolderThatExists() {
    const auto pictures = made("pictures"), shots = made("shots");
    const auto last = made("last");
    QCOMPARE(firstExistingFolder({last, shots, pictures}, "/home"), last);
    QCOMPARE(firstExistingFolder({temp.filePath("gone"), shots}, "/home"), shots);
    QCOMPARE(firstExistingFolder({"", temp.filePath("gone")}, "/home"), QString("/home"));
    QVERIFY(QFile(temp.filePath("file")).open(QIODevice::WriteOnly));
    QCOMPARE(firstExistingFolder({temp.filePath("file")}, "/home"), QString("/home"));
  }
  void imageContextPrefersLastThenScreenshotsThenPictures() {
    const auto candidates = startCandidates(Purpose::Open, false, "/last/i", "/last/v", "/shots", "/clips");
    QCOMPARE(candidates.mid(0, 2), (QStringList{"/last/i", "/shots"}));
    QVERIFY(!candidates.contains("/clips"));
    QVERIFY(!candidates.contains("/last/v"));
    QVERIFY(candidates.last().endsWith("Pictures"));
    QCOMPARE(startCandidates(Purpose::Open, false, "", "", "/shots", "/clips").first(),
             QString("/shots"));
  }
  void recordingContextPrefersTheVideoFolder() {
    const auto candidates = startCandidates(Purpose::Open, true, "/last/i", "/last/v", "/shots", "/clips");
    QCOMPARE(candidates.mid(0, 2), (QStringList{"/last/v", "/clips"}));
    QVERIFY(!candidates.contains("/shots"));
    QCOMPARE(startCandidates(Purpose::Open, true, "/last/i", "", "/shots", "/clips").first(),
             QString("/clips"));
  }
  void folderPickersStartInTheFolderTheyChange() {
    QCOMPARE(startCandidates(Purpose::ScreenshotFolder, false, "/last/i", "", "/shots", "/clips").first(),
             QString("/shots"));
    QCOMPARE(startCandidates(Purpose::RecordingFolder, false, "/last/i", "", "/shots", "/clips").first(),
             QString("/clips"));
  }
  void neverStartsInTheWorkingDirectory() {
    const auto shots = made("shots");
    QDir::setCurrent(made("elsewhere"));
    FilePicker picker([&] { return QString(temp.filePath("missing-shots")); },
                      [&] { return QString(temp.filePath("missing-clips")); },
                      std::make_unique<FakeBus>());
    const auto start = picker.startFolder("open", false).toLocalFile();
    QVERIFY(start != QDir::currentPath());
    QVERIFY(QFileInfo(start).isDir());
    FilePicker configured([&] { return shots; }, [&] { return shots; },
                          std::make_unique<FakeBus>());
    QCOMPARE(configured.startFolder("open", false).toLocalFile(), shots);
    QCOMPARE(configured.startFolder("screenshots", false), QUrl::fromLocalFile(shots));
  }

  void buildsAnOpenRequest() {
    const auto request = buildRequest(Purpose::Open, "Open", QString(), "/home/u/Pictures", "tok1");
    QCOMPARE(request.method, QString("OpenFile"));
    QVERIFY(request.parent.isEmpty());
    QCOMPARE(request.title, QString("Open"));
    QCOMPARE(request.options.value("handle_token").toString(), QString("tok1"));
    QVERIFY(request.options.value("modal").toBool());
    QVERIFY(!request.options.value("multiple").toBool());
    QVERIFY(!request.options.contains("directory"));
    QCOMPARE(request.options.value("current_folder").toByteArray(),
             QByteArray("/home/u/Pictures\0", 17));
    const auto filters = request.options.value("filters").value<QList<Filter>>();
    QCOMPARE(filters.size(), 3);
    QCOMPARE(request.options.value("current_filter").value<Filter>().label,
             filters.first().label);
  }
  void buildsAFolderRequest() {
    const auto request = buildRequest(Purpose::ScreenshotFolder, "Save", "", "/home/u/Pictures", "tok2");
    QVERIFY(request.options.value("directory").toBool());
    QVERIFY(!request.options.contains("filters"));
    QVERIFY(request.parent.isEmpty());
  }
  void parsesResponses() {
    auto chosen = parseResponse(0, {{"uris", QStringList{"file:///tmp/a%20b.png"}}});
    QCOMPARE(chosen.status, Reply::Chosen);
    QCOMPARE(chosen.urls, (QList<QUrl>{QUrl::fromLocalFile("/tmp/a b.png")}));
    QCOMPARE(parseResponse(0, {{"uris", QVariantList{"file:///tmp/a.png"}}}).status, Reply::Chosen);
    QCOMPARE(parseResponse(1, {}).status, Reply::Cancelled);
    QCOMPARE(parseResponse(2, {}).status, Reply::Cancelled);
    QCOMPARE(parseResponse(0, {}).status, Reply::Failed);
    QCOMPARE(parseResponse(0, {{"uris", QStringList{"https://example.com/a.png"}}}).status,
             Reply::Failed);
  }

  void pickerReportsTheChosenFileAndRemembersItsFolder() {
    const auto shots = made("shots"), clips = made("clips"), recent = made("recent");
    auto bus = std::make_unique<FakeBus>();
    auto *fake = bus.get();
    fake->results = {{"uris", QStringList{QUrl::fromLocalFile(recent + "/x.png").toString()}}};
    FilePicker picker([&] { return shots; }, [&] { return clips; }, std::move(bus));
    QSignalSpy chosen(&picker, &FilePicker::fileChosen);
    QSignalSpy fallback(&picker, &FilePicker::fallback);
    picker.openMedia(nullptr, false);
    QCOMPARE(chosen.count(), 1);
    QCOMPARE(chosen.first().first().toUrl(), QUrl::fromLocalFile(recent + "/x.png"));
    QCOMPARE(fallback.count(), 0);
    QVERIFY(!picker.busy());
    QCOMPARE(fake->requests.first().title, QString("Open an image or recording"));
    QCOMPARE(fake->requests.first().options.value("current_folder").toByteArray(),
             (shots + '\0').toLocal8Bit());
    // The next dialog starts where the last file came from, per kind of file.
    picker.openMedia(nullptr, false);
    QCOMPARE(fake->requests.last().options.value("current_folder").toByteArray(),
             (recent + '\0').toLocal8Bit());
    picker.openMedia(nullptr, true);
    QCOMPARE(fake->requests.last().options.value("current_folder").toByteArray(),
             (clips + '\0').toLocal8Bit());
  }
  void pickerStaysQuietOnCancel() {
    auto bus = std::make_unique<FakeBus>();
    bus->code = 1;
    FilePicker picker([] { return QString(); }, [] { return QString(); }, std::move(bus));
    QSignalSpy any(&picker, &FilePicker::fileChosen), fallback(&picker, &FilePicker::fallback);
    picker.openMedia(nullptr, false);
    QCOMPARE(any.count() + fallback.count(), 0);
    QVERIFY(!picker.busy());
  }
  void pickerFallsBackWhenThePortalIsMissingOrAnswersNothing() {
    for (const bool delivered : {false, true}) {
      auto bus = std::make_unique<FakeBus>();
      bus->delivered = delivered;
      FilePicker picker([] { return QString(); }, [] { return QString(); }, std::move(bus));
      QSignalSpy fallback(&picker, &FilePicker::fallback);
      picker.openMedia(nullptr, false);
      QCOMPARE(fallback.count(), 1);
      QCOMPARE(fallback.first().first().toString(), QString("open"));
      QVERIFY(!picker.busy());
    }
  }
  void pickerStaysQuietWhenTheDialogIsClosedSomeOtherWay() {
    auto bus = std::make_unique<FakeBus>();
    bus->code = 2;
    FilePicker picker([] { return QString(); }, [] { return QString(); }, std::move(bus));
    QSignalSpy fallback(&picker, &FilePicker::fallback);
    picker.openMedia(nullptr, false);
    QCOMPARE(fallback.count(), 0);
    QVERIFY(!picker.busy());
  }
  void pickerIgnoresASecondRequestWhileOneIsOpen() {
    auto bus = std::make_unique<FakeBus>();
    auto *fake = bus.get();
    fake->deferred = true;
    FilePicker picker([] { return QString(); }, [] { return QString(); }, std::move(bus));
    QSignalSpy busy(&picker, &FilePicker::busyChanged);
    picker.openMedia(nullptr, false);
    picker.openMedia(nullptr, false);
    picker.chooseFolder(nullptr, "screenshots");
    QCOMPARE(fake->requests.size(), 1);
    QVERIFY(picker.busy());
    fake->answer(1, {});
    QVERIFY(!picker.busy());
    QCOMPARE(busy.count(), 2);
  }
  void pickerReportsAChosenFolder() {
    const auto clips = made("clips"), chosen = made("chosen");
    auto bus = std::make_unique<FakeBus>();
    auto *fake = bus.get();
    fake->results = {{"uris", QStringList{QUrl::fromLocalFile(chosen).toString()}}};
    FilePicker picker([] { return QString(); }, [&] { return clips; }, std::move(bus));
    QSignalSpy folder(&picker, &FilePicker::folderChosen), fallback(&picker, &FilePicker::fallback);
    picker.chooseFolder(nullptr, "recordings");
    QVERIFY(fake->requests.first().options.value("directory").toBool());
    QCOMPARE(fake->requests.first().title, QString("Save recordings and clips in"));
    QCOMPARE(fake->requests.first().options.value("current_folder").toByteArray(),
             (clips + '\0').toLocal8Bit());
    QCOMPARE(folder.count(), 1);
    QCOMPARE(folder.first().at(0).toString(), QString("recordings"));
    QCOMPARE(folder.first().at(1).toUrl(), QUrl::fromLocalFile(chosen));
    // A portal too old for folder mode answers with a file.
    QVERIFY(QFile(temp.filePath("old.png")).open(QIODevice::WriteOnly));
    fake->results = {{"uris", QStringList{QUrl::fromLocalFile(temp.filePath("old.png")).toString()}}};
    picker.chooseFolder(nullptr, "recordings");
    QCOMPARE(fallback.count(), 1);
    QCOMPARE(folder.count(), 1);
  }
  void qmlDialogRemembersTheFolderItOpenedFrom() {
    const auto recent = made("recent"), clips = made("clips");
    FilePicker picker([&] { return recent; }, [&] { return clips; }, std::make_unique<FakeBus>());
    picker.rememberOpened(QUrl::fromLocalFile(clips + "/a.mp4"));
    QCOMPARE(picker.startFolder("open", true), QUrl::fromLocalFile(clips));
    picker.rememberOpened(QUrl("https://example.com/a.png"));
    QCOMPARE(picker.startFolder("open", false), QUrl::fromLocalFile(recent));
  }

  // The real D-Bus path, against a stand-in portal on a private session bus.
  void talksToThePortalOverDBus() {
    if (qEnvironmentVariableIsEmpty("DBUS_SESSION_BUS_ADDRESS"))
      QSKIP("needs a private session bus (run under dbus-run-session)");
    const auto shots = made("dbus-shots"), chosen = made("dbus-chosen");
    FilePicker picker([&] { return shots; }, [&] { return shots; });
    QSignalSpy fileChosen(&picker, &FilePicker::fileChosen);
    QSignalSpy folderChosen(&picker, &FilePicker::folderChosen);
    QSignalSpy fallback(&picker, &FilePicker::fallback);

    // No portal on the bus yet: the call fails and QML takes over.
    picker.openMedia(nullptr, false);
    QTRY_COMPARE(fallback.count(), 1);
    QVERIFY(!picker.busy());

    auto portalBus = QDBusConnection::connectToBus(
        QDBusConnection::SessionBus, "fake-portal");
    FakePortal portal;
    QVERIFY(portalBus.registerObject("/org/freedesktop/portal/desktop", &portal,
                                     QDBusConnection::ExportAllSlots));
    QVERIFY(portalBus.registerService("org.freedesktop.portal.Desktop"));

    portal.uris = {QUrl::fromLocalFile(chosen + "/shot.png").toString()};
    picker.openMedia(nullptr, false);
    QTRY_COMPARE(fileChosen.count(), 1);
    QCOMPARE(fileChosen.first().first().toUrl(), QUrl::fromLocalFile(chosen + "/shot.png"));
    QCOMPARE(portal.title, QString("Open an image or recording"));
    QCOMPARE(portal.folder, shots);
    QVERIFY(portal.terminated);
    QCOMPARE(portal.filterLabels,
             (QStringList{"Images and recordings", "Images", "Recordings"}));
    QVERIFY(portal.options.value("modal").toBool());

    portal.uris = {QUrl::fromLocalFile(chosen).toString()};
    picker.chooseFolder(nullptr, "screenshots");
    QTRY_COMPARE(folderChosen.count(), 1);
    QVERIFY(portal.options.value("directory").toBool());

    portal.code = 1;
    portal.uris.clear();
    picker.openMedia(nullptr, false);
    QTRY_VERIFY(!picker.busy());
    QTest::qWait(50);
    QCOMPARE(fileChosen.count(), 1);
    QCOMPARE(fallback.count(), 1);
    QDBusConnection::disconnectFromBus("fake-portal");
  }
};

QTEST_MAIN(FilePickerTest)
#include "file-picker-test.moc"
