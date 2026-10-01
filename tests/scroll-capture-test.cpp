#include "scroll-capture.hpp"
#include "auto-capture.hpp"
#include "stitch.hpp"
#include <QElapsedTimer>
#include <QPainter>
#include <QRandomGenerator>
#include <QTest>
#include <QThread>
#include <atomic>
#include <cmath>
#include <functional>

bool runStitchChecks();

namespace {
/// A tall page with what makes stitching hard: long runs of identical table
/// rows, blank gaps, pictures and short lines of text.
QImage makeDocument(int width, int height, quint32 seed = 7) {
  QImage doc(width, height, QImage::Format_RGB32);
  doc.fill(QColor("#fbfbf8"));
  QPainter p(&doc);
  QRandomGenerator random(seed);
  int y = 24;
  int section = 0;
  while (y < height - 40) {
    switch (section++ % 6) {
    case 0: // a heading and a paragraph
      p.fillRect(40, y, 180 + random.bounded(width / 3), 22, QColor("#1d2733"));
      y += 40;
      for (int line = 0; line < 6 + int(random.bounded(8)) && y < height - 40; ++line) {
        int x = 40;
        while (x < width - 120) {
          const int word = 18 + random.bounded(70);
          p.fillRect(x, y, word, 9, QColor(70, 78, 90));
          x += word + 8;
        }
        y += 20;
      }
      y += 18;
      break;
    case 1: { // a picture
      const int h = 120 + random.bounded(140);
      QLinearGradient g(0, y, width, y + h);
      g.setColorAt(0, QColor::fromHsv(random.bounded(360), 120, 220));
      g.setColorAt(1, QColor::fromHsv(random.bounded(360), 180, 120));
      p.fillRect(60, y, width - 160, h, g);
      p.setPen(QPen(Qt::white, 3));
      p.drawEllipse(QPoint(width / 2, y + h / 2), h / 3, h / 4);
      y += h + 30;
      break;
    }
    case 2: // a table of identical rows, each with its own number
      for (int row = 0; row < 24 && y < height - 40; ++row) {
        p.fillRect(40, y, width - 120, 1, QColor("#d7dbe0"));
        p.fillRect(56, y + 9, 60, 8, QColor("#5a6472"));
        p.fillRect(220, y + 9, 140, 8, QColor("#5a6472"));
        // Row numbers differ only a little; most of each row repeats.
        for (int bit = 0; bit < 8; ++bit)
          if ((row >> bit) & 1)
            p.fillRect(width - 200 + bit * 8, y + 10, 5, 5, QColor("#5a6472"));
        y += 28;
      }
      y += 20;
      break;
    case 3: // blank space
      y += 150 + random.bounded(150);
      break;
    case 4: // a code block
      p.fillRect(40, y, width - 120, 200, QColor("#20252c"));
      for (int line = 0; line < 9; ++line)
        p.fillRect(56 + (line % 3) * 16, y + 14 + line * 20,
                   80 + random.bounded(width / 2), 8, QColor("#9fd3a8"));
      y += 230;
      break;
    default: // a list
      for (int item = 0; item < 5 && y < height - 40; ++item) {
        p.setBrush(QColor("#3a6ea5"));
        p.setPen(Qt::NoPen);
        p.drawEllipse(48, y + 2, 7, 7);
        p.fillRect(66, y, 120 + random.bounded(width / 2), 9, QColor(70, 78, 90));
        y += 22;
      }
      y += 16;
      break;
    }
  }
  return doc;
}

/// A browser window on a display: a toolbar, a page with a sticky header,
/// a scroll bar and a status line, around a page that scrolls with an eased
/// animation, like a browser's smooth scrolling.
struct FakeDisplay {
  QSize size{1600, 1000};
  QRect window{100, 60, 1200, 880};
  int toolbar = 70, sticky = 48, status = 22, bar = 12;
  QImage page;
  /// Where the page is, and where the last wheel event sends it.
  double scrollY = 0, target = 0;
  double perNotch = 57;
  /// -1 when the wheel runs the other way (natural scrolling).
  int wheel = 1;
  bool ignoresWheel = false;
  bool canPoint = true;
  bool losesWheelFocus = false, wheelFocused = false;
  /// A spinner in the toolbar that never holds still.
  bool spinner = false;
  int spin = 0;
  /// The control the app shows, drawn into the top of the window once it
  /// may appear.
  int coverRows = 0;
  bool coverShown = false;
  /// Scrolls before the user grabs the mouse; -1 never.
  int userTakesPointerAfter = -1;
  int scrolls = 0;
  /// Pixels the user scrolls between pictures once scrolling by hand.
  int handStep = 0;
  /// The user took the mouse.
  bool tookPointer = false;
  /// Picture count at the end of the page while scrolling by hand; the
  /// user presses Done a few pictures later.
  int endGrabs = 0;
  std::function<void()> atEnd;
  QImage lastShown;
  bool opened = false;
  QPointF parked{-1, -1};

