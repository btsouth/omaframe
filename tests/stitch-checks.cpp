// Adapted from Omasnap's stitch-smoke.cpp, copyright 2026 Tobi Lütke, MIT.
// See docs/OMASNAP-LICENSE.
/** @fileoverview Checks for the scroll-capture stitcher. Synthetic frames
 *  with known deltas exercise the classifier, the accumulator and the
 *  automatic capture's verification. */
#include "auto-capture.hpp"
#include "stitch.hpp"

#include <QImage>
#include <QPainter>

#include <cmath>
#include <cstdio>
#include <utility>
#include <cstring>
#include <initializer_list>
#include <QDebug>
#include <cstdint>

using namespace stitch;

/// Every failure names its line, so a red run says which check broke.
#define CHECK_FAILED                                                          \
  do {                                                                        \
    std::fprintf(stderr, "stitch check failed at line %d\n", __LINE__);       \
    return false;                                                             \
  } while (0)

namespace {
// A deterministic textured strip so shifts have something to correlate.
QImage makeTexture(int width, int height) {
  QImage image(width, height, QImage::Format_ARGB32);
  image.fill(Qt::white);
  for (int y = 0; y < height; ++y) {
    QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
    // Aperiodic along both axes so a shift has a unique correlation peak.
    const std::uint32_t rowHash = static_cast<std::uint32_t>(y) * 2654435761u;
    for (int x = 0; x < width; ++x) {
      const std::uint32_t h =
          (static_cast<std::uint32_t>(x) * 40503u) ^ rowHash ^ (rowHash >> 15);
      const int v = static_cast<int>(h >> 24) & 0xff;
      row[x] = qRgb(v, (v * 3) & 0xff, (v * 7) & 0xff);
    }
  }
  return image;
}
} // namespace


