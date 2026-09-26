# Omarchy capture app: product and implementation plan

Date: 2026-09-25

Status: planning archive and roadmap. Omaframe now has a working local preview; see README.md and the validation documents for implemented behavior. Roadmap features and performance targets below are not claims about the current build.

## Current priority: small capture interface

Owner feedback on 2026-09-25 prioritizes instant capture and explicitly rejects feature bloat. The earlier broad studio roadmap below is a backlog, not a requirement to expose all those features in the core interface.

The next capture-bar proposal is Screenshot / Video, target selection, and video-only system-audio and microphone controls. Screenshot remains the Print Screen default. The video path should supervise the existing recorder and open the finished file for trimming, without taking the user through two region pickers. Preserve Alt+Print as a direct video shortcut. A mode switch alone is incomplete without reliable stop, finalization, and editor handoff.

Countdown and cursor visibility belong in a small More menu. Webcam is secondary. Scrolling capture is useful as a separate screenshot mode, initially focused on browser pages, after the normal screenshot and recording paths are dependable. It must handle fixed headers, repeated content, lazy loading, and manual stop before it is described as reliable. OCR and pinning are contextual result actions, not permanent capture-bar toggles. Timelines, captions, camera layers, and other advanced editing remain deferred.

Startup must not initialize the studio, render samples, or initialize video playback just to select a screenshot region. Detailed startup measurements are in capture-workflow-validation.md.

## 1. Product decision

Build a standalone capture studio for Omarchy and plain Arch Linux with Hyprland. Keep Omaroll as the file viewer and media library. Reuse suitable code and algorithms without adding a capture interface to Omaroll. The owner's latest direction selects a coherent new product that combines the strongest reviewed ideas; competitor evaluation now informs implementation and quality targets instead of repeatedly reopening whether to build.

The product promise is: take a screenshot or recording, get a finished result with almost no decisions, and have familiar editing tools available when needed.

The ambition is category-leading quality across capture, screenshot presentation, screen-recording production, and sharing. This is not a measured claim of superiority. The release must earn it through observed workflow speed, visual comparisons, dependable hardware behavior, and completed advanced tools. A useful screenshot preview or recording beta is an intermediate release, not completion of that ambition.

The primary screenshot flow is capture, choose a finished look, paste. Accepting a screenshot automatically copies the rendered PNG and saves the same result. The owner explicitly selected this default on 2026-09-25.

Video has a different default: record, review/trim, share. It opens unframed and does not inherit the screenshot's preferred matte. The 2026-09-25 follow-up explicitly asks for meaningful recording improvements beyond borders. Optional styling remains available without becoming a required step.

The app is local first, intended for personal use and public community distribution. No account, trial, payment, watermark, hosted sharing, or telemetry is required. Upstream adoption by Omarchy is not a prerequisite.

The final product name is intentionally undecided. `capture-app` in this document is a placeholder, not a proposed package name. Check repository, package, binary, and existing-product collisions before naming the repository.

## 2. What the existing projects establish

Local source snapshots reviewed:

| Project | Snapshot | Useful material | Boundary |
|---|---|---|---|
| MatteShot | `d4bfd6424f91f80c8d51e0097373591059a31b76` | Capture-to-picker flow; color-derived styles; composition; non-destructive editing; source-time video speed model; export recovery practices | Win32, WGC, Media Foundation, audio, clipboard, and native interface code need Linux replacements |
| Omaroll | `7715311f1d8908df20650c81eb175417b6a49452` | Qt/QML conventions; six-family matte picker; hue extraction; aspect framing | It is a media library. Its matte renderer and synchronous save path need review and improvement before reuse |
| Omasnap | `acfb3b5772ccb041b57ccc76e16f1b72719f37e9` | Wayland capture; geometry; PNG output; annotation/cut model; scrolling capture algorithms and tests | Capture and editor code are coupled. It is not an existing stable library API |

Omasnap already supports gradients, shadows, rounding, and a configured default background on fresh captures. The earlier suggestion that styling necessarily requires opening its editor was too strong. The intended distinction must be demonstrated through finished visual quality, the picker interaction, and a consistent video workflow.

The Omasnap and Omaroll checkouts contain MIT license notices. Any adapted files retain their original notices and a source revision in `THIRD_PARTY.md`. Review MatteShot's distribution rights and bundled assets before moving code into a publicly licensed repository. This plan does not assume every asset or dependency has the same license.

Evidence:

- [MatteShot styles](../matteshot/src/style.rs), [composition](../matteshot/src/compose.rs), and [video time mapping](../matteshot/src/video_speed.rs).
- [Omaroll picker](../omaroll/qml/components/MatteSheet.qml), [renderer](../omaroll/src/matte/MatteComposer.cpp), and [hue extraction](../omaroll/src/matte/HueExtractor.cpp).
- [Omasnap capture implementation](../omasnap/src/surface-capture.cpp), [editing model](../omasnap/docs/editing-model.md), and [dependencies](../omasnap/docs/dependencies.md).

## 3. Existing-app benchmarks and reuse evaluation

The live search has found meaningful overlap. Cap, ApexShot, Omasnap, and the Omarchy Pretty Screenshot plugin supply concrete reference experiences. A search cannot prove that no matching product exists. Keep their best verified results as benchmarks rather than using feature counts as a proxy for quality.

Use the same fictional inputs and tasks for each candidate:

1. Capture a light application, a dark terminal, a small notification, and a tall page.
2. Produce an attractive default result without opening settings.
3. Choose a visibly different finished style, then copy and save.
4. Crop, add two numbered steps, redact a token, undo, and export.
5. Record a 30-second task, trim the ends, remove an internal pause, change the framing, and export.
6. Reopen an existing file and repeat the same treatment.
7. Check cancellation, keyboard operation, clipboard persistence after closing, and a failed save.

Record required clicks and dialogs, output quality, available Linux features, local/offline behavior, and observed bugs. Keep screenshots and exported artifacts beside the scorecard. A feature mentioned on a website counts as documented, not passed.

Use Cap and ApexShot for the broad comparison, Omasnap and Pretty Screenshot as the baseline for minimal-effort captures, and Gradia as a reference for screenshot editing. Examine Omascreen if the video editor is the remaining gap. Do not turn this into a review of every Linux media tool.

Reuse rule: inspect the relevant implementation and license before extracting a component. Prefer a small maintained dependency or bounded attributed module to recreating mature capture/encoding infrastructure. Build a single document model and interface around those capabilities. Do not assemble separate apps with different editors and call the result integrated. If an existing foundation proves materially better than the proposed stack, record that evidence before changing the architecture; the standalone product and simple flows remain the objective.

## 4. Core interactions

### Screenshot capture

1. The capture shortcut opens a frozen selection surface. Drag selects a region; clicking a highlighted window selects its visible area; clicking empty desktop space selects that monitor. The toolbar exposes Screen, Region, Window, Scroll, and Record without a separate launcher screen.
2. Once selected, a compact picker opens on that monitor. It shows a large finished preview, eight small finished alternatives, Raw, Edit, and a clearly labeled Copy and save action.
3. Hover or keyboard focus previews a look without producing output. Clicking a look accepts it immediately. The accessible label and hover text make this action explicit. Arrow keys or 1-8 select a look; Enter accepts it. Raw remains available as an explicit action.
4. The app renders once, saves the PNG, offers the same bytes to the clipboard, closes the picker after success, and displays a small confirmation with Open and Edit actions. It restores focus to the previous application.