  int viewport() const { return window.height() - toolbar - sticky - status; }
  double maxScroll() const { return page.height() - viewport(); }
  bool scrollingByHand() const {
    return handStep > 0 && coverShown && (!canPoint || tookPointer);
  }

  void advance() {
    // Ease toward the target, as smooth scrolling does, then land on it.
    const double gap = target - scrollY;
    if (std::abs(gap) < 1.5)
      scrollY = target;
    else
      scrollY += gap * 0.45;
  }

  QImage render() {
    QImage frame(size, QImage::Format_ARGB32);
    frame.fill(QColor("#2b3a42"));
    QPainter p(&frame);
    const QRect w = window;
    p.fillRect(w.x(), w.y(), w.width(), toolbar, QColor("#e9eaee"));
    p.fillRect(w.x() + 90, w.y() + 22, 600, 26, Qt::white);
    p.fillRect(w.x() + 20, w.y() + 25, 40, 20, QColor("#6b7380"));
    if (spinner)
      p.fillRect(w.x() + 1100 + (spin++ % 4) * 4, w.y() + 30, 6, 6,
                 QColor("#3a6ea5"));
    const int top = w.y() + toolbar;
    p.fillRect(w.x(), top, w.width(), sticky, QColor("#123456"));
    p.fillRect(w.x() + 30, top + 16, 140, 16, QColor("#f0c674"));
    const int view = viewport();
    const int y0 = qRound(scrollY);
    p.drawImage(QPoint(w.x(), top + sticky),
                page.copy(0, y0, w.width() - bar, view));
    // The scroll bar and its thumb.
    const int barX = w.x() + w.width() - bar;
    p.fillRect(barX, top + sticky, bar, view, QColor("#eeeeee"));
    const int thumb = std::max(30, view * view / page.height());
    const int thumbY =
        top + sticky + int((view - thumb) * (y0 / std::max(1.0, maxScroll())));
    p.fillRect(barX + 2, thumbY, bar - 4, thumb, QColor("#a0a4aa"));
    p.fillRect(w.x(), top + sticky + view, w.width(), status, QColor("#dfe1e5"));
    p.fillRect(w.x() + 8, top + sticky + view + 6, 90, 10, QColor("#7a8290"));
    if (coverShown && coverRows > 0)
      p.fillRect(w.x() + 300, w.y(), 400, coverRows, QColor("#ff00ff"));
    return frame;
  }

  /// What a perfect capture of the whole page looks like.
  QImage expected() const {
    const int height = toolbar + sticky + page.height() + status;
    QImage image(window.width() - bar, height, QImage::Format_ARGB32);
    QPainter p(&image);
    FakeDisplay copy = *this;
    copy.coverShown = false;
    copy.spinner = false;
    copy.scrollY = 0;
    const QImage top = copy.render().copy(window.x(), window.y(),
                                          window.width() - bar, toolbar + sticky);
    p.drawImage(0, 0, top);
    p.drawImage(0, toolbar + sticky, page.copy(0, 0, window.width() - bar, page.height()));
    copy.scrollY = copy.maxScroll();
    const QImage bottom =
        copy.render().copy(window.x(), window.y() + window.height() - status,
                           window.width() - bar, status);
    p.drawImage(0, height - status, bottom);
    return image;
  }