namespace {
// Synthetic fixtures for the lookahead resolver's ambiguous cases.
GrayView exactlyPeriodicGray(int documentY) {
  constexpr int kWidth = 96, kHeight = 360, kPeriod = 72;
  GrayView view;
  view.width = kWidth;
  view.height = kHeight;
  view.sourceWidth = kWidth;
  view.pixels.reserve(static_cast<std::size_t>(kWidth) * kHeight);
  for (int screenY = 0; screenY < kHeight; ++screenY) {
    const std::uint64_t row = static_cast<std::uint64_t>((documentY + screenY) % kPeriod);
    for (int x = 0; x < kWidth; ++x) {
      std::uint64_t value = row * 0x9E3779B1ull + static_cast<std::uint64_t>(x) * 0x85EBCA77ull;
      value ^= value >> 17;
      view.pixels.push_back(static_cast<std::uint8_t>(24 + value % 210));
    }
  }
  return view;
}

GrayView periodicTerminalGray(int documentY) {
  constexpr int kWidth = 96, kHeight = 360, kBlockHeight = 144, kBackgroundPeriod = 48;
  GrayView view;
  view.width = kWidth;
  view.height = kHeight;
  view.sourceWidth = kWidth;
  view.pixels.reserve(static_cast<std::size_t>(kWidth) * kHeight);
  for (int screenY = 0; screenY < kHeight; ++screenY) {
    const int docY = documentY + screenY;
    const int block = docY / kBlockHeight;
    const int inBlock = docY % kBlockHeight;
    const int line = inBlock / 12;
    const bool glyphRow = inBlock % 12 >= 2 && inBlock % 12 < 7;
    for (int x = 0; x < kWidth; ++x) {
      const int backgroundPhase = (screenY / 6 + x / 12) % (kBackgroundPeriod / 12);
      std::uint8_t value = static_cast<std::uint8_t>(20 + backgroundPhase);
      const int glyphStart = 8 + (line * 7) % 28;
      const int glyphEnd = std::min(glyphStart + 28 + (line * 5) % 34, kWidth - 8);
      if (glyphRow && x >= glyphStart && x < glyphEnd && (x + line) % 5 != 0)
        value = static_cast<std::uint8_t>(150 + (line % 6) * 8);
      if (inBlock >= 4 && inBlock < 8 && x >= 12 && x < 36)
        value = static_cast<std::uint8_t>(72 + (block % 4) * 5);
      view.pixels.push_back(value);
    }
  }
  return view;
}

GrayView periodicTerminalWithUniqueGray(int documentY) {
  constexpr int kUniqueTop = 328, kUniqueBottom = 344;
  GrayView view = periodicTerminalGray(documentY);
  for (int screenY = 0; screenY < view.height; ++screenY) {
    const int absoluteY = documentY + screenY;
    if (absoluteY < kUniqueTop || absoluteY >= kUniqueBottom)
      continue;
    for (int x = 14; x < view.width - 10; ++x) {
      const std::uint64_t value =
          static_cast<std::uint64_t>(absoluteY) * 0x9E3779B1ull +
          static_cast<std::uint64_t>(x) * 0x85EBCA77ull;
      view.pixels[static_cast<std::size_t>(screenY) * view.width + x] =
          static_cast<std::uint8_t>(48 + value % 190);
    }
  }
  return view;
}

GrayView uniqueDocumentGray(int documentY) {
  constexpr int kWidth = 96, kHeight = 360;
  GrayView view;
  view.width = kWidth;
  view.height = kHeight;
  view.sourceWidth = kWidth;
  view.pixels.reserve(static_cast<std::size_t>(kWidth) * kHeight);
  for (int screenY = 0; screenY < kHeight; ++screenY) {
    const std::uint64_t absoluteY = static_cast<std::uint64_t>(documentY + screenY);
    for (int x = 0; x < kWidth; ++x) {
      std::uint64_t value = absoluteY * 0x9E3779B1ull + static_cast<std::uint64_t>(x) * 0x85EBCA77ull;
      value ^= value >> 15;
      value *= 0xC2B2AE3Dull;
      view.pixels.push_back(static_cast<std::uint8_t>(16 + value % 220));
    }
  }
  return view;
}

/// The lookahead resolver: the alignment verification auto-scroll depends on.
bool runLookaheadChecks() {
  using Tag = ForwardLookaheadResolution::Tag;
  const auto literalSet = [](std::initializer_list<ForwardMatchCandidate> candidates,
                             bool truncated, int searchMaxDelta) {
    ForwardCandidateSet set;
    set.candidates = candidates;
    set.truncated = truncated;
    set.searchMaxDelta = searchMaxDelta;
    return set;
  };

  // A consistent cumulative candidate is required: matching adjacent seams
  // whose sum disagrees with every cumulative basin stay unresolved (the
  // best-effort path is preserved for an explicit user choice).
  {
    const ForwardLookaheadResolution resolution = resolveForwardCandidateSets(
        literalSet({{54, 0.0}}, false, 100), literalSet({{18, 0.0}}, false, 100),
        literalSet({{80, 0.0}}, false, 100), true);
    if (resolution.tag != Tag::Unresolved || !resolution.path ||
        !(*resolution.path == ForwardMatchPath{54, 18, 0.0}))
      CHECK_FAILED;
  }
  // A seam above the matcher-grade error ceiling poisons every path.
  {
    const double bad = kMaxMatchError + 0.01;
    const ForwardLookaheadResolution resolution = resolveForwardCandidateSets(
        literalSet({{54, 0.0}, {126, 0.0}}, false, 240),
        literalSet({{18, bad}, {90, bad}}, false, 240),
        literalSet({{72, 0.0}, {144, 0.0}, {216, 0.0}}, false, 240), true);
    if (resolution.tag != Tag::Unresolved || !resolution.path)
      CHECK_FAILED;
  }
  // A truncated landscape cannot be Resolved, but a retained matcher-grade
  // path is still accepted for known-forward auto capture (cadence prior).
  {
    const ForwardLookaheadResolution resolution = resolveForwardCandidateSets(
        literalSet({{54, 0.0}, {126, 0.0}}, true, 240),
        literalSet({{18, 0.0}, {90, 0.0}}, false, 240),
        literalSet({{72, 0.0}, {144, 0.0}, {216, 0.0}}, false, 240), true);
    if (resolution.tag != Tag::LowErrorPeriodic ||
        !(*resolution.path == ForwardMatchPath{54, 18, 0.0}))
      CHECK_FAILED;
  }
  // Non-canonical cadence with matcher-grade errors is still accepted.
  {
    const ForwardLookaheadResolution resolution = resolveForwardCandidateSets(
        literalSet({{420, 4.0}, {700, 4.2}}, true, 1200),
        literalSet({{360, 5.0}, {640, 5.3}}, false, 1200),
        literalSet({{780, 6.0}, {1060, 6.5}}, false, 1200), true);
    if (resolution.tag != Tag::LowErrorPeriodic ||
        !(*resolution.path == ForwardMatchPath{420, 360, 15.0}))
      CHECK_FAILED;
  }

  // A stationary probe never verifies a truncated candidate search.
  {
    const GrayView frame = uniqueDocumentGray(0);
    const ForwardMatchCandidate candidate{60, 0.5};
    ForwardCandidateSet first;
    first.candidates = {candidate};
    first.truncated = true;
    first.searchMaxDelta = 240;
    const ForwardLookahead pending(frame, frame, Axis::Vertical, first,
                                   {{MotionKind::Ambiguous, 60}, 0.5, 1.0});
    const ForwardLookaheadResolution resolution = pending.resolve(frame);
    if (resolution.tag != Tag::StationaryProbe || !resolution.firstMatch ||
        resolution.firstMatch->tag != StationaryProbeFirstMatch::Tag::Ambiguous ||
        resolution.firstMatch->candidate != std::optional(candidate))
      CHECK_FAILED;
  }

  // Exactly periodic content: the strict resolver stays hint-free while the
  // known-forward automatic resolver uses the 3:1 cadence.
  {
    const GrayView f0 = exactlyPeriodicGray(0);
    const GrayView f1 = exactlyPeriodicGray(54);
    const GrayView f2 = exactlyPeriodicGray(54 + 18);
    const ForwardMatch match =
        classifyForwardWithLookahead(f0, f1, Axis::Vertical);
    if (match.tag != ForwardMatch::Tag::Ambiguous || !match.ambiguous)
      CHECK_FAILED;
    const ForwardLookahead &pending = *match.ambiguous;
    if (pending.candidates().size() < 2)
      CHECK_FAILED;
    if (pending.resolve(f2).tag != Tag::Unresolved || !pending.resolve(f2).path)
      CHECK_FAILED;
    const ForwardLookaheadResolution automatic = pending.resolveAuto(f2);
    if (automatic.tag != Tag::LowErrorPeriodic ||
        automatic.path->firstDelta != 54 || automatic.path->secondDelta != 18)
      CHECK_FAILED;
    // A trusted physical bound isolates only the small repeated basin.
    const std::optional<ForwardMatchCandidate> bounded =
        pending.uniqueCandidateAtMost(60);
    if (!bounded || bounded->delta != 54)
      CHECK_FAILED;
    if (pending.uniqueCandidateAtMost(200))
      CHECK_FAILED; // 54 and 126 both qualify: not unique
    // A probe that did not move reports a stationary probe, not a new band.
    if (pending.resolve(f1).tag != Tag::StationaryProbe)
      CHECK_FAILED;
  }

  // Full-overlap scoring sees the unique evidence near the viewport edge,
  // rather than delaying it until a fixed central sample happens to see it.
  // The strict lookahead resolver must confirm the same path independently.
  {
    const GrayView f0 = periodicTerminalWithUniqueGray(0);
    const GrayView f1 = periodicTerminalWithUniqueGray(18);
    const GrayView f2 = periodicTerminalWithUniqueGray(18 + 182);
    const ForwardMatch match =
        classifyForwardWithLookahead(f0, f1, Axis::Vertical);
    if (match.tag != ForwardMatch::Tag::Classified ||
        match.classified.motion.kind != MotionKind::Forward ||
        match.classified.motion.delta != 18)
      CHECK_FAILED;
    const ForwardLookahead pending(f0, f1, Axis::Vertical,
                                   forwardCandidateSet(f0, f1, Axis::Vertical),
                                   match.classified);
    if (std::none_of(pending.candidates().begin(), pending.candidates().end(),
                     [](const ForwardMatchCandidate &c) { return c.delta == 18; }))
      CHECK_FAILED;
    const ForwardLookaheadResolution resolution = pending.resolve(f2);
    if (resolution.tag != Tag::Resolved || resolution.path->firstDelta != 18 ||
        resolution.path->secondDelta != 182)
      CHECK_FAILED;
  }
  return true;
}
} // namespace


