#include "webcam.hpp"
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class WebcamTest : public QObject {
  Q_OBJECT
  static void wait(int ms) {
    // Keep pipe write notifications flowing as in the real application's
    // event loop. qWait's polling sleeps throttle raw-video pipe throughput.
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
  }
private slots:
  void recordedClockOmitsPausedTimeAndPreservesFrameShape() {
    QTemporaryDir dir;
    CameraTrack track;
    QSignalSpy finished(&track, &CameraTrack::finished);
    const QString path = dir.filePath("camera.mp4");
    track.begin(path);
    QTRY_VERIFY(track.busy());
    wait(50);
    QImage image(120, 120, QImage::Format_RGBA8888);
    image.fill(Qt::red);
    // A paused screen clock stays at the same time even as wall time passes.
    for (int time = 0; time <= 400; time += 42) {
      track.append(image, time);
      wait(42);
    }
    const auto pausedDuration = track.duration();
    wait(150);
    track.append(image, 378);
    QCOMPARE(track.duration(), pausedDuration);
    for (int time = 420; time <= 840; time += 42) {
      track.append(image, time);
      wait(42);
    }
    track.end();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 6000);
    QVERIFY2(finished.first()[0].toBool(),
             qPrintable(finished.first()[1].toString()));
    QVERIFY2(finished.first()[1].toString().isEmpty(),
             qPrintable(finished.first()[1].toString()));
    QProcess probe;
    probe.start("ffprobe", {"-v", "error", "-show_entries", "format=duration",
                            "-of", "json", path});
    QVERIFY(probe.waitForFinished(5000));
    const double duration =
        QJsonDocument::fromJson(probe.readAllStandardOutput())
            .object()
            .value("format")
            .toObject()
            .value("duration")
            .toString()
            .toDouble();
    QVERIFY(qAbs(duration - track.duration()) < 0.05);
    QVERIFY(duration < 0.95 && duration > 0.75);
    QProcess frame;
    frame.start("ffmpeg", {"-v", "error", "-i", path, "-frames:v", "1", "-f",
                           "rawvideo", "-pix_fmt", "rgb24", "-"});
    QVERIFY(frame.waitForFinished(5000));
    const QByteArray pixels = frame.readAllStandardOutput();
    QCOMPARE(pixels.size(), 640 * 360 * 3);
    const QImage decoded(reinterpret_cast<const uchar *>(pixels.constData()),
                         640, 360, 640 * 3, QImage::Format_RGB888);
    QVERIFY(decoded.pixelColor(320, 180).red() > 200);
    QVERIFY(decoded.pixelColor(20, 180).red() <
            20); // square camera stays square
  }
  void encoderFailureIsBoundedAndCameraStartsOff() {
    Webcam camera;
    QVERIFY(!camera.enabled());
    CameraTrack track;
    QSignalSpy finished(&track, &CameraTrack::finished);
    track.begin("/not-a-real-camera-output-folder/camera.mp4");
    QTRY_VERIFY(track.busy());
    track.end();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 6000);
    QVERIFY(!finished.first()[0].toBool());
    QVERIFY(!track.busy());
  }
};
QTEST_GUILESS_MAIN(WebcamTest)
#include "webcam-test.moc"