The first-run default is Auto selection of an existing style. The style order remains stable; the recommended badge can change. Once the user picks a favorite, subsequent captures preselect that family while still deriving its colors from the new content. A small preference can restore Auto. A separate quick command skips the picker and accepts the preferred style immediately.

Do not copy the raw screenshot to the clipboard before acceptance. Escape from the selection or picker cancels the new capture and leaves the previous clipboard unchanged. Escape within an armed editor tool cancels that tool before closing any surface.

Window capture initially means the visible window region from the output frame, with overlapping content honestly retained. Do not promise an unobscured window capture unless a separate surface-capture path has been implemented and tested. Cross-monitor selection is deliberately deferred for the first usable build; capturing on any selected monitor and mixed-DPI correctness are required before public release.

### Image editor

Edit expands the current document into a normal resizable window. Keep a large canvas, a compact tool row, the style strip, and one primary Copy and save action. Select, Crop, Arrow, Text, Steps, Highlight, and Redact are visible. Shapes, pen, OCR, rotate, resize, and basic adjustments remain one labeled menu or panel away.

Selecting a style here changes the document without committing output. Tools stay armed for repeated use. Objects remain selectable, movable, and resizable. Text is edited in place. Undo and redo cover both appearance and edits. Crop is reversible and does not invalidate source-coordinate annotations.

Closing an editor keeps a bounded local draft rather than forcing a Save As flow. Recapturing opens a new document and preserves the current draft. The app supports multiple open documents without building a file library; a compact tab strip appears only when necessary.

### Recording and quick video edit

The finished app supplies both the recording interface and the review/editor interface. It supervises the existing GPU Screen Recorder engine instead of implementing a new encoder. Opening videos recorded through Omarchy remains supported. The first video milestone can begin with opening existing files; direct recording is added only after backend and stop-control integration are verified.

1. Record uses the same target selection. A compact preflight bar shows the target, independent microphone/system-audio/camera toggles, selected devices, live audio meters, and Start. Audio and camera default to off on first use and thereafter remember explicit choices; enabled sources are always visible before starting. Mic-only narration must not require desktop audio. Add a remembered optional three-second countdown.
2. While recording, show elapsed time, Pause, Stop, and the enabled audio sources in a compact control. If that control cannot be excluded reliably from the selected target, position it outside the region or offer keyboard-only control. Never silently record the app's own interface. A missing selected source is a visible state, not an automatic switch to a different microphone.
3. Stop finalizes the recording, then opens a plain video preview. Play/pause, trim handles, and a primary Save/share action are immediately available. Frame styling is behind an optional Appearance control. An unchanged compatible clip uses the finalized recording directly; do not re-encode it or require an export dialog solely because the review window opened.
4. A labeled Edit action expands the timeline for removing internal sections, changing speed, and attaching annotations to time ranges. The first recording beta handles one source clip. The complete studio also supports adding/reordering clips and separate camera, voice, caption, and music layers. These controls appear when their corresponding feature is used.
5. Export renders the selected edit, preserves the original, and saves a shareable file. Copy offers the exported file URI where supported; the app does not imply that arbitrary applications can paste video as image data. Dragging the finished file and Open folder are reliable alternatives.

If the user enables a video style, it uses a stable palette derived from a representative early frame. It does not change colors frame by frame. The user can explicitly choose a different representative frame under Adjust. Video appearance is remembered separately from screenshot appearance. Preview, trim boundaries, audio, annotations, and export use the same source-to-output time mapping.

### Existing files and recent work

Open image, paste image, and Open video enter the same picker/editor used after capture. Recent work is a small recovery list, not albums, tagging, search, or another Omaroll. Saved output uses ordinary files so Omaroll can discover them through its normal folder watching. An optional Open in Omaroll action requires no changes to its source.

### One interface, progressively expanded

Use three surfaces: the capture selector, the quick result, and the editor. There is no mandatory choice between a basic product and a professional product before recording. The quick result exposes the actions needed most often; Edit expands that same document and preserves its selection, playback position, and undo history.

Image tools and video tools share labels, icons, selection behavior, crop handles, style controls, export sizing, and recovery behavior. Media-specific controls appear only when useful. A video timeline starts compact. Camera, captions, music, and focus/zoom tracks appear only after they are enabled or added. The app never displays an empty collection of professional tracks merely to advertise its features.

Provide a searchable action menu and discoverable tooltips for less common actions. Good defaults remain usable without memorizing shortcuts. Keyboard access is complete for capture, choosing looks, common edits, playback, and output.

## 5. Visual specification

Eight complete treatments, plus Raw:

| Look | Intended treatment |
|---|---|
| Paper | Warm near-white, restrained lifted shadow, generous but bounded spacing |
| Slate | Neutral charcoal, subtle edge separation, compact shadow |
| Deep | Dark content-derived color, muted saturation, soft depth |
| Adaptive | Restrained gradient from the captured content, automatic edge contrast |
| Aurora | Soft mesh color with enough quiet space around the screenshot |
| Outline | Slim neutral frame, small spacing, almost no shadow for documentation |
| Studio | Neutral light or dark floating card, balanced spacing and a refined dual shadow |
| Ambient | Heavily softened, darkened or lightened content-derived backdrop with a quiet center |

Names and exact numeric tokens may change during visual acceptance. The requirement is eight clearly differentiated, consistently good compositions. If only six meet that standard initially, ship six rather than padding the catalog with weak variations.

Each style defines its palette rules, padding, corner treatment, hairline edge, shadow, and optional subtle texture together. Advanced controls modify the composition without forcing ordinary users to understand those variables.

Initial renderer rules:

- Preserve original content pixels at the default screenshot output size. Add the frame around them. Resizing only happens through an explicit output-size choice or a clearly reported safety limit.
- Begin padding experiments at 6% of the shorter content edge, bounded to 16-96 logical pixels. Use logical-to-native scale consistently. This is a design starting point to tune against fixtures, not a final promise.
- Bound corners so tiny captures and text at the edges are not clipped. Prefer a very small or square corner when needed. Never add a fake operating-system titlebar by default.
- Limit saturation and background contrast so the screenshot remains the subject. Color sampling considers the visible crop and transparent pixels; a grayscale input gets a deliberate neutral fallback.
- Sample Ambient and Adaptive only from the sanitized visible image after redaction. A blurred background must never reintroduce content removed from the foreground.
- Default to the source aspect plus natural padding. Optional 1:1, 4:3, 16:9, 9:16, and social-wide canvases expand around the content; they do not crop it without an explicit crop operation.
- UI theme changes affect controls, not the colors of existing export documents. Frozen style parameters make reopened exports repeatable.
- Use real shadow falloff and antialiased edges; validate at actual export size, not only thumbnail size. Avoid halos, heavy gray borders, gradient banding, and visibly clipped shadows.
- Still-image imports respect orientation and embedded color profiles; convert compositing into a declared sRGB working space. v1 supports SDR export. HDR correctness is a separate capability gate, with an explicit conversion or unsupported message rather than washed-out output.