  Scrolling::Desktop desktop(std::atomic_bool &stopped) {
    Scrolling::Desktop d;
    d.open = [this](QString &) {
      opened = true;
      return true;
    };
    d.grab = [this](QImage &frame, QString &, int) {
      if (scrollingByHand()) {
        target = std::min(maxScroll(), target + handStep);
        if (scrollY >= maxScroll() && ++endGrabs > 6 && atEnd)
          atEnd();
      }
      advance();
      const QImage shown = render();
      if (!lastShown.isNull() && shown == lastShown) {
        QThread::msleep(1);
        return false; // no damage: the grab times out
      }
      lastShown = shown;
      frame = shown;
      return true;
    };
    d.stopped = [&stopped] { return stopped.load(); };
    d.openPointer = [this] { return canPoint; };
    d.park = [this](QPointF at) {
      parked = at;
      wheelFocused = true;
      return true;
    };
    d.scroll = [this](int notches) {
      ++scrolls;
      if (!ignoresWheel && (!losesWheelFocus || wheelFocused))
        target = std::clamp(target + notches * perNotch * wheel, 0.0, maxScroll());
      if (losesWheelFocus)
        wheelFocused = false; // mapping/updating a layer can steal pointer focus
      return true;
    };
    d.pointerMoved = [this](QPointF) {
      if (userTakesPointerAfter >= 0 && scrolls >= userTakesPointerAfter)
        tookPointer = true; // the user has the mouse and scrolls by hand
      return tookPointer;
    };
    d.closePointer = [] {};
    return d;
  }
};

Scrolling::Timing fastTiming() {
  Scrolling::Timing t;
  t.firstFrameMs = 0;
  t.motionWaitMs = 40;
  t.settleGrabMs = 10;
  t.maxSettleMs = 300;
  t.manualIntervalMs = 0;
  t.manualGrabMs = 10;
  return t;
}

Scrolling::Plan planFor(const FakeDisplay &display) {
  const QSizeF s = display.size;
  Scrolling::Plan plan;
  plan.area = QRectF(display.window.x() / s.width(), display.window.y() / s.height(),
                     display.window.width() / s.width(),
                     display.window.height() / s.height());
  plan.anchor = Scrolling::anchorFor(plan.area, true, 0, s);
  plan.home = plan.anchor;
  return plan;
}

/// Rows of `actual` that differ from `expected` by more than a trace.
int differingRows(const QImage &actual, const QImage &expected) {
  const QImage a = actual.convertToFormat(QImage::Format_RGB32);
  const QImage e = expected.convertToFormat(QImage::Format_RGB32);
  const int width = std::min(a.width(), e.width());
  int rows = std::abs(a.height() - e.height());
  for (int y = 0; y < std::min(a.height(), e.height()); ++y) {
    int bad = 0;
    for (int x = 0; x < width; ++x)
      if (a.pixel(x, y) != e.pixel(x, y))
        ++bad;
    if (bad > 0)
      ++rows;
  }
  return rows;
}
} // namespace

class ScrollCaptureTest : public QObject {
  Q_OBJECT
  QImage capture(FakeDisplay &display, QString &error,
                 std::function<void(Scrolling::Session &)> during = {},
                 Scrolling::Plan plan = {}, bool *limit = nullptr,
                 bool *end = nullptr) {
    std::atomic_bool stopped{false};
    if (plan.area.isEmpty())
      plan = planFor(display);
    QList<Scrolling::Progress> progress;
    Scrolling::Session session(
        display.desktop(stopped), plan,
        [&](const Scrolling::Progress &p) {
          progress << p;
          if (qEnvironmentVariableIsSet("OMAFRAME_SCROLL_DEBUG"))
            qInfo() << "[DEBUG-scroll] progress" << p.length << "actual"
                    << display.window.height() + qRound(display.scrollY);
        },
        [&] { display.coverShown = true; }, fastTiming());
    if (during)
      during(session);
    display.atEnd = [&session] { session.finish(); };
    QElapsedTimer clock;
    clock.start();
    const QImage image = session.run(error);
    if (limit)
      *limit = session.reachedLimit();
    if (end)
      *end = session.reachedEnd();
    qInfo().noquote() << QString("capture took %1 ms, %2 scrolls, %3 reports")
                             .arg(clock.elapsed())
                             .arg(display.scrolls)
                             .arg(progress.size());
    return image;
  }

private slots:
  void stitcherChecks() { QVERIFY(runStitchChecks()); }

  void areaPixelsRoundEdges() {
    // At a fractional scale, edges round on their own so the crop never
    // drifts a pixel against the area.
    const QRect r = Scrolling::areaPixels(QRectF(0.1, 0.25, 0.333, 0.5), {2560, 1440});
    QCOMPARE(r, QRect(256, 360, 852, 720));
    QCOMPARE(Scrolling::areaPixels(QRectF(-1, -1, 3, 3), {100, 50}),
             QRect(0, 0, 100, 50));
  }

