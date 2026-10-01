/** @fileoverview Scrolling capture: scrolls a window or area and stitches what
 *  it shows into one tall image.
 *
 *  The loop runs on its own thread. It scrolls the page with a virtual
 *  pointer, waits for each step to settle, and keeps a step only once the
 *  stitcher has verified how far the content really moved. When the page
 *  cannot be scrolled automatically, or the user takes the mouse, it carries
 *  on with what the user scrolls by hand. Capture only moves down the page. */
#pragma once
#include <QImage>
#include <QObject>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QString>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

class QThread;

namespace Scrolling {
/** What the capture loop needs from the desktop. Every function is called on
 *  the capture thread. */
struct Desktop {
  /** Starts capturing the display. False with `error` when it cannot. */
  std::function<bool(QString &error)> open;
  /** The whole display, in physical pixels. False when nothing changed
   *  within `timeoutMs`, or when capture failed (`error` set). */
  std::function<bool(QImage &frame, QString &error, int timeoutMs)> grab;
  /** Whether the display stopped for good (unplugged, mode change). */
  std::function<bool()> stopped;
  /** Starts a pointer that can scroll. False when only manual capture can
   *  work. */
  std::function<bool()> openPointer;
  /** Moves that pointer to a fraction of the display. */
  std::function<bool(QPointF)> park;
  /** Scrolls by wheel notches; positive moves the page down. */
  std::function<bool(int)> scroll;
  /** Whether the real pointer has left `parked` (a fraction of the display):
   *  the user has moved the mouse. */
  std::function<bool(QPointF parked)> pointerMoved;
  /** Gives the pointer back. */
  std::function<void()> closePointer;
};

/** How a capture is going, for the control and for tests. */
struct Progress {
  enum class Mode { Starting, Auto, Manual, Finishing };
  Mode mode = Mode::Starting;
  /** Height of the image so far, in pixels. */
  int length = 0;
  /** One short line for the control. */
  QString hint;
  bool warning = false;
};

/** Timing, exposed so tests run without waiting for real animations. */
struct Timing {
  /** Wait before the first picture, so the selector has gone. */
  int firstFrameMs = 150;
  /** How long a scroll may take to start moving before the page counts as
   *  still. */
  int motionWaitMs = 700;
  /** A grab with no change for this long means the page has settled. */
  int settleGrabMs = 120;
  /** Content that never holds still is taken after this long. */
  int maxSettleMs = 2000;
  /** Pause between pictures while the user scrolls by hand. */
  int manualIntervalMs = 40;
  int manualGrabMs = 250;
};

/** What to capture and where the pointer goes, as fractions of the display. */
struct Plan {
  /** The window or area to capture. */
  QRectF area;
  /** Where the wheel scrolls the page. */
  QPointF anchor;
  /** Where the pointer goes back to afterwards, unless the user has moved
   *  it: where they clicked. */
  QPointF home;
  /** How far down from the top of the area, as a fraction of the display's
   *  height, the progress control covers. Zero when it sits outside the
   *  area. Those rows come only from the first picture, taken before the
   *  control appears. */
  double coverTop = 0;
};

/** The capture loop. Construct, then run() on a thread of its own. */
class Session {
public:
  using Report = std::function<void(const Progress &)>;
  /** `ready` is called once the first picture is taken, when the control
   *  may appear. */
  Session(Desktop desktop, Plan plan, Report report,
          std::function<void()> ready = {}, Timing timing = {});
  /** Runs until the page ends, the user finishes or cancels, or capture
   *  fails. Returns the stitched image, or a null image with `error` set.
   *  A cancelled capture returns a null image and no error. */
  QImage run(QString &error);
  /** Keeps what has been captured and stops. Thread safe. */
  void finish() { m_finish.store(true); }
  /** Throws the capture away. Thread safe. */
  void cancel() { m_cancel.store(true); }
  /** Whether the result stopped at the size limit. */
  bool reachedLimit() const { return m_reachedLimit; }
  /** Whether automatic scrolling found the end of the page. */
  bool reachedEnd() const { return m_reachedEnd; }

private:
  bool stopRequested() const { return m_finish.load() || m_cancel.load(); }
  /** The stitched part of the area: below anything the control covers. */
  QImage crop(const QImage &frame) const;
  bool firstFrame(QImage &area, QString &error);
  bool settledFrame(const QImage &before, QImage &settled, QString &error);
  void report(Progress::Mode mode, int length, const QString &hint,
              bool warning = false);
  QImage capture(QString &error);
  Desktop m_desktop;
  Plan m_plan;
  Report m_report;
  std::function<void()> m_ready;
  Timing m_timing;
  /** The whole area and the stitched part of it, in pixels of a frame. */
  QRect m_pixels, m_stitched;
  std::atomic_bool m_finish{false}, m_cancel{false};
  bool m_reachedLimit = false, m_reachedEnd = false;
};

/** The pixels of `area` (a fraction of a display) in a frame of `size`. */
QRect areaPixels(QRectF area, QSize size);
/** Where to scroll a capture of `area`, a fraction of a display that is
 *  `logical` pixels in size. A window scrolls near its right edge, below
 *  its toolbar, where the wheel reaches the page itself rather than a panel
 *  inside it; a drawn area scrolls at its middle. Never inside the top
 *  `coverTop`, where the control sits. */
QPointF anchorFor(QRectF area, bool window, double coverTop, QSizeF logical);
/** Finds a scroll bar along the right edge of a stitched capture: columns
 *  that do not move with the page while everything else does. */
class EdgeStrip {
public:
  /** `after` is `before` scrolled down by `delta` pixels. */
  void observe(const QImage &before, const QImage &after, int delta);
  /** Columns to drop from the right edge; 0 when there is no scroll bar. */
  int width() const;

private:
  std::vector<int> m_votes, m_trackVotes;
  int m_steps = 0;
};
} // namespace Scrolling

/** A scrolling capture on the real desktop, for QML and the app. */
class ScrollCapture final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool active READ active NOTIFY changed)
  /** "starting", "auto", "manual" or "finishing". */
  Q_PROPERTY(QString mode READ mode NOTIFY changed)
  Q_PROPERTY(int length READ length NOTIFY changed)
  Q_PROPERTY(QString hint READ hint NOTIFY changed)
  Q_PROPERTY(bool warning READ warning NOTIFY changed)
public:
  explicit ScrollCapture(QObject *parent = nullptr);
  ~ScrollCapture() override;
  bool active() const { return m_thread != nullptr; }
  QString mode() const { return m_mode; }
  int length() const { return m_length; }
  QString hint() const { return m_hint; }
  bool warning() const { return m_warning; }
  /** Starts capturing on `monitor`, whose place in the desktop is `screen`,
   *  in logical pixels. */
  void start(const QString &monitor, QRect screen, Scrolling::Plan plan);
  /** Keeps what has been captured and stops. */
  Q_INVOKABLE void finish();
  /** Throws the capture away. */
  Q_INVOKABLE void cancel();
signals:
  void changed();
  /** The first picture is taken; the control may appear. */
  void ready();
  /** The capture is done; `image` is the stitched result. */
  void finished(const QImage &image, bool reachedLimit, bool reachedEnd);
  void failed(const QString &error);
  void cancelled();

private:
  void join();
  std::shared_ptr<Scrolling::Session> m_session;
  QThread *m_thread = nullptr;
  int m_generation = 0;
  QString m_mode = "starting", m_hint;
  int m_length = 0;
  bool m_warning = false;
};