The picker should fit a 1366x768 logical desktop without hidden primary actions. At narrower widths, wrap or scroll the style strip while preserving the canvas and acceptance button. Avoid giant modal margins. Use clear typography, consistent icon geometry, strong focus indicators, accessible names, and reduced-motion support. Follow Omarchy colors where readable, with safe light/dark fallbacks.

Visual acceptance uses at least 12 inputs: dark terminal, light document, colorful dashboard, code editor, browser window, photo, tiny tooltip, transparent asset, wide banner, portrait panel, long scrolling capture, and densely annotated screenshot. Review every shipped style against every input. Keep exported contact sheets and full-resolution samples. The owner must accept the final treatments before calling them premium or freezing the catalog.

## 6. Feature scope and complete studio target

| Area | First daily-use build or recording beta | Complete studio 1.0 |
|---|---|---|
| Capture | Region, visible window, monitor, cancel, re-snipping | Multiple monitors, fractional scale, scrolling capture, delayed capture, supported independent window capture, repeat-last-region |
| Presentation | Six to eight reviewed looks, Raw, preferred style, quick accept | All accepted looks, aspect and explicit size presets, existing-file input |
| Image editing | Crop, arrow, text, steps, highlight, secure redact, undo/redo | Lines, rectangle/ellipse, pen, spotlight/magnifier, rotate/flip, resize, OCR text selection, QR recognition, basic brightness/contrast/saturation, image band cut, pin-on-screen reference |
| Clipboard/save | Atomic PNG save and persistent clipboard ownership | Save As, drag actual output file, reopen draft, recover failures |
| Recording | Region/monitor, countdown, pause/stop, independent audio selection/meters, basic camera | Separately editable screen/camera/audio, tested window capture where available, explicit device-loss handling, recovery, remembered capture setup |
| Video edit | Trim, internal cuts, speed, crop, timed annotations/redaction, mute/volume, still-frame export | Add/join/reorder clips; simple transitions; editable focus/zoom; cursor emphasis; camera layout; voice-over; local captions and transcript-assisted cuts; silence suggestions; music, ducking and optional voice cleanup |
| Video output | Plain H.264 MP4 with AAC where audio exists; progress and cancellation | Optional framing, GIF/WebM, caption burn-in or SRT, reusable export presets, bounded export queue, cancel/retry and validated output |
| Integration | Separate shortcut in the isolated environment | Optional default shortcut, launcher entry, open-with actions, package/uninstall, Omaroll folder discovery |

The complete studio remains focused on screen communication. Full photo retouching, RAW development, a general film-editing suite, live streaming, always-on replay recording, team/cloud hosting, and automatic tracking of redacted objects are outside this release. Captions, focus/zoom, cursor emphasis, separate webcam layout, voice cleanup, and simple clip assembly are now committed stages of the full product, not indefinitely deferred extras. Basic camera-overlay parity is required before presenting an early recording build as a replacement for the user's current menu.

The minimum supported capture environments are Omarchy and plain Arch/Hyprland, with editing/export usable without Omarchy services. KDE/GNOME and other Wayland desktops require a separate portal/backend acceptance matrix; they are not implied by the word Arch. Keep backend interfaces capable of future expansion without delaying Hyprland quality for untested cross-desktop support.

Recording a moving window independently of other windows is a capability to verify early. The required initial target is a fixed screen region or monitor. A window selection may initialize that region, but the UI must label it as a region if it will not follow the window.

## 7. Technical architecture

### Preferred stack

For a new build, use C++20, Qt 6.8-compatible APIs, Qt Quick/QML for the interface, CMake, and Qt Test. Use LayerShellQt and Wayland protocols for selection surfaces. This fits both existing Linux codebases and avoids maintaining a Rust/C++ bridge only to share a small style algorithm.

Separate baseline desktop support from Omarchy integration. File locations come from XDG, theme defaults work without Omarchy palettes, notifications use standard desktop facilities with an optional Omarchy adapter, and recording/capture do not depend on Omarchy shell scripts. Test the packaged app on a plain Arch/Hyprland session as well as Omarchy.

Create a new repository. Start with adapted, attributed modules from pinned source snapshots rather than a continuously rebased fork of Omasnap. Its editor is approximately 8,000 lines and capture/render unit approximately 2,700 lines at the reviewed revision; extraction is real engineering work. Reuse the smaller capture/protocol pieces and pure algorithms first, not the whole interface.

Keep the editor a standalone application process. A later shell plugin may expose a button or recording status, but it must not host the media editor or export workload inside the desktop shell.

### Components

| Component | Responsibility |
|---|---|
| App controller | Single instance, activation requests, capture/editor/record/export states, focus restoration |
| Capture service | Monitor discovery, physical/logical transforms, frozen images, window targets, scrolling acquisition |
| Document model | Immutable source reference, crop, ordered edits, style, output intent, undo/redo, serialization |
| Layout/style engine | Content analysis, deterministic style parameters, coordinate transforms, final canvas geometry |
| Renderer | Background and mask assets, redacted content, annotations, previews, flattened output |
| Clipboard/output service | Encode once, atomic save, clipboard lifetime, drag URI, failure state |
| Recording service | Owned recorder subprocess, source/audio selection, timestamps, finalize/recovery |
| Timeline model | Clip assembly, kept source ranges, rational speed, timed overlays/captions, focus/camera transforms, audio settings, source/output mapping |
| Export worker | FFmpeg job construction, progress, cancellation, validation, final rename |
| Draft store | Bounded screenshot recovery, video edit metadata, schema versions and migration policy |
| Platform integration | XDG/Hyprland baseline, optional Omarchy theme/menu/bar adapters, launch/open-with, separately installed shortcuts and notifications |
| Recording event data | Source-timestamped pointer position/clicks and camera/audio stream identity, captured only during app-owned recording |
| Assisted editing workers | Optional local transcription, silence analysis, zoom suggestions and voice cleanup with explicit model/dependency setup |

Suggested source layout:

```text
src/app/          src/capture/       src/document/
src/render/       src/output/        src/recording/
src/video/        src/platform/      qml/
resources/styles/ tests/fixtures/    packaging/
docs/             THIRD_PARTY.md
```

### Capture and OS integration

Adapt Omasnap's native Wayland output capture and measured geometry handling behind a small internal interface. Keep protocol and compositor-specific code separate from the document and style engine. Runtime capability checks explain unavailable capture modes.

Do not shell out to Omasnap and infer the newest screenshot filename. Its CLI is not a documented structured handoff interface. If a temporary prototype uses an external capture tool, it must supply a unique destination or explicit bytes and verify that exact result.

Use `hyprctl` JSON for bounded read-only monitor/window discovery where necessary. Avoid compositor config mutations in the capture path. Do not change cursor configuration globally to make selection work.

Own a local IPC endpoint for subsequent command invocations. Only one selection surface can be active. Repeated capture shortcuts dismiss or restart that surface predictably; a new capture does not destroy an open edit. Use explicit states for Idle, Selecting, Picking, Editing, Recording, Finalizing, and Exporting, with document/export identity kept separate from UI visibility.

### Rendering and editing

