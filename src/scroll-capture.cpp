#include "scroll-capture.hpp"
#include "auto-capture.hpp"
#include "capture.hpp"
#include "scroll-pointer.hpp"
#include "stitch.hpp"
#include <QDebug>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>

namespace Scrolling {
namespace {
/// Fraction of the area each automatic step should travel. The stitcher
/// aligns on the overlap, and a little under half leaves plenty.
constexpr double kStepFraction = 0.45;
constexpr int kMaxNotches = 12;

bool debugging() {
  static const bool on = qEnvironmentVariableIsSet("OMAFRAME_SCROLL_DEBUG");
  return on;
}

int luma(QRgb p) { return (qRed(p) * 77 + qGreen(p) * 150 + qBlue(p) * 29) >> 8; }

/// Columns at the right edge that are left out when deciding whether the
/// page is still moving. A scroll bar that fades in and out after every
/// step is not the page moving, and waiting for it would slow each step.
int edgeAllowance(int width) { return std::clamp(width / 24, 0, 48); }

/// Whether two crops show the page at rest: at most a few scattered pixels
/// differ. A blinking caret or a spinner in a toolbar changes a handful of
/// pixels; a page that moved by even one line changes many.
bool looksStill(const QImage &a, const QImage &b) {
  if (a.size() != b.size())
    return false;
  const int width = a.width() - edgeAllowance(a.width());
  int samples = 0, changed = 0;
  for (int y = 0; y < a.height(); y += 2) {
    const auto *ra = reinterpret_cast<const quint32 *>(a.constScanLine(y));
    const auto *rb = reinterpret_cast<const quint32 *>(b.constScanLine(y));
    for (int x = 0; x < width; x += 2) {
      ++samples;
      if (ra[x] == rb[x])
        continue;
      const auto pa = reinterpret_cast<const uchar *>(ra + x),
                 pb = reinterpret_cast<const uchar *>(rb + x);
      if (std::abs(pa[0] - pb[0]) + std::abs(pa[1] - pb[1]) +
              std::abs(pa[2] - pb[2]) > 48)
        ++changed;
    }
  }
  return changed <= std::max(4, samples / 1000);
}
} // namespace

void EdgeStrip::observe(const QImage &before, const QImage &after, int delta) {
  const int w = before.width(), h = before.height();
  if (delta <= 0 || delta >= h - 16 || after.size() != before.size() ||
      w < 160)
    return;
  const int window = std::min(48, w / 8);
  // How many rows of column `x` do not line up once the page has moved by
  // `delta`.
  auto mismatches = [&](int x) {
    int count = 0;
    for (int y = 0; y + delta < h; ++y)
      if (std::abs(luma(before.pixel(x, y + delta)) - luma(after.pixel(x, y))) > 24)
        ++count;
    return count;
  };
  // Content columns say how much of the frame does not line up anyway: a
  // sticky header does that for every column, a scroll bar only for its own.
  std::vector<int> content;
  for (int i = 0; i < 9; ++i)
    content.push_back(mismatches(w * (20 + i * 7) / 100));
  std::nth_element(content.begin(), content.begin() + 4, content.end());
  const int baseline = content[4];
  const int noise = std::max(4, (h - delta) / 60);
  if (m_votes.size() != static_cast<size_t>(window)) {
    m_votes.assign(window, 0);
    m_trackVotes.assign(window, 0);
  }
  for (int i = 0; i < window; ++i) {
    const int x = w - window + i;
    if (mismatches(x) > baseline + noise)
      ++m_votes[i];
    // The thumb changes, but its track borders match the outer track
    // column. Keep those borders with the thumb rather than leaving a
    // repeated sliver of the scrollbar behind.
    int different = 0;
    for (int y = 0; y < h; ++y)
      if (std::abs(luma(after.pixel(x, y)) - luma(after.pixel(w - 1, y))) > 8)
        ++different;
    if (different <= std::max(3, h / 100))
      ++m_trackVotes[i];
  }
  ++m_steps;
}

int EdgeStrip::width() const {
  // A scroll bar shows up step after step, so one odd step proves nothing.
  if (m_steps < 2 || m_votes.empty())
    return 0;
  const int window = static_cast<int>(m_votes.size());
  // Walk in from the right edge. A pixel or two of window border may not
  // vote; the bar itself must, column after column.
  int i = window - 1, quiet = 0;
  while (i >= 0 && m_votes[i] * 2 <= m_steps && quiet < 3) {
    --i;
    ++quiet;
  }
  int bar = 0;
  while (i >= 0 && m_votes[i] * 2 > m_steps) {
    --i;
    ++bar;
  }
  // Scroll bars are 3 to 24 pixels wide; anything wider is page content
  // moving on its own.
  if (bar < 3 || bar > 32)
    return 0;
  int track = 0;
  while (i >= 0 && m_trackVotes[i] * 2 > m_steps && track < 4) {
    --i;
    ++track;
  }
  return std::min(window, bar + quiet + track);
}

QRect areaPixels(QRectF area, QSize size) {
  // Round edges, not extents, so the crop never drifts a pixel against the
  // area at fractional scales.
  const int left = std::clamp(qRound(area.left() * size.width()), 0, size.width());
  const int top = std::clamp(qRound(area.top() * size.height()), 0, size.height());
  const int right = std::clamp(qRound(area.right() * size.width()), 0, size.width());
  const int bottom =
      std::clamp(qRound(area.bottom() * size.height()), 0, size.height());
  return QRect(left, top, std::max(0, right - left), std::max(0, bottom - top));
}

QPointF anchorFor(QRectF area, bool window, double coverTop, QSizeF logical) {
  area = area.normalized();
  const double w = std::max(1.0, logical.width()),
               h = std::max(1.0, logical.height());
  const double top = area.top() + coverTop;
  const double height = std::max(0.0, area.bottom() - top);
  if (!window)
    return {area.center().x(), top + height / 2};
  // Clear of a scroll bar, but close to the edge: page margins are there,
  // and panels that scroll on their own (sidebars, embedded maps) seldom are.
  const double inset = std::clamp(area.width() * w * 0.04, 28.0, 72.0) / w;
  const double x = std::max(area.left() + area.width() / 2, area.right() - inset);
  // Below a toolbar and tabs, which do not scroll the page.
  const double y = top + std::min(height * 0.6, std::max(height * 0.3, 160.0 / h));
  return {x, y};
}

Session::Session(Desktop desktop, Plan plan, Report report,
                 std::function<void()> ready, Timing timing)
    : m_desktop(std::move(desktop)), m_plan(plan), m_report(std::move(report)),
      m_ready(std::move(ready)), m_timing(timing) {
  m_plan.area = m_plan.area.normalized();
}

void Session::report(Progress::Mode mode, int length, const QString &hint,
                     bool warning) {
  if (m_report)
    m_report({mode, length, hint, warning});
}

QImage Session::crop(const QImage &frame) const {
  return frame.copy(m_stitched).convertToFormat(QImage::Format_RGBA8888);
}

bool Session::firstFrame(QImage &area, QString &error) {
  // The selector has just closed. Let it leave the screen, then take the
  // first picture once the screen holds still.
  QThread::msleep(m_timing.firstFrameMs);
  QElapsedTimer clock;
  clock.start();
  QImage last;
  while (!stopRequested()) {
    QImage frame;
    QString grabError;
    if (!m_desktop.grab(frame, grabError, m_timing.settleGrabMs)) {
      if (m_desktop.stopped()) {
        error = "The display turned off before capture began.";
        return false;
      }
      if (!last.isNull()) {
        area = last;
        return true;
      }
      if (clock.elapsed() > 4000) {
        error = grabError.isEmpty() ? "The display sent no picture." : grabError;
        return false;
      }
      continue;
    }
    if (m_pixels.isEmpty()) {
      m_pixels = areaPixels(m_plan.area, frame.size());
      const int cover = std::clamp(
          qRound(m_plan.coverTop * frame.height()), 0, m_pixels.height());
      m_stitched = m_pixels.adjusted(0, cover, 0, 0);
      if (m_stitched.width() < 64 || m_stitched.height() < 96) {
        error = "Choose a larger area to capture.";
        return false;
      }
    }
    const QImage current =
        frame.copy(m_pixels).convertToFormat(QImage::Format_RGBA8888);
    if (!last.isNull() && looksStill(current, last)) {
      area = current;
      return true;
    }
    last = current;
    if (clock.elapsed() >= m_timing.maxSettleMs) {
      area = current;
      return true;
    }
  }
  return false;
}

bool Session::settledFrame(const QImage &before, QImage &settled,
                           QString &error) {
  // Pages animate a scroll over anything from one frame to half a second, so
  // a step is read once it has stopped moving rather than after a fixed
  // wait. No movement at all within motionWaitMs means the page did not move.
  QElapsedTimer clock;
  clock.start();
  bool moved = false;
  QImage last;
  int failures = 0;
  while (!stopRequested()) {
    QImage frame;
    const bool ok = m_desktop.grab(frame, error, moved ? m_timing.settleGrabMs
                                                      : m_timing.settleGrabMs / 2);
    if (stopRequested())
      return false;
    if (!ok) {
      if (m_desktop.stopped())
        return false;
      if (moved && !last.isNull()) {
        settled = last; // nothing changed since the last picture
        return true;
      }
      if (!moved && clock.elapsed() >= m_timing.motionWaitMs) {
        settled = before;
        return true;
      }
      if (!error.isEmpty() && ++failures >= 20)
        return false;
      continue;
    }
    failures = 0;
    error.clear();
    const QImage current = crop(frame);
    if (!moved) {
      if (!looksStill(current, before))
        moved = true;
      else if (clock.elapsed() >= m_timing.motionWaitMs) {
        settled = current;
        return true;
      } else
        continue;
    }
    if (!last.isNull() && looksStill(current, last)) {
      settled = current;
      return true;
    }
    last = current;
    if (clock.elapsed() >= m_timing.maxSettleMs) {
      settled = current; // never holds still: take what is there
      return true;
    }
  }
  return false;
}

QImage Session::run(QString &error) {
  error.clear();
  QImage image = capture(error);
  if (m_desktop.closePointer)
    m_desktop.closePointer();
  if (m_cancel.load()) {
    error.clear();
    return {};
  }
  return image;
}

QImage Session::capture(QString &error) {
  using AutoEvent = stitch::AutoCapture::Event;
  using Halt = stitch::AutoCapture::HaltReason;
  using ManualEvent = stitch::ManualCapture::Event;
  constexpr auto axis = stitch::Axis::Vertical;
  report(Progress::Mode::Starting, 0, "Starting…");
  if (!m_desktop.open(error))
    return {};
  QImage area;
  if (!firstFrame(area, error))
    return {};
  const int cover = m_stitched.top() - m_pixels.top();
  const QImage first = area.copy(0, cover, area.width(), area.height() - cover);
  if (m_ready)
    m_ready();

  EdgeStrip strip;
  QImage committed = first, held;
  std::optional<stitch::AutoCapture> automatic;
  std::optional<stitch::ManualCapture> manual;
  bool limit = false, ended = false, userHasPointer = false;
  QString handover;

  const bool canScroll = m_desktop.openPointer && m_desktop.openPointer() &&
                         m_desktop.park(m_plan.anchor);
  auto length = [&] {
    const int body = automatic ? automatic->extent() : manual ? manual->extent() : 0;
    return body + cover;
  };
  auto toManual = [&](const QString &why) {
    if (automatic) {
      stitch::GrayView last;
      int kept = 0;
      auto verified = automatic->takeVerified(last, kept);
      automatic.reset();
      if (verified)
        manual.emplace(axis, std::move(*verified), std::move(last), kept);
    }
    if (!manual) {
      manual.emplace(axis);
      manual->setForwardOnly();
      (void)manual->feed(first);
    }
    handover = why;
    if (debugging())
      qInfo().noquote() << "scroll: by hand:" << why;
    report(Progress::Mode::Manual, length(), why);
  };

  if (canScroll) {
    automatic.emplace(axis);
    if (automatic->feed(first).event != AutoEvent::Seeded) {
      error = "This area shows only one flat color.";
      return {};
    }
    report(Progress::Mode::Auto, length(), "Scrolling…");
  } else {
    toManual("Scroll the page to capture it.");
    if (!manual->started()) {
      error = "This area shows only one flat color.";
      return {};
    }
  }

  // Automatic: scroll, wait for the page to settle, keep only verified steps.
  int notches = 1, direction = 1;
  bool fitted = false, triedOtherWay = false, probe = false;
  while (automatic && !stopRequested()) {
    if (m_desktop.pointerMoved && m_desktop.pointerMoved(m_plan.anchor)) {
      userHasPointer = true;
      toManual("Scroll by hand to add more, then Done.");
      break;
    }
    const int step = probe ? stitch::kProbeNotches : notches;
    if (!m_desktop.scroll(step * direction)) {
      toManual("Scroll the page to capture it.");
      break;
    }
    QImage frame;
    QString grabError;
    // A probe starts from the held frame, not the last committed one. At
    // the page end the compositor sends no damage, so settledFrame returns
    // this reference unchanged to confirm the stationary probe.
    if (!settledFrame(probe ? held : committed, frame, grabError)) {
      if (stopRequested())
        break;
      if (!grabError.isEmpty() && !m_desktop.stopped()) {
        error = grabError;
        return {};
      }
      ended = true; // the display went away; keep what there is
      break;
    }
    const bool atStart = automatic->keptFrames() <= 1;
    const auto out = automatic->feed(frame);
    if (debugging())
      qInfo().noquote() << QString("scroll: event=%1 delta=%2/%3/%4 halt=%5 "
                                   "length=%6 notches=%7")
                               .arg(int(out.event))
                               .arg(out.estimate.motion.delta)
                               .arg(out.firstDelta)
                               .arg(out.secondDelta)
                               .arg(int(out.haltReason))
                               .arg(automatic->extent())
                               .arg(step * direction);
    // One notch moves a different distance in every app. Once a step is
    // verified, size the next ones to move a little under half the area.
    double perNotch = 0;
    if (out.event == AutoEvent::Appended && out.estimate.motion.delta > 0)
      perNotch = double(out.estimate.motion.delta) / step;
    else if (out.event == AutoEvent::Committed && out.secondDelta > 0)
      perNotch = double(out.secondDelta) / stitch::kProbeNotches;
    if (!fitted && perNotch > 0) {
      notches = std::clamp(int(std::lround(m_stitched.height() * kStepFraction /
                                           std::max(perNotch, 1.0))),
                           1, kMaxNotches);
      automatic->setCadence(double(notches) / stitch::kProbeNotches);
      fitted = true;
    }
    switch (out.event) {
    case AutoEvent::Appended:
      strip.observe(committed, frame, out.estimate.motion.delta);
      committed = frame;
      break;
    case AutoEvent::ProbeStarted:
      held = frame;
      break;
    case AutoEvent::Committed:
      strip.observe(committed, held, out.firstDelta);
      strip.observe(held, frame, out.secondDelta);
      committed = frame;
      held = {};
      break;
    case AutoEvent::ReachedEndAtSeam:
      ended = true;
      break;
    case AutoEvent::ReachedEnd:
      if (atStart) {
        // Nothing has moved since the start: the wheel runs the other way
        // here, the page is already at its end, or the app ignores the
        // wheel. Try the other way once, then leave it to the user.
        if (!triedOtherWay) {
          triedOtherWay = true;
          direction = -direction;
          automatic->resumeFromEnd();
          break;
        }
        toManual("This page does not scroll by itself. Scroll it by hand.");
        break;
      }
      ended = true;
      break;
    case AutoEvent::Halted:
      if (out.haltReason == Halt::ReachedLimit) {
        limit = ended = true;
        break;
      }
      if (out.haltReason == Halt::MovedBackward && atStart && !triedOtherWay) {
        // The wheel runs the other way here. Scroll back to where the
        // capture began, then carry on the other way from the same frame.
        triedOtherWay = true;
        direction = -direction;
        QImage back;
        QString backError;
        if (!m_desktop.scroll(notches * direction) ||
            !settledFrame(frame, back, backError)) {
          toManual("Scroll the page to capture it.");
          break;
        }
        automatic.emplace(axis);
        (void)automatic->feed(first);
        committed = first;
        break;
      }
      if (out.haltReason == Halt::AppendFailed) {
        error = out.error.isEmpty() ? "Could not add to the capture." : out.error;
        return {};
      }
      toManual("Lost track of the page. Scroll back a little, then on.");
      break;
    case AutoEvent::Paused:
      toManual("Lost track of the page. Scroll back a little, then on.");
      break;
    default:
      break;
    }
    if (ended || !automatic)
      break;
    // An ambiguous step is verified with a one-notch probe: a short, exact
    // move that only one of the candidate offsets can explain.
    probe = out.ack == stitch::AutoCapture::Ack::Probe;
    report(Progress::Mode::Auto, length(), "Scrolling…");
  }
  m_reachedEnd = ended && automatic.has_value();
  // Put the pointer back where the user left it, unless they have moved it.
  if (canScroll && !userHasPointer && m_desktop.pointerMoved &&
      !m_desktop.pointerMoved(m_plan.anchor))
    m_desktop.park(m_plan.home);
  if (canScroll && m_desktop.closePointer)
    m_desktop.closePointer();

  // By hand: read the area as the user scrolls.
  while (manual && !ended && !stopRequested()) {
    QImage frame;
    QString grabError;
    if (!m_desktop.grab(frame, grabError, m_timing.manualGrabMs)) {
      if (m_desktop.stopped())
        break;
      continue;
    }
    const QImage current = crop(frame);
    const auto out = manual->feed(current);
    if (debugging())
      qInfo() << "[DEBUG-scroll] manual" << int(out.event)
              << int(out.estimate.motion.kind) << out.estimate.motion.delta
              << out.estimate.error << out.estimate.confidence << out.pendingDelta;
    switch (out.event) {
    case ManualEvent::Kept:
      strip.observe(committed, current, out.estimate.motion.delta);
      committed = current;
      handover.clear();
      report(Progress::Mode::Manual, length(), "Keep scrolling, then Done.");
      break;
    case ManualEvent::Pending:
      if (handover.isEmpty())
        report(Progress::Mode::Manual, length(), "Keep scrolling, then Done.");
      break;
    case ManualEvent::WrongDirection:
      report(Progress::Mode::Manual, length(), "Scroll down to add more.", true);
      break;
    case ManualEvent::Unmatchable:
      report(Progress::Mode::Manual, length(),
             "Lost track of the page. Scroll back a little.", true);
      break;
    case ManualEvent::Ambiguous:
      report(Progress::Mode::Manual, length(),
             "This part repeats. Scroll a little further.", true);
      break;
    case ManualEvent::Full:
      limit = ended = true;
      break;
    case ManualEvent::Error:
      error = out.error;
      return {};
    default:
      break;
    }
    QThread::msleep(m_timing.manualIntervalMs);
  }
  if (m_cancel.load())
    return {};
  report(Progress::Mode::Finishing, length(), "Finishing…");
  QImage body = automatic ? automatic->finish(error) : manual->finish(error);
  if (body.isNull())
    return {};
  QImage image(body.width(), body.height() + cover, QImage::Format_RGBA8888);
  if (image.isNull()) {
    error = "Not enough memory for this capture.";
    return {};
  }
  for (int y = 0; y < cover; ++y)
    std::memcpy(image.scanLine(y), area.constScanLine(y), image.bytesPerLine());
  for (int y = 0; y < body.height(); ++y)
    std::memcpy(image.scanLine(cover + y), body.constScanLine(y),
                image.bytesPerLine());
  // A scroll bar repeats its thumb at every seam; leave it out of the page.
  if (const int bar = strip.width(); bar > 0 && bar < image.width() / 4)
    image = image.copy(0, 0, image.width() - bar, image.height());
  m_reachedLimit = limit;
  return image;
}
} // namespace Scrolling