  void anchorStaysInsideAndBelowTheControl() {
    const QRectF area(0.1, 0.1, 0.5, 0.8);
    const QPointF window = Scrolling::anchorFor(area, true, 0.05, {1920, 1080});
    QVERIFY(area.contains(window));
    QVERIFY(window.y() > area.top() + 0.05);
    QVERIFY(window.x() > area.center().x());
    const QPointF drawn = Scrolling::anchorFor(area, false, 0.1, {1920, 1080});
    QCOMPARE(drawn.x(), area.center().x());
    QVERIFY(drawn.y() > area.top() + 0.1);
  }

  void restoresWheelFocusAfterControlUpdates() {
    FakeDisplay display;
    display.page = makeDocument(display.window.width() - display.bar, 4200);
    display.losesWheelFocus = true;
    QString error;
    const auto image = capture(display, error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(image.height(), display.expected().height());
    QCOMPARE(differingRows(image, display.expected()), 0);
  }

  void capturesAWholePageExactly() {
    FakeDisplay display;
    display.page = makeDocument(1200, 5200);
    QString error;
    bool end = false;
    const QImage image = capture(display, error, {}, {}, nullptr, &end);
    QVERIFY2(!image.isNull(), qPrintable(error));
    QVERIFY(end);
    const QImage expected = display.expected();
    QCOMPARE(image.height(), expected.height());
    // The scroll bar is gone, and nothing of the page with it.
    QVERIFY2(image.width() <= expected.width() &&
                 image.width() >= expected.width() - 6,
             qPrintable(QString::number(image.width())));
    QCOMPARE(differingRows(image, expected), 0);
  }

  void ignoresASpinnerAndKeepsTheControlOut() {
    FakeDisplay display;
    display.page = makeDocument(1200, 3000, 11);
    display.spinner = true;
    display.coverRows = 40;
    Scrolling::Plan plan = planFor(display);
    plan.coverTop = double(display.coverRows) / display.size.height();
    QString error;
    const QImage image = capture(display, error, {}, plan);
    QVERIFY2(!image.isNull(), qPrintable(error));
    // The control never appears in the result.
    for (int y = 0; y < image.height(); y += 3)
      for (int x = 0; x < image.width(); x += 7)
        QVERIFY(image.pixel(x, y) != QColor("#ff00ff").rgb());
    display.spinner = false;
    QImage expected = display.expected();
    QCOMPARE(image.height(), expected.height());
    // Only the spinner's own rows may differ.
    QVERIFY(differingRows(image, expected) <= 6);
  }

  void followsTheWheelEitherWay() {
    FakeDisplay display;
    display.page = makeDocument(1200, 3600, 3);
    display.wheel = -1;
    QString error;
    const QImage image = capture(display, error);
    QVERIFY2(!image.isNull(), qPrintable(error));
    QCOMPARE(differingRows(image, display.expected()), 0);
  }

  void handsOverWhenTheUserTakesTheMouse() {
    FakeDisplay display;
    display.page = makeDocument(1200, 4800, 5);
    display.userTakesPointerAfter = 4;
    display.handStep = 37;
    QString error;
    const QImage image = capture(display, error);
    QVERIFY2(!image.isNull(), qPrintable(error));
    QCOMPARE(differingRows(image, display.expected()), 0);
  }

  void scrollsByHandWithoutAPointer() {
    FakeDisplay display;
    display.page = makeDocument(1200, 4000, 9);
    display.canPoint = false;
    display.handStep = 61;
    QString error;
    const QImage image = capture(display, error);
    QVERIFY2(!image.isNull(), qPrintable(error));
    QCOMPARE(differingRows(image, display.expected()), 0);
  }

  void handsOverWhenThePageIgnoresTheWheel() {
    FakeDisplay display;
    display.page = makeDocument(1200, 3000, 13);
    display.ignoresWheel = true;
    display.handStep = 45;
    // The user starts scrolling once automatic scrolling gives up.
    QString error;
    std::atomic_bool stopped{false};
    QList<Scrolling::Progress> progress;
    Scrolling::Session *running = nullptr;
    display.atEnd = [&] { running->finish(); };
    Scrolling::Session session(
        display.desktop(stopped), planFor(display),
        [&](const Scrolling::Progress &p) {
          progress << p;
          if (p.mode == Scrolling::Progress::Mode::Manual)
            display.tookPointer = true;
        },
        [&] { display.coverShown = true; }, fastTiming());
    running = &session;
    const QImage image = session.run(error);
    QVERIFY2(!image.isNull(), qPrintable(error));
    QVERIFY(display.tookPointer);
    QCOMPARE(differingRows(image, display.expected()), 0);
  }

  void finishingEarlyKeepsTheTop() {
    FakeDisplay display;
    display.page = makeDocument(1200, 9000, 17);
    QString error;
    std::atomic_bool stopped{false};
    Scrolling::Plan plan = planFor(display);
    Scrolling::Session *running = nullptr;
    auto desktop = display.desktop(stopped);
    auto scroll = desktop.scroll;
    desktop.scroll = [&](int notches) {
      if (display.scrolls == 6)
        running->finish();
      return scroll(notches);
    };
    Scrolling::Session session(desktop, plan, {}, {}, fastTiming());
    running = &session;
    const QImage image = session.run(error);
    QVERIFY2(!image.isNull(), qPrintable(error));
    QVERIFY(!session.reachedEnd());
    QVERIFY(image.height() > display.window.height());
    QVERIFY(image.height() < 9000);
    // What there is matches the page, all the way down, including the
    // status line at the bottom.
    const QImage expected = display.expected();
    const int body = image.height() - display.status;
    QCOMPARE(differingRows(image.copy(0, 0, image.width(), body),
                           expected.copy(0, 0, expected.width(), body)),
             0);
  }

  void cancellingKeepsNothing() {
    FakeDisplay display;
    display.page = makeDocument(1200, 6000, 19);
    QString error;
    std::atomic_bool stopped{false};
    Scrolling::Session *running = nullptr;
    auto desktop = display.desktop(stopped);
    auto scroll = desktop.scroll;
    desktop.scroll = [&](int notches) {
      if (display.scrolls == 3)
        running->cancel();
      return scroll(notches);
    };
    Scrolling::Session session(desktop, planFor(display), {}, {}, fastTiming());
    running = &session;
    QVERIFY(session.run(error).isNull());
    QVERIFY(error.isEmpty());
  }

  void stopsAtTheSizeLimit() {
    FakeDisplay display;
    display.size = {800, 1000};
    display.window = {100, 40, 300, 900};
    display.page = makeDocument(300, 40000, 23);
    QString error;
    bool limit = false;
    const QImage image = capture(display, error, {}, {}, &limit);
    QVERIFY2(!image.isNull(), qPrintable(error));
    QVERIFY(limit);
    QVERIFY(image.height() <= stitch::kMaxStitchedEdge);
    QVERIFY(qint64(image.width()) * image.height() <= stitch::kMaxStitchedPixels);
    QVERIFY(image.height() > 30000);
  }

  void restoredControlRowsCountTowardTheSizeLimit() {
    FakeDisplay display;
    display.page = makeDocument(display.window.width() - display.bar, 34000);
    display.coverRows = display.toolbar;
    QString error;
    bool limit = false;
    const QImage image = capture(display, error, {}, {}, &limit);
    QVERIFY2(!image.isNull(), qPrintable(error));
    QVERIFY(limit);
    QVERIFY(image.height() <= stitch::kMaxStitchedEdge);
    QVERIFY(qint64(image.width()) * image.height() <= stitch::kMaxStitchedPixels);
    QVERIFY(image.height() > 31000);
  }

  void pendingManualFramesRespectTheSizeLimit() {
    QImage document(64, 240, QImage::Format_RGBA8888);
    QRandomGenerator random(123);
    for (int y = 0; y < document.height(); ++y)
      for (int x = 0; x < document.width(); ++x)
        document.setPixelColor(x, y, QColor::fromRgb(random.generate()));
    const QImage first = document.copy(0, 0, 64, 200);
    QString error;
    bool ok = false;
    stitch::StitchAccumulator accumulator(first, stitch::Axis::Vertical, ok, error);
    QVERIFY(ok);
    while (accumulator.extent() < 31990)
      QVERIFY(accumulator.pushForward(first,
          std::min(100, 31990 - accumulator.extent()), error));
    const int kept = accumulator.frameCount();
    stitch::ManualCapture manual(stitch::Axis::Vertical, std::move(accumulator),
        stitch::downsampleToGray(first, stitch::Axis::Vertical), kept);
    const auto out = manual.feed(document.copy(0, 12, 64, 200));
    QCOMPARE(out.event, stitch::ManualCapture::Event::Full);
    QCOMPARE(out.keptFrames, kept);
    QCOMPARE(out.pendingDelta, 0);
    const QImage image = manual.finish(error);
    QVERIFY2(!image.isNull(), qPrintable(error));
    QCOMPARE(image.height(), 31990);
  }

  void probeStopsAtTheSizeLimitWithoutLosingFrames() {
    stitch::AutoCapture session(stitch::Axis::Vertical);
    auto frameAt = [](int top) {
      QImage frame(64, 360, QImage::Format_RGBA8888);
      for (int y = 0; y < frame.height(); ++y)
        for (int x = 0; x < frame.width(); ++x) {
          const quint32 hash = quint32((y + top) % 72) * 2654435761u
                               ^ quint32(x) * 40503u;
          frame.setPixelColor(x, y, QColor(int(hash >> 24), int((hash >> 16) & 255),
                                          int((hash >> 8) & 255)));
        }
      return frame;
    };
    QCOMPARE(session.feed(frameAt(0)).event, stitch::AutoCapture::Event::Seeded);
    int top = 0;
    stitch::AutoCapture::Outcome out;
    for (int step = 0; step < 500; ++step) {
      top += 54;
      out = session.feed(frameAt(top));
      if (out.event == stitch::AutoCapture::Event::Halted)
        break;
      QCOMPARE(out.event, stitch::AutoCapture::Event::ProbeStarted);
      top += 18;
      out = session.feed(frameAt(top));
      if (out.event == stitch::AutoCapture::Event::Halted)
        break;
      QCOMPARE(out.event, stitch::AutoCapture::Event::Committed);
    }
    QCOMPARE(out.event, stitch::AutoCapture::Event::Halted);
    QCOMPARE(out.haltReason, stitch::AutoCapture::HaltReason::ReachedLimit);
    QString error;
    const QImage image = session.finish(error);
    QVERIFY2(!image.isNull(), qPrintable(error));
    QVERIFY(image.height() > 31000);
    QVERIFY(image.height() <= stitch::kMaxStitchedEdge);
  }

  void aDisplayThatStopsKeepsWhatWasCaptured() {
    FakeDisplay display;
    display.page = makeDocument(1200, 9000, 29);
    QString error;
    std::atomic_bool stopped{false};
    auto desktop = display.desktop(stopped);
    auto scroll = desktop.scroll;
    desktop.scroll = [&](int notches) {
      if (display.scrolls == 5)
        stopped = true;
      return scroll(notches);
    };
    auto grab = desktop.grab;
    desktop.grab = [&](QImage &frame, QString &e, int t) {
      if (stopped) {
        e = "Compositor stopped native output capture";
        return false;
      }
      return grab(frame, e, t);
    };
    Scrolling::Session session(desktop, planFor(display), {}, {}, fastTiming());
    const QImage image = session.run(error);
    QVERIFY2(!image.isNull(), qPrintable(error));
    QVERIFY(image.height() > display.window.height());
  }

  void refusesATinyArea() {
    FakeDisplay display;
    display.page = makeDocument(1200, 3000);
    Scrolling::Plan plan = planFor(display);
    plan.area = QRectF(0.2, 0.2, 0.02, 0.03);
    QString error;
    QVERIFY(capture(display, error, {}, plan).isNull());
    QVERIFY(!error.isEmpty());
  }

  void findsAScrollBar() {
    // A page scrolled by 80 pixels with a thumb that moves the other way.
    QImage page = makeDocument(400, 1200, 31).convertToFormat(QImage::Format_RGBA8888);
    Scrolling::EdgeStrip strip;
    auto frameAt = [&](int y) {
      QImage f = page.copy(0, y, 400, 500);
      QPainter p(&f);
      p.fillRect(388, 0, 12, 500, QColor("#eeeeee"));
      p.fillRect(390, y / 2, 8, 60, QColor("#999999"));
      return f;
    };
    strip.observe(frameAt(0), frameAt(80), 80);
    QCOMPARE(strip.width(), 0); // one step proves nothing
    strip.observe(frameAt(80), frameAt(160), 80);
    strip.observe(frameAt(160), frameAt(240), 80);
    QVERIFY(strip.width() >= 12 && strip.width() <= 16);
    // A page without one keeps every column.
    Scrolling::EdgeStrip none;
    for (int y = 0; y < 300; y += 80)
      none.observe(page.copy(0, y, 400, 500), page.copy(0, y + 80, 400, 500), 80);
    QCOMPARE(none.width(), 0);
  }
};

QTEST_GUILESS_MAIN(ScrollCaptureTest)
#include "scroll-capture-test.moc"