The canonical document records edits in source coordinates and keeps source pixels immutable. Resolve crop, rotation, scaling, style layout, annotations, and output dimensions through one shared transform implementation.

Use bounded worker jobs for sampling, preview generation, encoding, OCR, and saving. Prioritize the chosen preview before the eight thumbnails. Cache by source identity, document revision, style version, and target dimensions. Discard stale completed jobs after newer edits. Do not hold eight full-resolution rendered variants in memory.

Reimplement or adapt MatteShot's refined compositing in the Qt renderer, using Omaroll's hue/picker code as a starting point. Omaroll currently draws the source image directly and approximates its shadow with expanded rounded rectangles; that is not an accepted visual specification for the new app. Its existing full-size encode/save also runs synchronously and must not be copied into the UI path.

Redaction is a destructive pixel operation in every flattened preview/export. Solid redaction is the default. Ordinary blur is labeled a visual effect and is not represented as secure redaction. OCR and derived backgrounds use the sanitized image. Shareable exports never embed raw source images or editable recovery state.

### Video recording and export

Use the installed `gpu-screen-recorder` as a supervised child process where the target capabilities are supported. Verify its actual installed version and flags in the recording spike. Qt Multimedia is the initial playback candidate. FFmpeg/ffprobe perform probing, edit rendering, encoding, and validation. These are proposed integrations, not tested behavior of the new app.

Give every recording a unique output path and owned process handle. Stop or pause only that process through supported IPC/signals. Never use a global `pkill` or modify the user's existing Omarchy recorder override. Do not normalize source audio silently. Capture system and microphone separately when the chosen backend supports it, with an explicit fallback if it does not.

Separate source audio stays editable in the project; the shareable MP4 mixes enabled tracks into a normal, widely playable output. Do not export two alternative player-selected tracks and imply both will play together. Preserve the user's explicitly selected `clean_desktop_microphone` source when available. Do not silently fall back to a headset microphone if it disappears.

Preserve the working camera format-negotiation behavior from the installed local script. Basic webcam support may be baked into the first recording beta; the complete studio captures a separate synchronized camera stream for later layout changes. Validate both live preview and encoded output frame rate instead of trusting the requested rate.

Recording readiness requires confirmed frames and selected-source readiness, not merely a process and an output file. Investigate warming the source before the visible countdown/recording start to remove capture-open transients. Do not silently discard a fixed interval of the user's spoken introduction as the product's final solution.

Stock Omarchy controls require particular care: the current bar detects any process matching `gpu-screen-recorder`, and stock stop handling sends signals by process name. Merely owning our own child PID does not prevent the stock bar or shortcut from stopping it incorrectly. Direct recording therefore needs a user-level routing integration for the recording menu, shortcut, and bar indicator: app-owned sessions route to this app, stock sessions route to the existing script. Keep simultaneous recording unsupported initially and never claim coexistence until these stop paths pass tests. Use user-owned menu/key configuration and, if necessary, a cloned indicator plugin; do not edit packaged shell files.

Record to a recoverable private working container, then finalize a usable original. Prefer a crash-tolerant recording container such as MKV when supported, with MP4 conversion/export as a separate step. Do not merely rename container bytes to change the format.

The video renderer uses the same generated backdrop, shadow, corner mask, and annotation assets as the still renderer. Compile cut/speed/crop/redaction and compositing into a typed FFmpeg filter plan. Share the layout/time calculations with preview; different Qt and FFmpeg render paths require frame-parity tests, not an assumption that they match.

Start preview with Qt video playback plus shared overlay assets. The milestone 0/4 prototype must establish seek behavior and smooth playback across cuts and speed changes. If that path visibly stalls, use a bounded preview proxy or an FFmpeg-backed frame scheduler before declaring the editor usable. Do not promise seamless playback across arbitrary edits from an untested MediaPlayer sequence.

Preserve video source timestamps, handle variable frame rate, and map audio through the same kept segments and speed changes. Default speed adjustments preserve pitch where supported. Annotation intervals are attached to source time so cuts and speed changes cannot make them drift. Include first/last kept frames and transitions in validation.

Default export is SDR H.264 MP4 with compatible pixel format and AAC audio where present. Offer Original and 1080p/720p sizes, preserve aspect, and never upscale by default. Framing adds canvas area, so label the final dimensions and apply encoder limits before allocation. A project that cannot preserve original-size framing must present the actual output choice rather than silently shrink content. GIF export clearly has no audio.

Keep the unchanged-recording path fast and lossless. If a clip needs conversion for compatibility, show that work as conversion. Frame-accurate trimming and visual changes may require re-encoding; use stream-copy cuts only when their boundary behavior matches what the interface promises. A simple smaller-file preset is useful, but the default must not silently reduce readable screen text.

Use hardware encoding when validated for the installed GPU. Maintain a software encoding fallback and report it only when the user needs to understand a material slowdown or failure. Pass process arguments as structured arrays; generate filter inputs through typed helpers and temporary files, never shell-interpolated user text.

Write exports to unique same-directory temporary files. Cancellation removes only the current app-owned incomplete output. Success requires a clean encoder exit, expected streams/duration, and decode checks at critical boundaries before atomic rename. Full decode and A/V sync checks run in the automated fixture suite. Keep originals and recoverable work after crashes or inconclusive validation errors.

