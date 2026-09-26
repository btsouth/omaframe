#include "recording.hpp"
#include <QFile>
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
   QCoreApplication::setOrganizationName("Omaframe-test");QCoreApplication::setApplicationName("Recording");
   QSettings().clear();QSettings().setValue("videoDirectory",temp.filePath("videos"));
   oldPath=qgetenv("PATH");qputenv("PATH",temp.path().toUtf8()+":"+oldPath);
   executable("hyprctl",R"(#!/bin/sh
if [ "$2" = monitors ]; then
 echo '[{"name":"A","width":1280,"height":800,"x":0,"y":0,"scale":1}]'
else
 echo '{}'
fi
)");
   executable("pactl",R"(#!/bin/sh
case "$1" in
get-default-source) echo clean_desktop_microphone;;
get-default-sink) echo speakers;;
*) echo '[{"name":"clean_desktop_microphone","description":"Clean microphone"}]';;
esac
)");
   executable("pgrep","#!/bin/sh\nexit 1\n");
   QProcess video;video.start("ffmpeg",{"-hide_banner","-loglevel","error","-f","lavfi","-i","color=c=green:s=64x64:r=10","-f","lavfi","-i","sine=frequency=440:sample_rate=48000","-c:a","aac","-t","1","-c:v","libx264","-threads","1",temp.filePath("fixture.mp4")});
   QVERIFY(video.waitForFinished(10000));QCOMPARE(video.exitCode(),0);
   qputenv("OMAFRAME_TEST_FIXTURE",temp.filePath("fixture.mp4").toUtf8());
   executable("gpu-screen-recorder",R"(#!/usr/bin/python3
import os,sys,signal,time,shutil
path=sys.argv[sys.argv.index('-o')+1]
shutil.copyfile(os.environ['OMAFRAME_TEST_FIXTURE'],path)
signal.signal(signal.SIGINT,lambda *_:sys.exit(0))
while True: time.sleep(.05)
)");
 }
 void placementAlwaysOutsideCapture_data() {
   QTest::addColumn<QRect>("target");QTest::addColumn<bool>("second");QTest::addColumn<bool>("possible");
   QTest::newRow("region")<<QRect(100,100,800,500)<<false<<true;
   QTest::newRow("full-single")<<QRect(0,0,1280,800)<<false<<false;
   QTest::newRow("full-dual")<<QRect(0,0,1280,800)<<true<<true;
   QTest::newRow("near-full")<<QRect(0,0,1280,775)<<false<<false;
 }
 void placementAlwaysOutsideCapture() {
   QFETCH(QRect,target);QFETCH(bool,second);QFETCH(bool,possible);
   QList<Recording::Display> screens{{"A",QRect(0,0,1280,800)}};
   if(second)screens.append({"B",QRect(-1920,0,1920,1080)});
   const auto p=Recording::placeStop(screens,"A",target);
   QCOMPARE(!p.bounds.isEmpty(),possible);
   if(possible){QVERIFY(!p.bounds.intersects(target));bool inside=false;for(auto d:screens)inside|=d.bounds.contains(p.bounds);QVERIFY(inside);}
 }
 void audioFlagsAreExplicitAndCombined() {
   auto a=Recording::arguments("800x600+-1200+20","/tmp/name with spaces.mp4","speakers.monitor","clean_desktop_microphone",false);
   QCOMPARE(a[a.indexOf("-a")+1],QString("speakers.monitor|clean_desktop_microphone"));
   QCOMPARE(a[a.indexOf("-cursor")+1],QString("no"));
   QCOMPARE(a[a.indexOf("-o")+1],QString("/tmp/name with spaces.mp4"));
   QVERIFY(!Recording::arguments("A","a.mp4",{}, {},true).contains("-a"));
 }
 void ownProcessStopsThenHandsOffValidClip() {
   Recorder r;QSignalSpy finished(&r,&Recorder::completed);
   r.prepare();QTRY_COMPARE(r.state(),QString("setup"));
   QCOMPARE(r.microphone(),0);r.selectDisplay(0);QVERIFY(!r.safeStop());
   r.setCountdown(0);r.setMicAudio(true);r.setDesktopAudio(true);r.start();
   QTRY_COMPARE_WITH_TIMEOUT(r.state(),QString("recording"),5000);
   QVERIFY(r.active());QVERIFY(finished.isEmpty());
   r.stop();QTRY_COMPARE_WITH_TIMEOUT(finished.count(),1,5000);
   QCOMPARE(r.state(),QString("saved"));QVERIFY(!r.active());
   QVERIFY(QFileInfo::exists(r.savedPath()));
   QVERIFY(!QFileInfo::exists(r.savedPath()+".cleaning.mp4"));
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
   r.setCountdown(0);r.start();
   QTRY_COMPARE_WITH_TIMEOUT(r.state(),QString("failed"),5000);
   QVERIFY(r.status().contains("did not appear safely"));QVERIFY(r.savedPath().isEmpty());
 }
 void cancellationDuringCountdownDoesNotLaunch() {
   Recorder r;r.prepare();QTRY_COMPARE(r.state(),QString("setup"));
   r.selectDisplay(0);r.setCountdown(3);r.start();r.stop();
   QCOMPARE(r.state(),QString("setup"));QVERIFY(r.savedPath().isEmpty());QVERIFY(!r.active());
 }
 void cleanupTestCase() {qputenv("PATH",oldPath);}
};
QTEST_GUILESS_MAIN(RecordingTest)
#include "recording-test.moc"