namespace {
/// The automatic capture machinery: calibration, handshake, the probe
/// decision table, and full feed() sequences per state-machine row.
bool runAutoCaptureChecks() {
  using Ack = AutoCapture::Ack;
  using Event = AutoCapture::Event;
  using Kind = AutoProbeDecision::Kind;
  using PauseReason = AutoProbeDecision::PauseReason;

  // --- AutoStepCalibration --------------------------------------------------
  {
    AutoStepCalibration calibration;
    if (calibration.endpointUpperBound())
      CHECK_FAILED; // < 3 samples
    calibration.recordVerifiedNormalStep(230);
    calibration.recordVerifiedNormalStep(0); // ignored
    calibration.recordVerifiedNormalStep(231);
    if (calibration.endpointUpperBound())
      CHECK_FAILED;
    calibration.recordVerifiedNormalStep(232);
    // median 231, jitter max(ceil(231*5%)=12, 4) = 12; bound = 232+12.
    if (calibration.endpointUpperBound() != std::optional(244))
      CHECK_FAILED;
    calibration.recordVerifiedNormalStep(231);
    calibration.recordVerifiedNormalStep(232);
    calibration.recordVerifiedNormalStep(231); // window slides to 5 samples
    if (calibration.endpointUpperBound() != std::optional(244))
      CHECK_FAILED;
  }
  {
    AutoStepCalibration unstable;
    unstable.recordVerifiedNormalStep(180);
    unstable.recordVerifiedNormalStep(231);
    unstable.recordVerifiedNormalStep(300);
    if (unstable.endpointUpperBound())
      CHECK_FAILED; // spread beyond jitter: no bound, keep the pause
  }

  // --- autoProbeDecision table ---------------------------------------------
  {
    const ForwardMatchPath path{54, 18, 0.0};
    ForwardLookaheadResolution resolved;
    resolved.tag = ForwardLookaheadResolution::Tag::Resolved;
    resolved.path = path;
    AutoProbeDecision decision = autoProbeDecision(resolved, 0, std::nullopt);
    if (decision.kind != Kind::Commit || decision.periodic || !(decision.path == path))
      CHECK_FAILED;
    resolved.tag = ForwardLookaheadResolution::Tag::LowErrorPeriodic;
    decision = autoProbeDecision(resolved, 0, std::nullopt);
    if (decision.kind != Kind::Commit || !decision.periodic)
      CHECK_FAILED;
    ForwardLookaheadResolution unresolved;
    unresolved.tag = ForwardLookaheadResolution::Tag::Unresolved;
    unresolved.path = path;
    decision = autoProbeDecision(unresolved, 1, std::nullopt);
    if (decision.kind != Kind::Pause ||
        decision.pauseReason != PauseReason::StillAmbiguous ||
        decision.bestEffort != std::optional(path))
      CHECK_FAILED;
    ForwardLookaheadResolution stationary;
    stationary.tag = ForwardLookaheadResolution::Tag::StationaryProbe;
    StationaryProbeFirstMatch unique;
    unique.tag = StationaryProbeFirstMatch::Tag::Unique;
    unique.candidate = ForwardMatchCandidate{60, 0.5};
    stationary.firstMatch = unique;
    if (autoProbeDecision(stationary, 0, std::nullopt).kind != Kind::ProbeAgain)
      CHECK_FAILED; // first stationary probe retries
    decision = autoProbeDecision(stationary, 1, std::nullopt);
    if (decision.kind != Kind::End ||
        decision.endCandidate != std::optional(ForwardMatchCandidate{60, 0.5}))
      CHECK_FAILED;
    StationaryProbeFirstMatch ambiguous;
    ambiguous.tag = StationaryProbeFirstMatch::Tag::Ambiguous;
    ambiguous.candidate = ForwardMatchCandidate{18, 0.0};
    stationary.firstMatch = ambiguous;
    const ForwardMatchCandidate calibrated{22, 0.1};
    decision = autoProbeDecision(stationary, 1, calibrated);
    if (decision.kind != Kind::End || decision.endCandidate != std::optional(calibrated))
      CHECK_FAILED; // the calibrated physical bound wins
    decision = autoProbeDecision(stationary, 1, std::nullopt);
    if (decision.kind != Kind::End ||
        decision.endCandidate != std::optional(ForwardMatchCandidate{18, 0.0}))
      CHECK_FAILED; // matcher-ranked best effort finishes without a pause
    ambiguous.candidate = std::nullopt;
    stationary.firstMatch = ambiguous;
    decision = autoProbeDecision(stationary, 1, std::nullopt);
    if (decision.kind != Kind::Pause || decision.pauseReason != PauseReason::Stationary)
      CHECK_FAILED;
  }

  // Widen a GrayView fixture into an RGB image whose vertical downsample
  // reproduces it exactly (each gray pixel repeated kDownsampleCross times;
  // gray luma is the identity).
  const auto grayToImage = [](const GrayView &view) {
    QImage image(view.width * kDownsampleCross, view.height,
                 QImage::Format_RGBA8888);
    for (int y = 0; y < view.height; ++y) {
      QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
      for (int x = 0; x < image.width(); ++x) {
        const std::uint8_t v =
            view.pixels[static_cast<std::size_t>(y) * view.width +
                        x / kDownsampleCross];
        row[x] = qRgba(v, v, v, 255);
      }
    }
    return image;
  };

  // --- AutoCapture sequences -----------------------------------------------
  // (a) Seed, three verified forward steps, then two stationary cycles.
  {
    const QImage doc = makeTexture(64, 500);
    const auto window = [&](int top) { return doc.copy(0, top, 64, 200); };
    AutoCapture session(Axis::Vertical);
    AutoCapture::Outcome out = session.feed(window(0));
    if (out.event != Event::Seeded || out.ack != Ack::Normal)
      CHECK_FAILED;
    int height = 200;
    for (const int top : {60, 120, 180}) {
      out = session.feed(window(top));
      if (out.event != Event::Appended || out.ack != Ack::Normal)
        CHECK_FAILED;
      height += 60;
    }
    out = session.feed(window(180));
    if (out.event != Event::StillOnce || out.ack != Ack::Normal)
      CHECK_FAILED;
    out = session.feed(window(180));
    if (out.event != Event::ReachedEnd || !session.reachedEnd())
      CHECK_FAILED;
    QString error;
    const QImage stitched = session.finish(error);
    if (stitched.isNull() || stitched.height() != height)
      CHECK_FAILED;
  }
  // (b) Solid first frames never seed; the ninth halts.
  {
    AutoCapture session(Axis::Vertical);
    QImage blank(64, 200, QImage::Format_RGBA8888);
    blank.fill(Qt::darkGray);
    for (int i = 0; i < 7; ++i) {
      const AutoCapture::Outcome out = session.feed(blank);
      if (out.event != Event::Blank || out.ack != Ack::Hold)
        CHECK_FAILED;
    }
    const AutoCapture::Outcome out = session.feed(blank);
    if (out.event != Event::Halted ||
        out.haltReason != AutoCapture::HaltReason::BlankFrames)
      CHECK_FAILED;
  }
  // (c) Periodic content: probe with one notch, resolve, commit both frames.
  {
    AutoCapture session(Axis::Vertical);
    if (session.feed(grayToImage(exactlyPeriodicGray(0))).event != Event::Seeded)
      CHECK_FAILED;
    AutoCapture::Outcome out = session.feed(grayToImage(exactlyPeriodicGray(54)));
    if (out.event != Event::ProbeStarted || out.ack != Ack::Probe ||
        session.keptFrames() != 1)
      CHECK_FAILED; // F1 held, not committed
    out = session.feed(grayToImage(exactlyPeriodicGray(72)));
    if (out.event != Event::Committed || out.ack != Ack::Normal ||
        out.firstDelta != 54 || out.secondDelta != 18 ||
        session.keptFrames() != 3)
      CHECK_FAILED;
    QString error;
    const QImage stitched = session.finish(error);
    if (stitched.isNull() || stitched.height() != 360 + 54 + 18)
      CHECK_FAILED;
  }
  // (d) Probe, then two stationary probes: the end is confirmed at the seam
  // and only F1 is committed.
  {
    AutoCapture session(Axis::Vertical);
    if (session.feed(grayToImage(exactlyPeriodicGray(0))).event != Event::Seeded)
      CHECK_FAILED;
    if (session.feed(grayToImage(exactlyPeriodicGray(54))).event !=
        Event::ProbeStarted)
      CHECK_FAILED;
    AutoCapture::Outcome out = session.feed(grayToImage(exactlyPeriodicGray(54)));
    if (out.event != Event::ProbeAgain || out.ack != Ack::Probe)
      CHECK_FAILED;
    out = session.feed(grayToImage(exactlyPeriodicGray(54)));
    if (out.event != Event::ReachedEndAtSeam || out.ack != Ack::Hold ||
        session.keptFrames() != 2 || !session.reachedEnd())
      CHECK_FAILED;
    QString error;
    const QImage stitched = session.finish(error);
    if (stitched.isNull() || stitched.height() != 360 + out.firstDelta)
      CHECK_FAILED;
  }
  // (e) An unresolvable probe pauses holding the frames; abandoning keeps only
  // verified content.
  {
    AutoCapture session(Axis::Vertical);
    if (session.feed(grayToImage(exactlyPeriodicGray(0))).event != Event::Seeded)
      CHECK_FAILED;
    if (session.feed(grayToImage(exactlyPeriodicGray(54))).event !=
        Event::ProbeStarted)
      CHECK_FAILED;
    const AutoCapture::Outcome out =
        session.feed(grayToImage(uniqueDocumentGray(0)));
    if (out.event != Event::Paused || out.ack != Ack::Hold)
      CHECK_FAILED;
    session.abandonPause();
    QString error;
    const QImage stitched = session.finish(error);
    if (stitched.isNull() || stitched.height() != 360 ||
        session.unverifiedSeams() != 0)
      CHECK_FAILED;
  }
  return true;
}
} // namespace