Technical references: [Qt VideoOutput](https://doc.qt.io/qt-6/qml-qtmultimedia-videooutput.html), [FFmpeg filter documentation](https://ffmpeg.org/ffmpeg-filters.html), [GPU Screen Recorder documentation](https://git.dec05eba.com/gpu-screen-recorder/about/). Probe backend capabilities rather than inferring them from a global product feature list.

## 8. Files, clipboard, and recovery

Suggested destinations respect XDG user directories: final screenshots under `Pictures/Screenshots`; final video exports under `Videos/CaptureApp`; recorded originals in its clearly named `Originals` subdirectory. The placeholder video folder is renamed when the product name is chosen. Preserve existing environment-based capture directory overrides deliberately and document precedence.

Filenames use timestamps and collision-safe suffixes. Do not include captured window titles by default. Imports are never overwritten. Save As requires the ordinary explicit replacement decision when an existing destination is selected.

Screenshot source pixels and edit logs are private drafts, not extra public PNGs. Initial retention proposal: at most 20 recent screenshot drafts, seven days, and a 1 GiB storage ceiling, with Clear recent work available in the app. Exclude open documents from pruning and report when the limit prevents retaining more drafts. Video originals are user files and are never automatically pruned. A canceled picker discards its unaccepted source; closing an editor retains the draft under the stated policy.

Use private XDG state/runtime directories, atomic state files, and bounded schema validation. On recovery, identify the draft and source clearly. Unsupported newer schemas open read-only or show a specific error rather than discarding work.

Save and clipboard operations are independently truthful:

- Both succeed: close and show Copied and saved.
- Save succeeds, clipboard fails: retain the saved result, show Saved with Retry copy.
- Save fails: keep the document and show the destination problem; allow another destination or an explicitly labeled copy-only action.
- Clipboard ownership must survive the picker/editor closing. `wl-copy` is a possible v1 helper, but process success alone is not end-to-end paste evidence. Verify browser and native-app pasting in isolation.

Final PNGs retain required color information but omit unnecessary imported personal metadata. No clipboard or folder watcher uploads anything. No auto-update service is needed for the initial Arch package.

## 9. Performance and reliability targets

These are initial release targets to measure on a named reference machine with fixed fixture dimensions. Record cold and warm runs separately and adjust targets only with evidence.

| Measurement | Initial target |
|---|---|
| Warm shortcut to usable selection | p95 at or below 250 ms for a 4K monitor |
| Cold shortcut to usable selection | p95 at or below 750 ms |
| Region accepted to selected styled preview | p95 at or below 200 ms for an ordinary 4K-or-smaller capture |
| All preset thumbnails populated | p95 at or below 500 ms |
| Cached style switch | within 50 ms |
| Accept to copy/save completion | p95 at or below 500 ms for a typical 1440p capture on local storage |
| Editing/recording interface | responsive input, no synchronous encode/probe/I/O on the UI thread |
| Typical 1080p preview | smooth 30 fps minimum; 60 fps where source/hardware support it |
| Export progress and cancel | progress at least once per second; cancel acknowledged within one second |
| Idle footprint | no capture polling, folder crawl, or continuously repainting hidden window |

Start with at most two expensive image jobs, one export job, and a bounded preview cache around 128 MiB, excluding currently required source buffers. Measure actual peak RSS and GPU memory. Serialize large work when needed. Long scroll captures need dimension/byte overflow checks and explicit capacity handling; a huge forced-aspect canvas must never become an unbounded allocation. Set final pixel limits from measurements and expose any required size reduction honestly.

## 10. Test and acceptance plan

Use the omabox skill for all GUI, capture, clipboard, shortcut, notification, tray, and desktop-session tests. The isolated desktop proves interaction and much of the capture flow; it does not prove physical microphone/system audio, real multi-monitor hardware, HDR, or GPU-specific recording behavior. Those require a later explicitly requested real-desktop check with the owner.

Automated evidence should cover:

- Pure geometry, crop/annotation transforms, style determinism, alpha edges, aspect constraints, no unintended upscaling, and allocation limits.
- Undo/redo, serialization/recovery, canceled capture, multiple open documents, stale async jobs, and single-instance activation.
- Source-pixel removal through redaction, OCR filtering, and sanitized background sampling.
- Atomic output, duplicate filenames, read-only/full/missing destinations, partial clipboard failure, and original preservation.
- Video source/output mapping, disjoint cuts, fractional rates, silent clips, variable frame rate, audio offsets, timed overlays, cancellation, and interrupted export recovery.
- Preview/export parity with tolerances appropriate to lossy codecs; exact pixel checks for deterministic PNG output where suitable.
- 1366x768, 1920x1080, and 4K layouts; light/dark and several Omarchy palettes; 100%, 125%, 150%, and 200% scale where the isolated compositor can represent them accurately.
- Keyboard-only capture/edit/output, accessible names/focus order, readable controls, and reduced motion. Screen-reader acceptance must be observed rather than inferred from accessible labels alone.

Do visual inspection of rendered application states and actual exported files, not only snapshot tests. For the final installed candidate, verify the launched binary and version, actual shortcut routing, clipboard paste after closing, file visibility in Omaroll, and rollback. Keep native hardware acceptance separate from headless or isolated test results.

## 11. Ordered implementation milestones

| Milestone | Deliverable | Exit condition |
|---|---|---|
| 0. Benchmarks and technical probes | Reference scorecard; capture geometry prototype; Qt/video render/export parity probe; backend and event-capture capability list | Written implementation/reuse choices; no unresolved architecture blocker hidden behind a polished mockup |
| 1. Visual prototype | Fixture-driven native picker and image renderer with six to eight looks | Owner accepts actual exported treatments across the fixture matrix; ordinary use requires no tuning |
| 2. Daily screenshot flow | Shortcut, selection, picker, copy/save, quick style, cancellation, imported image | Real isolated capture-to-paste works; no raw clipboard overwrite before acceptance; performance and failure behavior measured |
| 3. Complete image editor | Core annotations, crop/resize/rotate, redaction/OCR, undo/drafts, scroll and multi-monitor work | Daily-use screenshot release candidate passes image and interaction tests; physical monitor checks listed separately |
| 4. Recording and quick trim | Open existing Omarchy clips first; then recorder lifecycle, audio setup/meters, countdown, pause, basic camera, finalize, playback and trim; plain video default | Verified stop routing, no self-capture, successful recovery, accurate trim, preserved audio levels, A/V checks and camera parity before replacing the current menu |
| 5. Complete everyday video editing | Internal cuts, speed, timed annotation/redaction, crop, audio controls, GIF/WebM | Preview/export agreement and source-time behavior pass; unsupported combinations are not exposed as working controls |
| 6. Presentation tools | Manual and suggested focus/zoom, cursor emphasis, separately editable camera with simple layout presets | Natural motion, cursor/source alignment, synchronized camera, exact export agreement and one-click return to plain video |
| 7. Narration and assisted edits | Local editable captions/SRT, transcript-assisted cuts, silence suggestions, voice-over, optional voice cleanup, music/ducking | Every proposed edit is reviewable/undoable; clean original audio retained; timing and A/V behavior pass |
| 8. Assembly and workflow completion | Add/join/reorder clips, simple transitions, project reopen/relocate, output presets, bounded export queue, screenshot utility completion | Mixed-source clips and all intended screenshot tools pass; no independent editor or document model introduced |
| 9. Complete studio release | Arch and Omarchy packages, optional shortcut integration, uninstall/rollback, docs, comparative demos, third-party notices | All committed milestones pass; fresh install/upgrade and owner hardware acceptance complete; exact artifact and supported configurations recorded |

The screenshot preview and recording beta are useful intermediate deliverables, not completion of the requested studio. Do not mark the project finished until milestones 1-9 meet the agreed full scope or the owner explicitly changes it. Milestones 6-8 are committed work. Packaging and upgrade checks start with the first beta and continue throughout; milestone 9 is the final integrated acceptance, not the first installation test. Estimate elapsed time only after milestone 0 establishes reuse and backend behavior.

Suggested first implementation batches after planning:

1. Run the bounded reference comparison and record the strongest flows, visual samples, and implementation reuse choices.
2. Create the separately named repository; add the plan, attribution inventory, CMake/Qt baseline, fixtures, and omabox test instructions.
3. Implement a pure image document/layout renderer with deterministic export and a minimal QML picker over fixture files.
4. Tune and review the visual catalog before implementing the larger editor.
5. Connect native capture and transactional copy/save, then complete a usable screenshot slice.

## 12. Distribution, coexistence, and rollback

Start with an Arch package and a desktop entry, targeting both the current Omarchy/Hyprland combination and a documented plain Arch/Hyprland baseline. Omarchy adapters are optional. Use a separate application ID, binary, settings directory, and recovery store. Keep Omasnap installed.

Shortcut integration is optional and reversible. Development begins with a separate shortcut inside omabox. A later owner-requested installation can bind Print Screen to the new app with a clearly identified fallback to stock capture. Keep changes in user-owned configuration, preserve unrelated edits, and remove only app-owned integration during uninstall. Package installation itself must not silently take over Print Screen.

Do not patch `/usr/share/omarchy`, replace the system Omasnap executable, or alter existing recorder scripts. Omaroll integration is ordinary files and open-with actions. Sharing code back into Omaroll is a later maintenance improvement, not a dependency for shipping this app.

Prepare release notes, screenshots, build evidence, source/binary version, package dependencies, license notices, and rollback instructions before publication. Planning does not authorize commits, pushes, public repository creation, releases, package submissions, or changes on the real desktop. Future implementation should follow the user's requested scope and the repository's personal GitHub identity instructions.

## 13. Main risks and decisions still to validate

| Risk or unknown | Resolution |
|---|---|
| Existing apps already solve a feature well | Benchmark their actual behavior and reuse an appropriate component instead of rebuilding infrastructure for novelty |
| Attractive sample but poor general results | Full preset/input matrix and owner visual acceptance |
| Omasnap code reuse is more coupled than expected | Extract narrow protocol/algorithm modules; do not assume its widget editor can be embedded cheaply |
| QML playback diverges from FFmpeg export | Shared layout/time model, shared assets, frame comparisons, and preview strategy gate before full editor work |
| Recording target/audio behavior varies by driver/backend | Probe exact capabilities, label region versus tracked window honestly, complete hardware checks |
| Scope becomes a general media suite | Keep advanced tools tied to screen communication; use bounded clip/camera/audio layers and retain Omaroll's library responsibility |
| Huge captures or exports stress the desktop | Bounded workers/caches, dimension checks, cancellation, measured resource ceilings |
| Redacted source leaks through OCR or derived backgrounds | Sanitize before all derived image operations and test the actual exports |
| Silent failure after a click | Independent save/clipboard states with recoverable documents and truthful feedback |

Only the name, visual tuning, and evidence-dependent backend/reuse choices remain open. The core product boundaries and automatic copy-and-save behavior are specified. New findings should revise this document before implementation relies on an outdated assumption.

## 14. Competitive review and search coverage

An explicitly requested research sub-agent checked GitHub, Omarchy plugin listings, Reddit, and indexed X results. The lead also inspected local source and verified the key new Cap and Pretty Screenshot findings. No candidate was installed or interactively tested during this planning pass.

The idea already exists in substantial pieces. Cap is a serious combined competitor, and automatic framed screenshots already exist in Omarchy. We did not establish an exact match for the proposed live finished-look picker plus equally simple video editing. This is an unverified experience gap, not proof that the product category is empty.

| Candidate | Established overlap | What still needs checking |
|---|---|---|
| [Cap](https://cap.so/) | Screenshot styling/copy plus Studio recording/editing; Linux packages and source are real | Exact picker interaction, released Linux feature parity, local workflow, and multi-monitor/window capture behavior |
| [Screenix](https://screenix.studio/docs/screenshots) | Screenshot backgrounds, crop, annotations, redaction and clipboard; video [trim, cuts and speed](https://screenix.studio/docs/trim-cut-and-speed) | Commercial closed-source option; screenshot flow uses an editor; compare effort and final appearance |
| [ApexShot](https://github.com/apex-shot/apexshot) | Linux screenshot beautification/annotation, recording, and trim/resize/re-encode/audio editing; GPL-3.0-or-later | No verified current-image gallery or equivalent complete video styling/cuts/speed flow; public-beta reliability needs testing |
| [Pretty Screenshot](https://github.com/ricardosuman/omarchy-pretty-screenshot) | Omarchy Print Screen to automatically framed clipboard output; wallpaper, gradient, solid or none; MIT | Menu-configured styles rather than live finished alternatives; no video; stock-capture integration may be behind current Omasnap |
| [Omasnap](https://github.com/omacom/omasnap) | Native screenshot/annotation flow with mesh backgrounds and configured default styling; MIT | No video; no documented simultaneous finished-look gallery |
| [OpenShots](https://github.com/Tracekit-Dev/openshots) | Offline screenshot editor, gradients/borders/shadows, presets and clipboard; MIT | Screenshot-only; README documents a Wayland global-hotkey limitation |
| [Gradia](https://apps.gnome.org/Gradia/) | Backgrounds, shadow, padding, automatic balance and annotation | Screenshot-only; useful visual/interaction baseline |
| [Shotdock](https://github.com/sirrryasir/shotdock) | Wayland screenshot framing, backdrop presets, clipboard and recording; MIT | Delegates annotations; no documented integrated video editing timeline |
| [Omascreen](https://github.com/k4ditano/omascreen) | Omarchy recording and video cut/trim/speed/layers/subtitles/export; MIT | No established screenshot treatment picker; some preview effects are documented approximations |
| [ChalKak](https://github.com/BitYoungjae/ChalKak) | Hyprland capture, preview, copy/save and lightweight editing | No documented premium frame catalog or video suite |
| [Omashot](https://github.com/brianblakely/omashot) | Omarchy screenshot/recording overlay with Omacut handoff | Multiple-tool workflow rather than the proposed shared editor |

Cap details that change the earlier recommendation:

- Its [download page](https://cap.so/download) lists Linux packages. Its [Linux packaging documentation](https://github.com/CapSoftware/Cap/blob/main/packaging/linux/README.md) explicitly covers Arch and Omarchy and references Studio's editable camera track. Linux cannot be dismissed as Instant-only or unsupported based on an older introductory README.
- The reviewed [Wayland screenshot source](https://github.com/CapSoftware/Cap/blob/main/crates/recording/src/screenshot.rs) requires exactly one enumerated display and rejects window screenshot targets in that path. This is a source-level limitation to verify against a specific installed release; it is not a claim that every Cap capture mode fails on multiple monitors.
- [Release notes](https://github.com/CapSoftware/Cap/releases) mention screenshot presets and after-capture automation. Test those features directly before assuming our desired interaction is absent.
- Its [license](https://github.com/CapSoftware/Cap/blob/main/LICENSE) must be reviewed before a fork or code reuse. An extension of Cap or ApexShot would be a separate licensing/maintenance choice from our own app using the reviewed MIT modules.

Pretty Screenshot has a concrete compatibility question. Its [script](https://github.com/ricardosuman/omarchy-pretty-screenshot/blob/main/omarchy-pretty-screenshot) expects a saved filename on standard output from `omarchy-capture-screenshot ... save`. The installed Omarchy wrapper now delegates directly to Omasnap; no matching filename stdout interface was found in the reviewed Omasnap source. Treat this as a suspected integration mismatch to test, not a proven runtime failure. Its `--input` path still provides a way to compare its image treatment independently.

Search coverage:

- GitHub: candidate repositories, documentation, selected implementation files, packaging guidance, and release notes.
- Omarchy plugins: the rendered catalog was insufficient, so the agent inspected the [actual marketplace registry](https://github.com/omacom/omarchy-plugin-marketplace/blob/main/registry.json). It includes Pretty Screenshot, Omascreen, Omashot and other capture helpers. [Omahub's Omascreen page](https://omahub.dev/plugins/k4ditano.omascreen) also provided a discovery route.
- Reddit: original [ChalKak announcement](https://www.reddit.com/r/omarchy/comments/1r4si8p/i_missed_that_legendary_mac_screenshot_tool_so/), [Omascreen announcement](https://www.reddit.com/r/omarchy/comments/1vv79dp/i_built_a_screen_recorder_and_video_editor_for/), and [OpenShots discussion](https://www.reddit.com/r/indiehackers/comments/1sh5mqk/openshots_free_opensource_alternative_to/), plus targeted searches. These establish discovery and discussion, not reliable comparative testing. Older pricing statements were not treated as current licensing evidence.
- X: multiple indexed searches were attempted, but useful readable posts were not returned and a relevant author profile did not provide readable content. X coverage is incomplete. Do not claim that X confirms no equivalent exists.

The next step is the bounded practical comparison in milestone 0 as a quality and reuse benchmark for the chosen standalone product. Related products establish expectations that this project should meet or exceed in its supported environments.

## 15. Recording review after the owner's menu screenshot

Added 2026-09-25. The supplied image shows the four current menu choices: no audio; desktop audio; desktop plus microphone; and desktop plus microphone plus webcam. The owner reports that the current recording works reliably and asks where this app would fit and what would improve materially.

Read-only inspection traced these menu rows and Alt+Print to a local wrapper, then `~/.local/bin/omarchy-capture-screenrecording-mjpeg`. This is a customized flow, not an untouched stock installation. No capture, audio-device change, or GUI action was performed during the review.

Observed implementation and proposed improvements:

| Current evidence | User-facing improvement | Priority |
|---|---|---|
| Menu combines audio/camera choices; the script can accept microphone audio independently | Independent Desktop, Mic, Camera toggles; remember chosen devices; live levels and a short optional countdown | Core recording |
| UI does not expose pause; engine documentation supports pause and instance-specific control | Pause/resume, elapsed time, visible audio state, consistent Stop from keyboard/bar/app | Core recording |
| Desktop and microphone are mixed into one track while recording | Preserve separate sources for later voice/desktop balance; mix normally in the final shared file | Core when supported by the backend |
| On stop, the current script offers a notification that opens mpv | Immediate plain review with trim handles, remove-pause editing and copy/drag/share controls | Highest-value first video delivery |
| Current capture uses 60 fps CFR and automatic codec choice | Retain dependable capture; add verified compatibility/smaller-file output choices and truthful size/dimension feedback | Core export |
| Webcam is a live mpv window baked into the screen capture | First preserve this capability with dependable format selection; later capture separately for movable post-recording camera layout | Parity first, advanced later |
| Local script explicitly selects `clean_desktop_microphone`, negotiates MJPEG, and omits stock loudness normalization | Preserve those working choices; never substitute generic default input or reintroduce automatic loudness boosting | Regression requirement |
| Current finalization skips an initial 0.1 seconds and mutes approximately the first 0.4 seconds of processed audio | Establish an explicit ready/countdown boundary and investigate transient-free startup so the first spoken words survive | Recording spike |
| Current stop handling targets all matching recorder processes; the stock indicator discovers recordings the same way | Session-owned control and tested routing of stock menu/key/bar actions to the correct owner | Required before direct-recording rollout |

The installed script is configured for these behaviors; this review did not measure the resulting image quality, audio fidelity, dropped frames, or startup loss in a new recording. A performance or quality improvement must be demonstrated with before/after clips rather than inferred from changing codec settings.

Priorities for a more polished video result are readable framing/crop, understandable audio, removal of dead time, and reliable fast sharing. The later owner request expands the full product to include focus/zoom, cursor emphasis, optional voice cleanup, local captions, and separate webcam layout. These now have explicit milestones while remaining absent from the quick flow unless needed. Video borders are optional throughout.

The implementation sequence is deliberately low risk: support completed Omarchy recordings in the editor first, add the app's own controls around the same recording engine next, then optionally route the normal recording shortcut/menu through that interface once parity is proven. The existing recorder remains usable, and importing its files never requires replacing it.

Engine capability reference: [GPU Screen Recorder control documentation](https://git.dec05eba.com/gpu-screen-recorder/about/). Local evidence: the installed menu extension, Alt+Print binding, recording wrapper/script, and packaged bar indicator reviewed in this turn. The app is not implemented yet.

## 16. Feature synthesis for the complete product

Added after the owner's direction to combine the strongest features of the reviewed apps into a category-leading Arch/Omarchy product. This section expands the earlier lightweight scope. It is a product design, not a claim that competitors have been interactively benchmarked or that their every marketed feature works on Linux.

| Reference | Idea to retain | How it belongs in this app |
|---|---|---|
| MatteShot | Finished choices and immediate clipboard output | The defining screenshot interaction: capture, choose, copy/save, with automatic style selection and an optional favorite shortcut |
| Omasnap | Fast native selection, scrolling, editable annotations, OCR, cut-band tool, pinned references | One capture surface and image document model, with most-used tools visible and utilities under More |
| Gradia | Accessible screenshot presentation and annotation controls | Clear spacing, restrained tool layout, natural backgrounds and direct manipulation; validate results against its exported images |
| ApexShot | Broad Linux screenshot utilities and recording integration | Strong crop/redact/annotation coverage, text/QR extraction, and consistent image/video entry points |
| Pretty Screenshot | Automatic styling that fits the desktop | Optional theme-derived treatment and a zero-extra-decision shortcut; export styles remain stable when the desktop theme changes |
| Shotdock | Convenient framing and command-line entry points | A small stable CLI for capture, opening inputs, applying a preset and exporting; no requirement to script the normal flow |
| OpenShots | Reusable presets and direct output | Remembered appearance/output choices with clear reset and immediate copy/save |
| ChalKak | Preview before committing, more editing only when needed | A useful quick result that expands into the editor without reopening the media |
| Cap | Automated focus/cursor presentation and separately editable capture streams | Optional focus/zoom suggestions and camera layout in the same video document; quick clips remain plain and immediately usable |
| Screenix | Connected recording, editing, presentation and output | A consistent route from stop to trim, precise edits, and export, with no trip through separate specialist applications |
| Omascreen | Timeline power, voice-over, captions, layers and audio controls | Contextual tracks and panels in the expanded editor, with source-time alignment and preview/export parity |
| Omaroll | Ordinary files, useful handoffs and theme integration | Output appears in the existing library; open-with, LocalSend/Taildrop where installed, and no duplicate media catalog |

Reference links: [Omasnap](https://github.com/omacom/omasnap), [Gradia](https://apps.gnome.org/Gradia/), [ApexShot](https://github.com/apex-shot/apexshot), [Pretty Screenshot](https://github.com/ricardosuman/omarchy-pretty-screenshot), [Shotdock](https://github.com/sirrryasir/shotdock), [OpenShots](https://github.com/Tracekit-Dev/openshots), [ChalKak](https://github.com/BitYoungjae/ChalKak), [Cap](https://cap.so/), [Screenix guide](https://screenix.studio/docs), [Omascreen](https://github.com/k4ditano/omascreen).

Take useful behaviors and verified implementation ideas while designing original product visuals. Do not copy branding, trade dress, commercial assets, or entire incompatible codebases. Source licenses constrain code reuse separately from feature inspiration. Use preserved attribution for adapted modules and normal dependencies for external tools.

### Screenshot tools as one system

The completed screenshot editor supports precise capture and framing, expressive annotations, and reliable concealment. Arrows, text, steps, crop, and redact stay prominent. Freehand, shapes, spotlight, magnification, image-band removal, OCR selection, QR recognition and pinning sit under labeled secondary controls.

Pins show the rendered version, including redactions. Their actions reopen the corresponding draft when available; dragging or copying shares flattened output. QR recognition shows the result and requires explicit action to open a URL. OCR supports useful selectable text without a new document window. Existing images enter exactly the same editing path as fresh captures.

Offer only a few output presets initially: Original, Compact, and explicitly sized social/document formats. Remember style and size independently. A saved user preset packages look, spacing, aspect, and output size, with a live thumbnail and a visible Reset. Do not turn the eight-look picker into a catalog of hundreds of generic backgrounds.

### Focus, cursor, and camera

Add Focus to the video editor with manual zoom regions first. The completed feature supports suggested focus regions derived from recorded pointer/click events. Suggestions should hold on meaningful activity, avoid chasing incidental pointer motion, and return to the full view when context is needed. Generate editable focus segments; users can delete, move, lengthen, or disable them together.

Pointer events are recorded only for an explicitly active, app-owned recording and mapped to captured source pixels and timestamps. No keyboard text logging or background activity history. The capture backend must demonstrate access to the required metadata on Hyprland. If metadata is absent in an imported video, keep manual focus available and label automatic suggestions unavailable instead of inventing a cursor path.

Cursor size, click emphasis, hide-when-idle, and smoothing require correctly synchronized cursor metadata and a capture mode that avoids a second cursor baked into the video. Expose only the verified combinations. Cursor processing must not move actual desktop input. Test visual focus against clicks, cropping, scaling, variable frame rate, and edited timing.

Camera footage is a separate synchronized source in the complete product. Give it simple presets: hidden, small corner, larger corner, side-by-side, and camera-only. Drag/resize and one-click layout choices cover normal use. Preserve the ability to move or hide the camera after capture. Camera dropout must not freeze a stale face over the remaining recording without explanation.

Use the same restrained motion language for focus changes, cursor emphasis, camera layouts, and clip transitions. Animation should improve comprehension. Default video remains plain until the user enables these features; the app can remember a video preset independently of screenshot settings.

### Audio, narration, and captions

Preserve unprocessed original tracks. The default retains recorded levels and the explicitly selected working microphone. For an already cleaned microphone, do not stack another denoiser automatically. An optional Enhance voice action can apply a tested cleanup chain with immediate A/B preview and Undo. Expose desktop/voice balance directly; put detailed filters behind Adjust.

Add voice-over at the playhead with a count-in, listen-back and retake. Music is an imported user-selected track with trim, volume and optional ducking beneath narration. Do not ship an unlicensed music library. Audio transitions across edits need short fades where appropriate to prevent clicks without cutting words.

Local captions are an optional worker capability. The first use explains any required model download and its size; no model download or transcription runs merely because a recording stopped. Provide cancellation and resource limits. After setup, recognition runs locally and supports editable text/timing, a few readable caption styles, burned-in output and SRT export.

Transcript-assisted editing maps words to source-time ranges and highlights the proposed cut in video before applying it. Silence detection produces suggested removable ranges with handles and playback, not irreversible automatic deletion. Do not infer a complete or accurate transcript from an unverified model result. Non-speech footage remains fully editable without any transcript.

### Simple assembly and export

Allow Add clip, split, delete, reorder, and join in one primary screen-content track. Offer a hard cut by default and a small set of restrained transitions. Supporting camera, audio, caption and annotation tracks does not require a general-purpose compositor UI. New tracks appear only when they have content or a user explicitly adds them.

Different resolutions, frame rates and audio layouts resolve through a project output specification. Show the chosen final dimensions and duration. A new smaller clip is not silently enlarged; offer Fit/Fill with clear crop behavior. Shared time mapping keeps captions, focus, camera and audio aligned after every edit.

The export surface defaults to the last appropriate local preset and shows format, dimensions, estimated work and destination. An unchanged compatible recording can be used directly. Size estimates must be labeled estimates unless the bytes already exist. A bounded queue allows continued editing; its jobs snapshot the document so later edits cannot silently change an in-flight export.

Local copy/save/drag is always available. Optional LocalSend or Taildrop actions use installed user-configured tools and explicit destinations. Hosted URLs, cloud storage and account systems are not required to use the app and are outside the initial complete release.

## 17. What earns the quality claim

The release target is a noticeably better daily experience, not the longest feature list. Maintain a scorecard with the best observed reference behavior and the app's measured result for each common task.

| Dimension | Acceptance requirement |
|---|---|
| Immediate usefulness | A first screenshot looks finished with one acceptance action; a quick video can be shared without entering the expanded editor |
| Visual consistency | Every shipped look works across the fixture matrix; controls and output are inspected separately; light/dark defaults remain readable |
| Editing coherence | One document/undo history through capture, quick review and editor; image/video tools share behavior; no external editor handoff for promised core features |
| Recording trust | Actual source devices and levels are visible; pause/stop reach the right process; no accidental self-capture or silently missing audio/camera |
| Timing correctness | Cuts, speed, focus, captions, pointer/camera and audio agree between preview and export; boundary cases have regression fixtures |
| Fidelity | Text stays legible, color conversion is declared, originals survive, redactions remain effective, and outputs have validated streams |
| Responsiveness | Meet the measured targets in section 9 with bounded jobs, cancelable background work, and no hidden polling/encoding while idle |
| Platform quality | Fresh-install and upgrade tests on Omarchy and plain Arch/Hyprland; report the GPU/driver/display configurations actually tested |
| Recovery | Interrupted capture/export, missing media, full disk and failed clipboard retain work and explain the actual state |
| Discoverability | New users can find crop, redact, trim and audio balance without a guide; complete keyboard paths and observed accessibility checks |

Three canonical acceptance tasks should appear in every milestone demo:

1. Share a polished screenshot: capture a window, choose Paper or Adaptive, paste it into another app, then reopen and redact one detail without losing editability.
2. Send a quick explanation: record with the selected mic, pause once, stop, trim an end, and share a plain clip with synchronized audio.
3. Make a polished demo: assemble two clips, adjust a focus segment, reposition the camera, balance voice/desktop audio, correct captions, and export a result that matches the preview.

The third task becomes a release gate when the complete studio is delivered. It cannot be replaced by a screenshot of an attractive but nonfunctional timeline. Artifacts and owner-observed hardware acceptance are required before describing the product as best in category. Remaining unsupported environments or features stay explicit.


## Recording implementation update, 2026-09-26

The local preview now owns GPU Screen Recorder launch/stop and opens the saved clip automatically. The capture bar switches between Screenshot and Video. Recording setup includes region/display, independent desktop/mic switches, explicit microphone selection, cursor visibility, and countdown. A timer/Stop pill is placed outside the capture and its actual compositor geometry is checked before starting. Full-display recording uses another monitor for Stop when available; otherwise keyboard-only stopping requires explicit acknowledgement.

Pause, separate audio tracks, camera integration, scrolling capture, and automatic focus remain planned. Existing Omarchy webcam recording remains available. See recording-validation.md for the distinction between automated/UI verification and physical recording acceptance.