namespace {
/// The real desktop: native output capture, a virtual pointer, and
/// Hyprland's idea of where the pointer is.
Scrolling::Desktop realDesktop(const QString &monitor, QRect screen) {
  auto output = std::make_shared<OutputCapture>();
  auto pointer = std::make_shared<ScrollPointer>();
  Scrolling::Desktop desktop;
  desktop.open = [output, monitor](QString &error) {
    return output->open(monitor, error);
  };
  desktop.grab = [output](QImage &frame, QString &error, int timeoutMs) {
    error.clear();
    const bool ok = output->grab(frame, error, timeoutMs);
    // A timeout only means the screen did not change.
    if (!ok && error.contains("timed out"))
      error.clear();
    return ok;
  };
  desktop.stopped = [output] { return output->sessionStopped(); };
  desktop.openPointer = [pointer, monitor] {
    QString error;
    if (!pointer->open(monitor, error)) {
      qInfo().noquote() << "scroll: no automatic scrolling:" << error;
      return false;
    }
    return true;
  };
  desktop.park = [pointer](QPointF at) { return pointer->park(at); };
  desktop.scroll = [pointer](int notches) { return pointer->scroll(notches); };
  desktop.pointerMoved = [screen](QPointF parked) {
    QProcess process;
    process.start("hyprctl", {"-j", "cursorpos"});
    if (!process.waitForFinished(800)) {
      process.kill();
      process.waitForFinished();
      return false;
    }
    const auto at = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
    if (!at.contains("x"))
      return false;
    const QPointF expected(screen.x() + parked.x() * screen.width(),
                           screen.y() + parked.y() * screen.height());
    return std::abs(at.value("x").toDouble() - expected.x()) > 4 ||
           std::abs(at.value("y").toDouble() - expected.y()) > 4;
  };
  desktop.closePointer = [pointer] { pointer->close(); };
  return desktop;
}

QString modeName(Scrolling::Progress::Mode mode) {
  switch (mode) {
  case Scrolling::Progress::Mode::Starting:
    return "starting";
  case Scrolling::Progress::Mode::Auto:
    return "auto";
  case Scrolling::Progress::Mode::Manual:
    return "manual";
  case Scrolling::Progress::Mode::Finishing:
    return "finishing";
  }
  return "starting";
}
} // namespace

