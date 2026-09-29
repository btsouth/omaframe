#include "recording.hpp"
#include "shortcuts.hpp"
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <cstring>

class RecordingTest:public QObject {
 Q_OBJECT
 QTemporaryDir temp;
 QByteArray oldPath;
 void executable(const QString &name,const QByteArray &body) {
   QFile f(temp.filePath(name));QVERIFY(f.open(QIODevice::WriteOnly));f.write(body);f.close();
   QVERIFY(f.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
 }
private slots:
 void initTestCase() {
   qputenv("XDG_CONFIG_HOME",temp.filePath("config").toUtf8());
   QCoreApplication::setOrganizationName("Omaframe-test");QCoreApplication::setApplicationName("Recording");
   QSettings().clear();QSettings().setValue("videoDirectory",temp.filePath("videos"));
   oldPath=qgetenv("PATH");qputenv("PATH",temp.path().toUtf8()+":"+oldPath);
   executable("hyprctl",R"(#!/bin/sh
if [ "$2" = monitors ]; then
 if [ "$OMAFRAME_TEST_DUAL_DISPLAY" = 1 ]; then
  echo '[{"name":"A","width":1280,"height":800,"x":0,"y":0,"scale":1,"reserved":[0,24,0,0]},{"name":"B","width":1920,"height":1080,"x":-1920,"y":0,"scale":1,"reserved":[0,24,0,0]}]'
 else echo '[{"name":"A","width":1280,"height":800,"x":0,"y":0,"scale":1,"reserved":[0,24,0,0]}]'; fi
elif [ "$2" = binds ]; then
 if [ -n "$OMAFRAME_TEST_AUTO_SHORTCUT" ]; then
   if [ -e "$OMAFRAME_TEST_AUTO_SHORTCUT" ]; then
     echo '[{"key":"PRINT","modmask":0,"description":"Screenshot with Omaframe","dispatcher":"__lua","arg":"148","submap":""},{"key":"PRINT","modmask":8,"description":"Record with Omaframe","dispatcher":"__lua","arg":"152","submap":""}]'
   else
     echo '[{"key":"PRINT","modmask":0,"description":"Screenshot","dispatcher":"__lua","arg":"148","submap":""},{"key":"PRINT","modmask":8,"description":"Screenrecording","dispatcher":"__lua","arg":"152","submap":""}]'
   fi
 elif [ -n "$OMAFRAME_TEST_NO_STOP_BIND" ]; then echo '[]';
 else echo '[{"key":"Print","modmask":8,"dispatcher":"exec","arg":"$HOME/.local/bin/omaframe --stop-recording || omaframe --record","submap":""}]'; fi
elif [ "$1" = eval ] && [ -n "$OMAFRAME_TEST_AUTO_SHORTCUT" ]; then
 if printf %s "$2" | grep -q "assert"; then
  if [ -n "$OMAFRAME_TEST_NO_BIND_API" ]; then echo unavailable; exit 1; fi
  echo ok; exit 0
 fi
 if printf %s "$2" | grep -q "o.rebind"; then echo unavailable; exit 1; fi
 : > "$OMAFRAME_TEST_AUTO_SHORTCUT"
 echo ok
elif [ "$2" = layers ] && [ -n "$OMAFRAME_TEST_LAYERS" ]; then
 echo "$OMAFRAME_TEST_LAYERS"
else
 echo '{}'
fi
)");
   executable("pactl",R"(#!/bin/sh
case "$1" in
get-default-source) echo clean_desktop_microphone;;
get-default-sink) if [ -z "$OMAFRAME_TEST_NO_SINK" ]; then echo speakers; fi;;
*) echo '[{"name":"clean_desktop_microphone","description":"Clean microphone"}]';;
esac
)");
   executable("pgrep","#!/bin/sh\nexit 1\n");
   QProcess video;video.start("ffmpeg",{"-hide_banner","-loglevel","error","-f","lavfi","-i","color=c=green:s=64x64:r=10","-f","lavfi","-i","sine=frequency=440:sample_rate=48000","-c:a","aac","-t","1","-c:v","libx264","-threads","1",temp.filePath("fixture.mp4")});
   QVERIFY(video.waitForFinished(10000));QCOMPARE(video.exitCode(),0);
   qputenv("OMAFRAME_TEST_FIXTURE",temp.filePath("fixture.mp4").toUtf8());
   executable("gpu-screen-recorder",R"(#!/usr/bin/python3
import os,sys,signal,time,shutil,socket,json
if '--version' in sys.argv:
    print(os.environ.get('OMAFRAME_TEST_RECORDER_VERSION', '6.1.3'))
    sys.exit(0)
path=sys.argv[sys.argv.index('-o')+1]
if os.environ.get('OMAFRAME_TEST_HEADER_ONLY'):
    open(path,'wb').write(b'x'*88)
else:
    shutil.copyfile(os.environ['OMAFRAME_TEST_FIXTURE'],path)
    open(path+'.ts','w').write('monotonic_microsec\trealtime_microsec\n123456\t123456\n')
signal.signal(signal.SIGINT,lambda *_:sys.exit(0))
mode=os.environ.get('OMAFRAME_TEST_PAUSE_REPLY', 'ok')
default_mode=mode
server=socket.socket(socket.AF_UNIX)
if mode == 'missing':
    while True: time.sleep(.05)
server.bind(sys.argv[sys.argv.index('-ipc')+1])
server.listen()
while True:
    client,_=server.accept()
    data=b''
    while b'\n' not in data: data+=client.recv(4096)
    request=json.loads(data)
    assert request['id']==1 and request['name']=='set-paused'
    assert isinstance(request['data'],bool)
    with open(path+'.controls','a') as log:
        log.write(json.dumps({'pid':os.getpid(),'paused':request['data']})+'\n')
    mode=default_mode
    if os.path.exists(path+'.pause-mode'):
        mode=open(path+'.pause-mode').read().strip()
    if mode == 'silent':
        time.sleep(10)
    elif mode == 'malformed':
        client.sendall(b'{"id":999,"result":"ok"}\n')
    elif mode == 'fragmented':
        client.sendall(b'{"id":1,')
        time.sleep(.1)
        client.sendall(b'"result":"ok"}\n')
    elif mode != 'disconnect':
        reply={'id':1,'result':'error' if mode=='reject' else 'ok','data':'Fixture rejection' if mode=='reject' else ''}
        client.sendall((json.dumps(reply)+'\n').encode())
    client.close()
)");
 }
 void recordingControlPlacement_data() {
   QTest::addColumn<QRect>("target");QTest::addColumn<QString>("second");QTest::addColumn<QString>("display");QTest::addColumn<QRect>("expected");
   // A is 1280x800 with a 24 px bar. The control is 232x48 with a 16 px gap.
   QTest::newRow("region-below")<<QRect(100,100,800,500)<<QString()<<QString("A")<<QRect(383,616,232,48);
   QTest::newRow("region-above")<<QRect(100,300,800,480)<<QString()<<QString("A")<<QRect(383,236,232,48);
   QTest::newRow("tall-region-right")<<QRect(100,40,300,760)<<QString()<<QString("A")<<QRect(416,40,232,48);
   QTest::newRow("tall-region-left")<<QRect(900,40,380,760)<<QString()<<QString("A")<<QRect(652,40,232,48);
   QTest::newRow("full-single")<<QRect(0,0,1280,800)<<QString()<<QString()<<QRect();
   QTest::newRow("near-full-single")<<QRect(0,0,1280,775)<<QString()<<QString()<<QRect();
   // The other display's edge that faces the recording, below its bar.
   QTest::newRow("full-left-neighbor")<<QRect(0,0,1280,800)<<QString("left")<<QString("B")<<QRect(-248,40,232,48);
   QTest::newRow("full-right-neighbor")<<QRect(0,0,1280,800)<<QString("right")<<QString("B")<<QRect(1296,40,232,48);
   QTest::newRow("full-below-neighbor")<<QRect(0,0,1280,800)<<QString("below")<<QString("B")<<QRect(523,840,232,48);
   QTest::newRow("region-too-big-uses-neighbor")<<QRect(0,100,1280,700)<<QString("right")<<QString("B")<<QRect(1296,100,232,48);
 }
 void recordingControlPlacement() {
   QFETCH(QRect,target);QFETCH(QString,second);QFETCH(QString,display);QFETCH(QRect,expected);
   QList<Recording::Display> screens{{"A",QRect(0,0,1280,800),{},{},QMargins(0,24,0,0)}};
   if(second=="left")screens.append({"B",QRect(-1920,0,1920,1080),{},{},QMargins(0,24,0,0)});
   if(second=="right")screens.append({"B",QRect(1280,0,1920,1080),{},{},QMargins(0,24,0,0)});
   if(second=="below")screens.append({"B",QRect(0,800,1920,1080),{},{},QMargins(0,24,0,0)});
   const auto p=Recording::placeStop(screens,"A",target);
   QCOMPARE(p.display,display);QCOMPARE(p.bounds,expected);
   if(!p.bounds.isEmpty()){
     QVERIFY(!p.bounds.intersects(target));
     for(const auto &d:screens)if(d.name==p.display)QVERIFY(d.bounds.marginsRemoved(d.reserved).contains(p.bounds));
   }
 }
 void countdownSitsBelowTheBarOnTheRecordedDisplay() {
   QList<Recording::Display> screens{{"A",QRect(0,0,1280,800),{},{},QMargins(0,24,0,0)}};
   const auto p=Recording::placeCountdown(screens,"A");
   QCOMPARE(p.display,QString("A"));QCOMPARE(p.bounds,QRect(429,40,420,48));
   QVERIFY(Recording::placeCountdown(screens,"missing").bounds.isEmpty());
 }
 void audioFlagsAreExplicitAndCombined() {
   auto a=Recording::arguments("800x600+-1200+20","/tmp/name with spaces.mp4","speakers.monitor","clean_desktop_microphone",false);
   QCOMPARE(a[a.indexOf("-a")+1],QString("speakers.monitor|clean_desktop_microphone"));
   QCOMPARE(a[a.indexOf("-cursor")+1],QString("no"));
   QCOMPARE(a[a.indexOf("-o")+1],QString("/tmp/name with spaces.mp4"));
   QCOMPARE(a[a.indexOf("-write-first-frame-ts")+1],QString("yes"));
   QVERIFY(!Recording::arguments("A","a.mp4",{}, {},true).contains("-a"));
   // The Omarchy bar finds recorders whose command starts with the bare name.
   const auto [program,command]=Recording::recorderCommand({"-w","A","-o","a b.mp4"});
   QCOMPARE(program,QString("bash"));
   QVERIFY(command[1].startsWith("exec -a gpu-screen-recorder gpu-screen-recorder "));
   QCOMPARE(command.mid(3),QStringList({"-w","A","-o","a b.mp4"}));
   QProcess named;named.start(program,QStringList{"-c","exec -a gpu-screen-recorder sleep 5","x"});
   QVERIFY(named.waitForStarted(3000));
   QTRY_VERIFY_WITH_TIMEOUT(QFile(QString("/proc/%1/cmdline").arg(named.processId())).open(QIODevice::ReadOnly),2000);
   QFile cmdline(QString("/proc/%1/cmdline").arg(named.processId()));QVERIFY(cmdline.open(QIODevice::ReadOnly));
   QTRY_VERIFY_WITH_TIMEOUT((cmdline.seek(0),cmdline.readAll().startsWith("gpu-screen-recorder")),2000);
   named.kill();named.waitForFinished();
 }
 void shortcutsAreRecognizedOnAnyKeyAndStockOnesAreReplaceable() {
   using Shortcuts::Action;
   auto binds=[](const char *json){return QJsonDocument::fromJson(json).array();};
   QCOMPARE(Shortcuts::omaframeKey(binds(R"([{"key":"Print","modmask":8,"dispatcher":"exec","arg":"omaframe --record"}])"),Action::Record),QString("Alt+Print"));
   QCOMPARE(Shortcuts::omaframeKey(binds(R"([{"key":"PRINT","modmask":8,"description":"Record with Omaframe","dispatcher":"__lua","arg":"152"}])"),Action::Record),QString("Alt+Print"));
   QCOMPARE(Shortcuts::omaframeKey(binds(R"([{"key":"r","modmask":65,"dispatcher":"exec","arg":"/usr/bin/omaframe --record"}])"),Action::Record),QString("Super+Shift+R"));
   QCOMPARE(Shortcuts::omaframeKey(binds(R"([{"key":"Print","modmask":0,"dispatcher":"exec","arg":"omaframe --capture"}])"),Action::Screenshot),QString("Print"));
   QCOMPARE(Shortcuts::omaframeKey(binds(R"([{"key":"Print","modmask":0,"dispatcher":"exec","arg":"omaframe"}])"),Action::Screenshot),QString("Print"));
   QVERIFY(Shortcuts::omaframeKey(binds(R"([{"key":"Print","modmask":0,"dispatcher":"exec","arg":"omaframe --capture"}])"),Action::Record).isEmpty());
   QVERIFY(Shortcuts::omaframeKey(binds(R"([{"key":"Print","modmask":8,"dispatcher":"exec","arg":"other-app --record"}])"),Action::Record).isEmpty());
   QVERIFY(Shortcuts::omaframeKey(binds(R"([{"key":"Print","modmask":8,"dispatcher":"exec","arg":"omaframe --record","submap":"resize"}])"),Action::Record).isEmpty());
   const auto stock=binds(R"([{"key":"PRINT","modmask":0,"description":"Screenshot","dispatcher":"__lua","arg":"148"},{"key":"PRINT","modmask":8,"description":"Screenrecording","dispatcher":"__lua","arg":"152"}])");
   QCOMPARE(Shortcuts::defaultKeyState(stock,Action::Screenshot),QString("stock"));
   QCOMPARE(Shortcuts::defaultKeyState(stock,Action::Record),QString("stock"));
   QCOMPARE(Shortcuts::defaultKeyState(binds("[]"),Action::Record),QString("none"));
   QCOMPARE(Shortcuts::defaultKeyState(binds(R"([{"key":"PRINT","modmask":8,"description":"My recorder","dispatcher":"__lua","arg":"1"}])"),Action::Record),QString("custom"));
   QCOMPARE(Shortcuts::defaultKeyState(binds(R"([{"key":"PRINT","modmask":8,"description":"Record with Omaframe","dispatcher":"__lua","arg":"1"}])"),Action::Record),QString("omaframe"));
 }
 void fullDisplayOnOneMonitorKeepsEveryControlOutOfTheVideo() {
   qputenv("OMAFRAME_TEST_NO_STOP_BIND","1");
   Recorder r;r.prepare();QTRY_COMPARE(r.state(),QString("setup"));
   r.selectDisplay(0);
   QVERIFY(!r.hasControl());QVERIFY(r.needsStopShortcut());QVERIFY(!r.canStart());
   QVERIFY(r.status().contains("stop shortcut"));
   r.setStopKey("Alt+Print");
   QVERIFY(r.canStart());QVERIFY(r.controlLocation().contains("nothing from Omaframe is in the video"));
   qunsetenv("OMAFRAME_TEST_NO_STOP_BIND");
   // A pill counts down on the recorded display, then must be gone before
   // capture. While the stub still reports it over the display, launch waits
   // and then refuses.
   qputenv("OMAFRAME_TEST_LAYERS",R"({"A":{"levels":{"3":[{"namespace":"omaframe-record-control","x":429,"y":40,"w":420,"h":48}]}}})");
   QSignalSpy control(&r,&Recorder::controlRequested);
   r.setCountdown(1);r.start();
   QCOMPARE(control.count(),1);QVERIFY(r.countdownOnly());
   QCOMPARE(r.visibleControl().bounds,QRect(429,40,420,48));
   QTRY_COMPARE_WITH_TIMEOUT(r.state(),QString("failed"),6000);
   QVERIFY(r.status().contains("still over the recording area"));QVERIFY(r.savedPath().isEmpty());
   qunsetenv("OMAFRAME_TEST_LAYERS");
   r.setStopKey("Alt+Print");
   r.selectDisplay(0);r.setCountdown(0);r.start();
   QTRY_COMPARE_WITH_TIMEOUT(r.state(),QString("recording"),5000);
   QVERIFY(!r.hasControl());
   r.stop();QTRY_COMPARE_WITH_TIMEOUT(r.state(),QString("saved"),5000);
 }
 void fullDisplayUsesUnrecordedMonitorWhenAvailable() {
   qputenv("OMAFRAME_TEST_DUAL_DISPLAY","1");
   qputenv("OMAFRAME_TEST_LAYERS",R"({"B":{"levels":{"3":[{"namespace":"omaframe-record-control","x":-248,"y":40,"w":232,"h":48}]}}})");
   Recorder r;r.prepare();QTRY_COMPARE(r.state(),QString("setup"));
   r.selectDisplay(0);
   QVERIFY(r.canStart());QVERIFY(r.safeStop());
   QCOMPARE(r.control().display,QString("B"));
   QCOMPARE(r.control().bounds,QRect(-248,40,232,48));
   QVERIFY(r.controlLocation().contains("outside the video"));
   r.setCountdown(0);r.start();
   QTRY_COMPARE_WITH_TIMEOUT(r.state(),QString("recording"),5000);
   r.stop();QTRY_COMPARE_WITH_TIMEOUT(r.state(),QString("saved"),5000);
   qunsetenv("OMAFRAME_TEST_DUAL_DISPLAY");
   qunsetenv("OMAFRAME_TEST_LAYERS");
 }
 void shortcutInstallPreservesCustomBindings() {
   using Shortcuts::Action;
   const QString config=temp.filePath("onboarding/hypr");
   QVERIFY(QDir().mkpath(config));
   QFile startup(config+"/hyprland.lua");QVERIFY(startup.open(QIODevice::WriteOnly));
   startup.write("require(\"hypr.bindings\")\n");startup.close();
   QFile bindings(config+"/bindings.lua");QVERIFY(bindings.open(QIODevice::WriteOnly));
   bindings.write("-- My other shortcuts\n-- omaframe:recording-shortcut:start\no.rebind(\"ALT + PRINT\", \"Record with Omaframe\", \"old --record\")\n-- omaframe:recording-shortcut:end\n");bindings.close();
   QString error,backup;
   QVERIFY2(Shortcuts::install(config,"/bin/true",{Action::Screenshot},&error,&backup),qPrintable(error));
   QVERIFY(QFileInfo::exists(backup));
   QVERIFY(bindings.open(QIODevice::ReadOnly));
   auto installed=bindings.readAll();bindings.close();
   // The legacy block is replaced by one block holding both shortcuts.
   QVERIFY(installed.startsWith("-- My other shortcuts\n"));
   QCOMPARE(installed.count("omaframe:shortcuts:start"),1);
   QVERIFY(!installed.contains("recording-shortcut"));
   QVERIFY(!installed.contains("o.rebind"));
   QVERIFY(installed.contains("hl.unbind(\"PRINT\")"));
   QVERIFY(installed.contains("hl.unbind(\"ALT + PRINT\")"));
   QVERIFY(installed.contains("o.bind(\"PRINT\", \"Screenshot with Omaframe\", \"'/bin/true' --capture\")"));
   QVERIFY(installed.contains("o.bind(\"ALT + PRINT\", \"Record with Omaframe\", \"'/bin/true' --record\")"));
   // Installing again changes nothing and makes no second backup.
   QString again;
   QVERIFY(Shortcuts::install(config,"/bin/true",{Action::Screenshot,Action::Record},&error,&again));
   QVERIFY(again.isEmpty());
   QVERIFY(bindings.open(QIODevice::ReadOnly));QCOMPARE(bindings.readAll(),installed);bindings.close();
   QVERIFY(bindings.open(QIODevice::WriteOnly|QIODevice::Truncate));
   bindings.write("o.rebind(\"ALT + PRINT\", \"My recorder\", \"other-recorder\")\n");bindings.close();
   QVERIFY(!Shortcuts::install(config,"/bin/true",{Action::Record},&error));
   QVERIFY(error.contains("custom binding"));
   QVERIFY(bindings.open(QIODevice::ReadOnly));
   QCOMPARE(bindings.readAll(),QByteArray("o.rebind(\"ALT + PRINT\", \"My recorder\", \"other-recorder\")\n"));
   bindings.close();
   // A block with its end marker deleted is never guessed at.
   const QByteArray broken="-- omaframe:shortcuts:start\no.rebind(\"PRINT\", \"Screenshot with Omaframe\", \"omaframe --capture\")\n-- my own line\n";
   QVERIFY(bindings.open(QIODevice::WriteOnly|QIODevice::Truncate));bindings.write(broken);bindings.close();
   QVERIFY(!Shortcuts::install(config,"/bin/true",{Action::Record},&error));
   QVERIFY(error.contains("missing its end"));
   QVERIFY(bindings.open(QIODevice::ReadOnly));QCOMPARE(bindings.readAll(),broken);bindings.close();
   QFile plain(config+"/hyprland.lua");QVERIFY(plain.open(QIODevice::WriteOnly|QIODevice::Truncate));plain.write("-- nothing\n");plain.close();
   QVERIFY(!Shortcuts::install(config,"/bin/true",{Action::Screenshot},&error));
   QVERIFY(error.contains("does not load"));
 }
 void recordingNeverEditsShortcutsWithoutBeingAsked() {
   const QString config=temp.filePath("config/hypr");
   QVERIFY(QDir().mkpath(config));
   QFile startup(config+"/hyprland.lua");QVERIFY(startup.open(QIODevice::WriteOnly));
   startup.write("require(\"hypr.bindings\")\n");startup.close();
   QFile bindings(config+"/bindings.lua");QVERIFY(bindings.open(QIODevice::WriteOnly));
   bindings.write("-- existing user shortcuts\n");bindings.close();
   const QByteArray flag=temp.filePath("shortcut-active").toUtf8();
   qputenv("OMAFRAME_TEST_AUTO_SHORTCUT",flag);
   Recorder r;r.prepare();QTRY_COMPARE(r.state(),QString("setup"));
   r.selectDisplay(0);
   QVERIFY(!r.stopShortcut());QVERIFY(!r.canStart());
   QVERIFY(!QFile::exists(QString::fromUtf8(flag)));
   QVERIFY(bindings.open(QIODevice::ReadOnly));QCOMPARE(bindings.readAll(),QByteArray("-- existing user shortcuts\n"));bindings.close();
   // Setup changes the stock keys only when the user asks for it.
   ShortcutSetup s;QObject::connect(&s,&ShortcutSetup::changed,&r,[&]{if(s.available()&&!s.checking())r.setStopKey(s.recordKey());});
   s.refresh();QTRY_VERIFY(s.available()&&!s.checking());
   QCOMPARE(s.screenshotState(),QString("stock"));QCOMPARE(s.recordState(),QString("stock"));QVERIFY(s.canSetUp());
   s.setUp();QTRY_VERIFY_WITH_TIMEOUT(!s.checking(),5000);
   QVERIFY2(s.ready(),qPrintable(s.message()));
   QCOMPARE(s.recordKey(),QString("Alt+Print"));QCOMPARE(s.screenshotKey(),QString("Print"));
   QVERIFY(s.message().contains("now open Omaframe"));
   QVERIFY(r.canStart());
   QVERIFY(bindings.open(QIODevice::ReadOnly));
   const QByteArray content=bindings.readAll();bindings.close();
   QVERIFY(content.startsWith("-- existing user shortcuts\n"));
   QVERIFY(content.contains("Screenshot with Omaframe"));QVERIFY(content.contains("Record with Omaframe"));
   qunsetenv("OMAFRAME_TEST_AUTO_SHORTCUT");
 }
 void unsupportedShortcutApiDoesNotTouchConfig() {
   const QString config=temp.filePath("config/hypr");
   QVERIFY(QDir().mkpath(config));
   QFile bindings(config+"/bindings.lua");
   QVERIFY(bindings.open(QIODevice::WriteOnly));
   const QByteArray original="-- user shortcuts must survive\n";
   bindings.write(original);bindings.close();
   qputenv("OMAFRAME_TEST_AUTO_SHORTCUT",temp.filePath("unsupported-api").toUtf8());
   qputenv("OMAFRAME_TEST_NO_BIND_API","1");
   ShortcutSetup s;s.refresh();QTRY_VERIFY(s.available()&&!s.checking());
   QVERIFY(s.canSetUp());s.setUp();QTRY_VERIFY(!s.checking());
   QVERIFY(!s.ready());QVERIFY(s.message().contains("left unchanged"));
   QVERIFY(bindings.open(QIODevice::ReadOnly));QCOMPARE(bindings.readAll(),original);
   qunsetenv("OMAFRAME_TEST_AUTO_SHORTCUT");qunsetenv("OMAFRAME_TEST_NO_BIND_API");
 }
 void unavailableAudioWarningClearsWhenTurnedOff() {
   qputenv("OMAFRAME_TEST_NO_SINK", "1");
   Recorder r;
   r.prepare();
   QTRY_COMPARE(r.state(),QString("setup"));
   qunsetenv("OMAFRAME_TEST_NO_SINK");
   r.setDesktopAudio(true);
   QVERIFY(r.status().contains("Desktop audio is unavailable"));
   r.selectDisplay(0);
   QVERIFY(r.status().contains("Desktop audio is unavailable"));
   QVERIFY(!r.canStart());
   r.setDesktopAudio(false);
   QCOMPARE(r.status(),QString("Ready to record."));
   QVERIFY(r.canStart());
   r.prepare();
   QTRY_COMPARE(r.state(),QString("setup"));
   QCOMPARE(r.status(),QString("Choose what to record."));
   QVERIFY(!r.canStart());
 }
 void ownProcessStopsThenHandsOffValidClip() {
   Recorder r;QSignalSpy finished(&r,&Recorder::completed);
   r.prepare();QTRY_COMPARE(r.state(),QString("setup"));
   QCOMPARE(r.microphone(),0);r.selectDisplay(0);QVERIFY(!r.safeStop());QCOMPARE(r.stopKey(),QString("Alt+Print"));
   r.setCountdown(0);r.setMicAudio(true);r.setDesktopAudio(true);r.start();
   QTRY_COMPARE_WITH_TIMEOUT(r.state(),QString("recording"),5000);
   QVERIFY(r.active());QVERIFY(finished.isEmpty());
   r.stop();QTRY_COMPARE_WITH_TIMEOUT(finished.count(),1,5000);
   QCOMPARE(r.state(),QString("saved"));QVERIFY(!r.active());
   QVERIFY(QFileInfo::exists(r.savedPath()));
   QVERIFY(!QFileInfo::exists(r.savedPath()+".ts"));
   QVERIFY(!QFileInfo::exists(r.savedPath()+".cleaning.mp4"));
   QProcess audio;audio.start("ffmpeg",{"-v","error","-i",r.savedPath(),"-vn","-ac","1","-ar","8000","-f","f32le","pipe:1"});
   QVERIFY(audio.waitForFinished(5000));QCOMPARE(audio.exitCode(),0);
   const QByteArray samples=audio.readAllStandardOutput();
   QVERIFY(samples.size()>=8000*4);
   auto energy=[&](int first,int last) { double sum=0;for(int i=first;i<last;++i){float v;memcpy(&v,samples.constData()+i*4,4);sum+=v*v;}return sum/(last-first); };
   QVERIFY(energy(800,2400)>0.0001);
   QVERIFY(energy(4800,7200)>0.0001);
 }
 void pauseResumesTheSameProcessAndExcludesPausedTime() {
   qputenv("OMAFRAME_TEST_PAUSE_REPLY","fragmented");
   Recorder r;QSignalSpy finished(&r,&Recorder::completed),ack(&r,&Recorder::pauseFinished);
   QVERIFY(!r.setPaused(true));
   r.prepare();QTRY_COMPARE(r.state(),QString("setup"));
   r.selectDisplay(0);r.setCountdown(0);r.start();
   QVERIFY(!r.setPaused(true));
   QTRY_COMPARE_WITH_TIMEOUT(r.state(),QString("recording"),5000);
   qunsetenv("OMAFRAME_TEST_PAUSE_REPLY");
   QTest::qWait(650);
   QVERIFY(r.setPaused(true));QVERIFY(r.pausePending());QVERIFY(!r.setPaused(false));
   QTRY_COMPARE(r.state(),QString("paused"));QVERIFY(!r.pausePending());
   QVERIFY(r.active());QVERIFY(!r.canStart());QVERIFY(finished.isEmpty());
   const QString frozen=r.elapsed(),path=r.savedPath();
   const bool cursor=r.cursor();r.setCursor(!cursor);QCOMPARE(r.cursor(),cursor);
   r.prepare();r.reset();QCOMPARE(r.state(),QString("paused"));
   QTest::qWait(1200);QCOMPARE(r.elapsed(),frozen);
   QVERIFY(r.setPaused(true));QCOMPARE(ack.count(),2); // Idempotent, no backend toggle.
   QVERIFY(r.setPaused(false));QTRY_COMPARE(r.state(),QString("recording"));
   QTRY_VERIFY_WITH_TIMEOUT(r.elapsed()!=frozen,800); // Retains the earlier partial second.
   QVERIFY(r.setPaused(true));QTRY_COMPARE(r.state(),QString("paused"));
   const QString second=r.elapsed();QTest::qWait(1100);QCOMPARE(r.elapsed(),second);
   QVERIFY(r.setPaused(false));QTRY_COMPARE(r.state(),QString("recording"));
   QCOMPARE(r.savedPath(),path);
   QFile controls(path+".controls");QVERIFY(controls.open(QIODevice::ReadOnly));
   const auto lines=controls.readAll().trimmed().split('\n');QCOMPARE(lines.size(),4);
   const auto pid=QJsonDocument::fromJson(lines[0]).object().value("pid");
   QFile cmdline(QString("/proc/%1/cmdline").arg(pid.toInt()));QVERIFY(cmdline.open(QIODevice::ReadOnly));
   const auto args=cmdline.readAll().split('\0');
   QVERIFY(args.contains("-ipc"));
   const QString socket=QString::fromUtf8(args[args.indexOf("-ipc")+1]);
   const QString privateDir=QFileInfo(socket).absolutePath();
   QVERIFY(privateDir!=QFileInfo(path).absolutePath());
   QVERIFY(!(QFileInfo(privateDir).permissions() & (QFile::ReadGroup|QFile::WriteGroup|QFile::ExeGroup|QFile::ReadOther|QFile::WriteOther|QFile::ExeOther)));
   for(int i=0;i<lines.size();++i){
     const auto request=QJsonDocument::fromJson(lines[i]).object();
     QCOMPARE(request.value("pid"),pid);QCOMPARE(request.value("paused").toBool(),i%2==0);
   }
   r.stop();QTRY_COMPARE_WITH_TIMEOUT(finished.count(),1,5000);
   QCOMPARE(r.state(),QString("saved"));QVERIFY(!r.active());
   QVERIFY(!QFileInfo::exists(privateDir));
   QVERIFY(!r.setPaused(false));
   r.prepare();QTRY_COMPARE(r.state(),QString("setup"));
   r.selectDisplay(0);r.setCountdown(0);r.start();
   QTRY_COMPARE_WITH_TIMEOUT(r.state(),QString("recording"),5000);
   QCOMPARE(r.elapsed(),QString("00:00"));QVERIFY(r.savedPath()!=path);
   QVERIFY(r.setPaused(true));QTRY_COMPARE(r.state(),QString("paused"));
   r.cancel();QTRY_COMPARE_WITH_TIMEOUT(finished.count(),2,5000);
 }
 void rejectedResumeLeavesTheRecordingPaused() {
   Recorder r;QSignalSpy finished(&r,&Recorder::completed),ack(&r,&Recorder::pauseFinished);
   r.prepare();QTRY_COMPARE(r.state(),QString("setup"));
   r.selectDisplay(0);r.setCountdown(0);r.start();
   QTRY_COMPARE_WITH_TIMEOUT(r.state(),QString("recording"),5000);
   QVERIFY(r.setPaused(true));QTRY_COMPARE(r.state(),QString("paused"));
   QFile mode(r.savedPath()+".pause-mode");QVERIFY(mode.open(QIODevice::WriteOnly));
   mode.write("reject");mode.close();
   const QString frozen=r.elapsed();
   QVERIFY(r.setPaused(false));QTRY_COMPARE(ack.count(),2);QVERIFY(!ack[1][0].toBool());
   QCOMPARE(r.state(),QString("paused"));QVERIFY(r.active());
   QVERIFY(r.status().contains("Could not resume"));
   QTest::qWait(1100);QCOMPARE(r.elapsed(),frozen);
   QVERIFY(mode.remove());
   QVERIFY(r.setPaused(false));QTRY_COMPARE(r.state(),QString("recording"));
   r.stop();QTRY_COMPARE_WITH_TIMEOUT(finished.count(),1,5000);
 }
 void stoppingDuringPendingPauseStillSaves() {
   qputenv("OMAFRAME_TEST_PAUSE_REPLY","silent");
   Recorder r;QSignalSpy finished(&r,&Recorder::completed),ack(&r,&Recorder::pauseFinished);
   r.prepare();QTRY_COMPARE(r.state(),QString("setup"));
   r.selectDisplay(0);r.setCountdown(0);r.start();
   QTRY_COMPARE_WITH_TIMEOUT(r.state(),QString("recording"),5000);
   qunsetenv("OMAFRAME_TEST_PAUSE_REPLY");
   QVERIFY(r.setPaused(true));r.stop();
   QVERIFY(!r.pausePending());QCOMPARE(ack.count(),1);QVERIFY(!ack[0][0].toBool());
   QTRY_COMPARE_WITH_TIMEOUT(finished.count(),1,5000);QCOMPARE(r.state(),QString("saved"));
 }
 void pauseFailures_data() {
   QTest::addColumn<QByteArray>("mode");QTest::addColumn<bool>("finishes");
   QTest::newRow("rejected")<<QByteArray("reject")<<false;
   QTest::newRow("socket-unavailable")<<QByteArray("missing")<<false;
   QTest::newRow("lost-reply")<<QByteArray("silent")<<true;
   QTest::newRow("invalid-reply")<<QByteArray("malformed")<<true;
   QTest::newRow("disconnected")<<QByteArray("disconnect")<<true;
 }
 void pauseFailures() {
   QFETCH(QByteArray,mode);QFETCH(bool,finishes);
   qputenv("OMAFRAME_TEST_PAUSE_REPLY",mode);
   Recorder r;QSignalSpy finished(&r,&Recorder::completed),ack(&r,&Recorder::pauseFinished);
   r.prepare();QTRY_COMPARE(r.state(),QString("setup"));
   r.selectDisplay(0);r.setCountdown(0);r.start();
   QTRY_COMPARE_WITH_TIMEOUT(r.state(),QString("recording"),5000);
   qunsetenv("OMAFRAME_TEST_PAUSE_REPLY");
   QVERIFY(r.setPaused(true));QTRY_COMPARE_WITH_TIMEOUT(ack.count(),1,4000);
   QVERIFY(!ack[0][0].toBool());QVERIFY(!r.pausePending());
   if(!finishes){QCOMPARE(r.state(),QString("recording"));QVERIFY(r.status().contains("Could not pause"));r.stop();}
   QTRY_COMPARE_WITH_TIMEOUT(finished.count(),1,5000);QCOMPARE(r.state(),QString("saved"));
 }
 void destroyingAnActiveRecorderDoesNotRunCaptureCallbacks_data() {
   QTest::addColumn<bool>("pending");
   QTest::newRow("paused")<<false;
   QTest::newRow("pause-pending")<<true;
 }
 void destroyingAnActiveRecorderDoesNotRunCaptureCallbacks() {
   QFETCH(bool,pending);
   qputenv("OMAFRAME_TEST_PAUSE_REPLY",pending ? "silent" : "ok");
   auto *r=new Recorder;
   r->prepare();QTRY_COMPARE(r->state(),QString("setup"));
   r->selectDisplay(0);r->setCountdown(0);r->start();
   QTRY_COMPARE_WITH_TIMEOUT(r->state(),QString("recording"),5000);
   qunsetenv("OMAFRAME_TEST_PAUSE_REPLY");
   QVERIFY(r->setPaused(true));
   if(!pending)QTRY_COMPARE(r->state(),QString("paused"));
   QSignalSpy finished(r,&Recorder::completed),ack(r,&Recorder::pauseFinished);
   QTest::ignoreMessage(QtWarningMsg,"QProcess: Destroyed while process (\"bash\") is still running.");
   delete r;
   QVERIFY(finished.isEmpty());QVERIFY(ack.isEmpty());
 }
 void layoutChangeStopsPausedRecording() {
   Recorder r;QSignalSpy finished(&r,&Recorder::completed);
   r.prepare();QTRY_COMPARE(r.state(),QString("setup"));
   r.selectDisplay(0);r.setCountdown(0);r.start();
   QTRY_COMPARE_WITH_TIMEOUT(r.state(),QString("recording"),5000);
   QVERIFY(r.setPaused(true));QTRY_COMPARE(r.state(),QString("paused"));
   r.layoutChanged();QTRY_COMPARE_WITH_TIMEOUT(finished.count(),1,5000);
   QCOMPARE(r.state(),QString("saved"));
 }
 void headerOnlyRecordingNeverReportsReady() {
   Recorder r;QSignalSpy finished(&r,&Recorder::completed);
   r.prepare();QTRY_COMPARE(r.state(),QString("setup"));
   r.selectDisplay(0);r.setCountdown(0);
   qputenv("OMAFRAME_TEST_HEADER_ONLY","1");
   r.start();QTRY_VERIFY_WITH_TIMEOUT(!r.savedPath().isEmpty(),5000);
   qunsetenv("OMAFRAME_TEST_HEADER_ONLY");
   QTest::qWait(500);
   QCOMPARE(r.state(),QString("starting"));
   QCOMPARE(QFileInfo(r.savedPath()).size(),qint64(88));
   r.stop();QTRY_COMPARE_WITH_TIMEOUT(r.state(),QString("failed"),5000);
   QVERIFY(finished.isEmpty());
   QVERIFY(r.status().contains("without a readable video"));
   QVERIFY(r.status().contains("No file was kept"));
   QVERIFY(!QFileInfo::exists(r.savedPath()));
 }
 void oldRecorderIsRejectedBeforeCreatingAFile() {
   // 6.1.0 is the oldest release Omarchy's stable channel serves; it is accepted.
   qputenv("OMAFRAME_TEST_RECORDER_VERSION", "6.1.0");
   {
     Recorder ok;ok.prepare();QTRY_COMPARE(ok.state(),QString("setup"));
     ok.selectDisplay(0);ok.setCountdown(0);ok.start();
     QTRY_COMPARE_WITH_TIMEOUT(ok.state(),QString("recording"),5000);
     ok.stop();QTRY_COMPARE_WITH_TIMEOUT(ok.state(),QString("saved"),5000);
   }
   qputenv("OMAFRAME_TEST_RECORDER_VERSION", "6.0.9");
   Recorder r;r.prepare();QTRY_COMPARE(r.state(),QString("setup"));
   r.selectDisplay(0);r.setCountdown(0);r.start();
   QTRY_COMPARE_WITH_TIMEOUT(r.state(),QString("failed"),5000);
   QVERIFY(r.status().contains("6.1.0 or newer"));
   QVERIFY(r.savedPath().isEmpty());
   qunsetenv("OMAFRAME_TEST_RECORDER_VERSION");
 }
 void optionalPopSuppressionMutesOnlyTheStart() {
   Recorder r;QSignalSpy finished(&r,&Recorder::completed);
   r.prepare();QTRY_COMPARE(r.state(),QString("setup"));
   r.selectDisplay(0);r.setCountdown(0);r.setMicAudio(true);
   r.setSuppressStartupPop(true);r.start();
   QTRY_COMPARE_WITH_TIMEOUT(r.state(),QString("recording"),5000);
   r.stop();QTRY_COMPARE_WITH_TIMEOUT(finished.count(),1,5000);
   QProcess audio;audio.start("ffmpeg",{"-v","error","-i",r.savedPath(),"-vn","-ac","1","-ar","8000","-f","f32le","pipe:1"});
   QVERIFY(audio.waitForFinished(5000));QCOMPARE(audio.exitCode(),0);
   const QByteArray samples=audio.readAllStandardOutput();
   QVERIFY(samples.size()>=8000*4);
   auto energy=[&](int first,int last) { double sum=0;for(int i=first;i<last;++i){float v;memcpy(&v,samples.constData()+i*4,4);sum+=v*v;}return sum/(last-first); };
   QVERIFY(energy(800,2400)<0.000001);
   QVERIFY(energy(4800,7200)>0.0001);
 }
 void unsafeActualControlPlacementBlocksRecording() {
   Recorder r;r.prepare();QTRY_COMPARE(r.state(),QString("setup"));
   r.regionSelected("A",QRectF(.1,.1,.5,.5));QVERIFY(r.safeStop());
   QCOMPARE(r.control().bounds,QRect(331,496,232,48));
   r.setCountdown(0);r.start();
   QTRY_COMPARE_WITH_TIMEOUT(r.state(),QString("failed"),5000);
   QVERIFY(r.status().contains("did not appear"));QVERIFY(r.savedPath().isEmpty());
   // A control that ends up over the area is refused too.
   qputenv("OMAFRAME_TEST_LAYERS",R"({"A":{"levels":{"3":[{"namespace":"omaframe-record-control","x":300,"y":300,"w":232,"h":48}]}}})");
   r.regionSelected("A",QRectF(.1,.1,.5,.5));r.start();
   QTRY_COMPARE_WITH_TIMEOUT(r.state(),QString("failed"),5000);
   QVERIFY(r.status().contains("would be in the recording"));QVERIFY(r.savedPath().isEmpty());
   qunsetenv("OMAFRAME_TEST_LAYERS");
 }
 void cancellationDuringCountdownDoesNotLaunch() {
   Recorder r;r.prepare();QTRY_COMPARE(r.state(),QString("setup"));
   QSignalSpy dismissed(&r,&Recorder::dismissRequested);
   r.selectDisplay(0);r.setCountdown(3);r.start();r.stop();
   QCOMPARE(r.state(),QString("idle"));QVERIFY(r.savedPath().isEmpty());QVERIFY(!r.active());
   QCOMPARE(dismissed.count(),1);
   r.setCountdown(0);
 }
 void selectorChoicesMadeBeforeLoadingAreApplied() {
   qputenv("OMAFRAME_TEST_LAYERS",R"({"A":{"levels":{"3":[{"namespace":"omaframe-record-control","x":331,"y":496,"w":232,"h":48}]}}})");
   Recorder r;QSignalSpy setup(&r,&Recorder::setupRequested);
   r.setCountdown(0);r.prepare(false);QCOMPARE(r.state(),QString("loading"));
   r.regionSelected("A",QRectF(.1,.1,.5,.5),true);
   QTRY_COMPARE_WITH_TIMEOUT(r.state(),QString("recording"),5000);
   QCOMPARE(setup.count(),0);
   r.stop();QTRY_COMPARE_WITH_TIMEOUT(r.state(),QString("saved"),5000);
   qunsetenv("OMAFRAME_TEST_LAYERS");
   // Clicking empty desktop in video mode records the display itself.
   r.prepare(false);QTRY_COMPARE(r.state(),QString("setup"));
   r.regionSelected("A",QRectF(0,0,1,1),false);
   QVERIFY(r.targetLabel().startsWith("Entire display"));
   QCOMPARE(setup.count(),1);
 }
 void selectingRegionHidesSetupAndStartsRecording() {
   qputenv("OMAFRAME_TEST_LAYERS",R"({"A":{"levels":{"3":[{"namespace":"omaframe-record-control","x":331,"y":496,"w":232,"h":48}]}}})");
   Recorder r;r.prepare();QTRY_COMPARE(r.state(),QString("setup"));
   r.setCountdown(0);
   QSignalSpy hidden(&r,&Recorder::hideRequested);
   QSignalSpy selecting(&r,&Recorder::selectionRequested);
   QSignalSpy setup(&r,&Recorder::setupRequested);
   r.chooseRegion();
   QCOMPARE(hidden.count(),1);
   QTRY_COMPARE_WITH_TIMEOUT(selecting.count(),1,1000);
   r.regionSelected("A",QRectF(.1,.1,.5,.5),true);
   QCOMPARE(setup.count(),0);
   QTRY_COMPARE_WITH_TIMEOUT(r.state(),QString("recording"),5000);
   r.stop();QTRY_COMPARE_WITH_TIMEOUT(r.state(),QString("saved"),5000);
   qunsetenv("OMAFRAME_TEST_LAYERS");
 }
 void cleanupTestCase() {qputenv("PATH",oldPath);}
};
QTEST_GUILESS_MAIN(RecordingTest)
#include "recording-test.moc"