/// A session that concluded because the screen went still can be picked up
/// again: the bands it has are kept and the next scroll appends to them.
bool checkAutoResume() {
  const QSize size(120, 90);
  auto frameAt = [size](int offset) {
    QImage frame(size, QImage::Format_RGBA8888);
    for (int y = 0; y < size.height(); ++y) {
      QRgb *row = reinterpret_cast<QRgb *>(frame.scanLine(y));
      for (int x = 0; x < size.width(); ++x) {
        const int document = y + offset;
        row[x] = qRgb((document * 7) & 0xff, (document * 13) & 0xff,
                      ((document ^ x) * 5) & 0xff);
      }
    }
    return frame;
  };
  stitch::AutoCapture session(stitch::Axis::Vertical);
  for (const int offset : {0, 20, 40}) {
    const stitch::AutoCapture::Outcome out = session.feed(frameAt(offset));
    static_cast<void>(out);
  }
  const int keptBefore = session.keptFrames();
  // The screen goes still, as it does when the pointer leaves the frame.
  for (int still = 0; still < 8; ++still)
    static_cast<void>(session.feed(frameAt(40)));
  if (!session.reachedEnd())
    return true; // never concluded; nothing to resume, and nothing to prove
  session.resumeFromEnd();
  if (session.reachedEnd())
    CHECK_FAILED;
  static_cast<void>(session.feed(frameAt(60)));
  return session.keptFrames() >= keptBefore;
}