ScrollCapture::ScrollCapture(QObject *parent) : QObject(parent) {}

ScrollCapture::~ScrollCapture() {
  if (m_session)
    m_session->cancel();
  join();
}

void ScrollCapture::join() {
  if (!m_thread)
    return;
  m_thread->wait();
  delete m_thread;
  m_thread = nullptr;
}

void ScrollCapture::start(const QString &monitor, QRect screen,
                          Scrolling::Plan plan) {
  if (m_thread)
    return;
  const int generation = ++m_generation;
  m_mode = "starting";
  m_hint = "Starting…";
  m_length = 0;
  m_warning = false;
  QPointer<ScrollCapture> self(this);
  auto report = [self, generation](const Scrolling::Progress &progress) {
    QMetaObject::invokeMethod(
        self.data(),
        [self, generation, progress] {
          if (!self || self->m_generation != generation)
            return;
          self->m_mode = modeName(progress.mode);
          self->m_length = progress.length;
          self->m_hint = progress.hint;
          self->m_warning = progress.warning;
          emit self->changed();
        },
        Qt::QueuedConnection);
  };
  auto ready = [self, generation] {
    QMetaObject::invokeMethod(
        self.data(),
        [self, generation] {
          if (self && self->m_generation == generation)
            emit self->ready();
        },
        Qt::QueuedConnection);
  };
  m_session = std::make_shared<Scrolling::Session>(realDesktop(monitor, screen),
                                                   plan, report, ready);
  auto session = m_session;
  m_thread = QThread::create([self, session, generation] {
    QString error;
    const QImage image = session->run(error);
    const bool limit = session->reachedLimit(), end = session->reachedEnd();
    QMetaObject::invokeMethod(
        self.data(),
        [self, generation, image, error, limit, end] {
          if (!self || self->m_generation != generation)
            return;
          self->join();
          self->m_session.reset();
          emit self->changed();
          if (!image.isNull())
            emit self->finished(image, limit, end);
          else if (!error.isEmpty())
            emit self->failed(error);
          else
            emit self->cancelled();
        },
        Qt::QueuedConnection);
  });
  m_thread->start();
  emit changed();
}

void ScrollCapture::finish() {
  if (m_session)
    m_session->finish();
}

void ScrollCapture::cancel() {
  if (m_session)
    m_session->cancel();
}