bool runStitchChecks() {
  // Downsample: cross axis /4, motion axis exact; luma correct.
  const QImage texture = makeTexture(256, 180);
  const GrayView vertical = downsampleToGray(texture, Axis::Vertical);
  if (vertical.width != 64 || vertical.height != 180 || vertical.sourceWidth != 256)
    CHECK_FAILED;
  const GrayView horizontal = downsampleToGray(texture, Axis::Horizontal);
  if (horizontal.width != 256 || horizontal.height != 45)
    CHECK_FAILED;
  // cropAxis spans the requested range on either axis.
  const GrayView band = vertical.cropAxis(Axis::Vertical, 20, 80);
  if (band.height != 60 || band.width != 64)
    CHECK_FAILED;

  // scoreShift is bit-identical to the naive reference across axes, shifts and
  // sampling steps.
  const GrayView vp = downsampleToGray(makeTexture(53, 71), Axis::Vertical);
  const GrayView hp = downsampleToGray(makeTexture(53, 71), Axis::Horizontal);
  for (const auto &pair : {std::pair{Axis::Vertical, &vp}, std::pair{Axis::Horizontal, &hp}}) {
    const Axis axis = pair.first;
    const GrayView &view = *pair.second;
    const int axisLen = view.axisLen(axis);
    for (int maxShift : {3, 7, axisLen / 3}) {
      for (long shift = -maxShift; shift <= maxShift; ++shift)
        for (int aStep : {0, 1, 2, 5, 13})
          for (int cStep : {0, 1, 3, 8}) {
            const double fast = scoreShift(view, view, axis, shift, maxShift, aStep, cStep);
            const double slow = scoreShiftReference(view, view, axis, shift, maxShift, aStep, cStep);
            if (fast != slow && !(std::isinf(fast) && std::isinf(slow)))
              CHECK_FAILED;
          }
    }
  }

  // A stationary pair caps each scoring edge at axisLen/4 and classifies as
  // Stationary.
  {
    QImage still = makeTexture(64, 180);
    const GrayView g = downsampleToGray(still, Axis::Vertical);
    const StationaryEdges edges = stationaryScoringEdges(g, g, Axis::Vertical);
    if (edges.lead != 45 || edges.trail != 45)
      CHECK_FAILED;
    if (classifyMotion(g, g, Axis::Vertical).motion.kind != MotionKind::Stationary)
      CHECK_FAILED;
  }

  // A known vertical shift classifies as Forward(delta) and stitches to the
  // expected height with the deltas applied.
  {
    QImage tall = makeTexture(64, 400);
    QImage a = tall.copy(0, 0, 64, 200);
    QImage b = tall.copy(0, 40, 64, 200);   // shifted down 40 px
    QImage c = tall.copy(0, 90, 64, 200);   // a further 50 px
    const MotionEstimate est =
        classifyMotion(downsampleToGray(a, Axis::Vertical),
                       downsampleToGray(b, Axis::Vertical), Axis::Vertical);
    if (est.motion.kind != MotionKind::Forward || est.motion.delta != 40)
      CHECK_FAILED;
    QString error;
    const QImage stitched = stitchWithDeltas({a, b, c}, {40, 50}, Axis::Vertical, error);
    if (stitched.isNull() || stitched.width() != 64 || stitched.height() != 290)
      CHECK_FAILED;
  }

  // Reverse (scrolled-up) frames rebuild the document in order: windows that
  // each show content 40 px higher than the last stitch back to the span they
  // cover, top-to-bottom.
  {
    const QImage doc = makeTexture(64, 400);
    const QImage f0 = doc.copy(0, 200, 64, 160); // window low in the document
    const QImage f1 = doc.copy(0, 160, 64, 160); // scrolled up 40
    const QImage f2 = doc.copy(0, 120, 64, 160); // up another 40
    const MotionEstimate est =
        classifyMotion(downsampleToGray(f0, Axis::Vertical),
                       downsampleToGray(f1, Axis::Vertical), Axis::Vertical);
    if (est.motion.kind != MotionKind::Reverse || est.motion.delta != 40)
      CHECK_FAILED;
    QString error;
    bool ok = false;
    StitchAccumulator acc(f0, Axis::Vertical, ok, error);
    if (!ok || !acc.pushReverse(f1, 40, error) || !acc.pushReverse(f2, 40, error))
      CHECK_FAILED;
    // Forward and reverse cannot mix.
    if (acc.pushForward(f2, 40, error))
      CHECK_FAILED;
    const QImage rebuilt = acc.finish(error);
    // Spans doc rows [120, 360): 160 + 40 + 40 = 240 tall.
    const QImage expected = doc.copy(0, 120, 64, 240)
                                .convertToFormat(QImage::Format_RGBA8888);
    if (rebuilt.isNull() || rebuilt.size() != expected.size())
      CHECK_FAILED;
    if (rebuilt.convertToFormat(QImage::Format_RGBA8888) != expected)
      CHECK_FAILED;
  }
  // ---- ManualCapture: the live-loop semantics ------------------------------
  {
    using Event = ManualCapture::Event;
    // A 64x400 document viewed through a 64x200 window (coalesce threshold
    // for axisLen 200 = clamp(25,16,128) = 25).
    const QImage doc = makeTexture(64, 400);
    const auto window = [&](int top) { return doc.copy(0, top, 64, 200); };

    // (a) Ordinary forward scrolling in steps above the coalesce threshold
    // stitches the span; a solid first frame is refused; stationary grabs
    // are ignored.
    {
      ManualCapture session(Axis::Vertical);
      QImage blank(64, 200, QImage::Format_RGBA8888);
      blank.fill(Qt::black);
      if (session.feed(blank).event != Event::Blank || session.started())
        CHECK_FAILED;
      if (session.feed(window(0)).event != Event::Seeded)
        CHECK_FAILED;
      if (session.feed(window(0)).event != Event::Still)
        CHECK_FAILED;
      if (session.feed(window(60)).event != Event::Kept)
        CHECK_FAILED;
      if (session.feed(window(60)).event != Event::Still)
        CHECK_FAILED;
      if (session.feed(window(150)).event != Event::Kept)
        CHECK_FAILED;
      QString error;
      const QImage out = session.finish(error);
      const QImage expected = doc.copy(0, 0, 64, 350).convertToFormat(QImage::Format_RGBA8888);
      if (out.isNull() || out.convertToFormat(QImage::Format_RGBA8888) != expected)
        CHECK_FAILED;
    }
    // (b) Small movements are held as one pending frame, replaced as they
    // grow, dropped when reversed, and committed at finish.
    {
      ManualCapture session(Axis::Vertical);
      if (session.feed(window(0)).event != Event::Seeded)
        CHECK_FAILED;
      ManualCapture::Outcome out = session.feed(window(10));
      if (out.event != Event::Pending || out.pendingDelta != 10)
        CHECK_FAILED;
      out = session.feed(window(20));
      if (out.event != Event::Pending || out.pendingDelta != 20 || out.keptFrames != 1)
        CHECK_FAILED;
      // Back to the start: the pending movement is dropped.
      if (session.feed(window(0)).event != Event::PendingDropped)
        CHECK_FAILED;
      // Grow past the threshold: committed as one band.
      if (session.feed(window(12)).event != Event::Pending)
        CHECK_FAILED;
      out = session.feed(window(40));
      if (out.event != Event::Kept || out.keptFrames != 2)
        CHECK_FAILED;
      // A trailing small movement is committed by finish().
      if (session.feed(window(52)).event != Event::Pending)
        CHECK_FAILED;
      QString error;
      const QImage outImage = session.finish(error);
      const QImage expected = doc.copy(0, 0, 64, 252).convertToFormat(QImage::Format_RGBA8888);
      if (outImage.isNull() || outImage.convertToFormat(QImage::Format_RGBA8888) != expected)
        CHECK_FAILED;
    }
    // (c) Content that changes in place before anything is committed (an
    // autoplaying video) re-seeds the first frame instead of jamming the
    // capture; scrolling then proceeds from the new appearance.
    {
      ManualCapture session(Axis::Vertical);
      if (session.feed(window(0)).event != Event::Seeded)
        CHECK_FAILED;
      QImage changed = window(0);
      {
        QPainter painter(&changed); // a "video" area repainted in place
        painter.fillRect(QRect(0, 60, 64, 100), QColor(200, 30, 30));
      }
      // First sight of the change: unmatched vs the reference, and it moved
      // vs the previous grab, so not yet at rest → keep the reference.
      if (session.feed(changed).event != Event::Unmatchable)
        CHECK_FAILED;
      // Same changed frame again: at rest and nothing committed → re-seed.
      if (session.feed(changed).event != Event::ReSeeded)
        CHECK_FAILED;
      // Now scrolling from the changed appearance works.
      QImage changedScrolled = window(60);
      {
        QPainter painter(&changedScrolled);
        painter.fillRect(QRect(0, 0, 64, 100), QColor(200, 30, 30));
      }
      if (session.feed(changedScrolled).event != Event::Kept)
        CHECK_FAILED;
    }
    // (d) A jump beyond the safe overlap cannot be aligned; the reference is
    // kept, and scrolling back into overlap resumes the capture without a gap.
    {
      ManualCapture session(Axis::Vertical);
      if (session.feed(window(0)).event != Event::Seeded)
        CHECK_FAILED;
      if (session.feed(window(60)).event != Event::Kept)
        CHECK_FAILED;
      // 200-px window: safe max shift is 200 - max(200/3, 32) = 133, so this
      // frame cannot be aligned to the reference (on this synthetic texture
      // it may surface an aliasing peak the wrong way instead; either way it
      // must be refused without changing state).
      const auto refused = [](Event event) {
        return event == Event::Unmatchable || event == Event::WrongDirection;
      };
      ManualCapture::Outcome jump = session.feed(window(60 + 180));
      if (!refused(jump.event) || jump.keptFrames != 2)
        CHECK_FAILED;
      // Once a band exists, a repeat never re-seeds (that could hide a gap).
      jump = session.feed(window(60 + 180));
      if (!refused(jump.event) || jump.keptFrames != 2)
        CHECK_FAILED;
      // Scroll back into overlap: resumes against the kept reference.
      ManualCapture::Outcome out = session.feed(window(60 + 100));
      if (out.event != Event::Kept || out.estimate.motion.delta != 100)
        CHECK_FAILED;
      QString error;
      const QImage outImage = session.finish(error);
      const QImage expected = doc.copy(0, 0, 64, 360).convertToFormat(QImage::Format_RGBA8888);
      if (outImage.isNull() || outImage.convertToFormat(QImage::Format_RGBA8888) != expected)
        CHECK_FAILED;
    }
    // (e) Direction locks once a band exists; before that it may switch.
    {
      ManualCapture session(Axis::Vertical);
      if (session.feed(window(200)).event != Event::Seeded)
        CHECK_FAILED;
      if (session.feed(window(190)).event != Event::Pending) // small reverse
        CHECK_FAILED;
      if (session.feed(window(260)).event != Event::Kept) // switched forward, ok
        CHECK_FAILED;
      if (session.feed(window(200)).event != Event::WrongDirection)
        CHECK_FAILED;
      if (session.feed(window(320)).event != Event::Kept)
        CHECK_FAILED;
    }
  }
  // The capture budget: 200 MB of RGBA and an edge short of 32767, so the
  // result opens in other apps. A 1600-wide capture stops at 32000 rows, a
  // 2560-wide one at about 20000. The predicate is checked directly because
  // tripping it for real would need hundreds of megabytes of frames.
  if (exceedsStitchBudget(1600, 31000) || !exceedsStitchBudget(1600, 32001) ||
      exceedsStitchBudget(2560, 20000) || !exceedsStitchBudget(2560, 21000)) {
    std::fprintf(stderr, "stitch budget did not bound a tall capture\n");
    CHECK_FAILED;
  }
  if (exceedsStitchBudget(0, 1) || exceedsStitchBudget(1, 0) ||
      exceedsStitchBudget(2560, 1)) {
    std::fprintf(stderr, "stitch budget rejected an ordinary capture\n");
    CHECK_FAILED;
  }
  if (kMaxStitchedEdge >= kWidelyOpenableEdge) {
    std::fprintf(stderr, "captures can outgrow what other apps open\n");
    CHECK_FAILED;
  }

  if (!runLookaheadChecks())
    CHECK_FAILED;
  if (!runAutoCaptureChecks())
    CHECK_FAILED;

  if (!checkAutoResume())
    CHECK_FAILED;
  return true;
}
