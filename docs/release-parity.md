# Omaframe release and MatteShot parity tracker

Current release: **0.7.1**. This tracks shipped behavior and possible future
work. It is not a release checklist. See [RELEASING.md](../RELEASING.md) for
the current release policy and [ci.md](ci.md) for automated coverage.

Reference: local MatteShot `01f16a2` (0.21.1) README and source, reviewed on
2026-09-27. The apps use different capture APIs and operating systems.

| Workflow | Omaframe 0.7.1 | Future work |
| --- | --- | --- |
| First run | Start screen, one-time explanation and optional Print/Alt+Print setup that preserves custom bindings | Custom key recorder in Settings |
| Capture and finish | Window, area or display; automatic scrolling capture with manual takeover; finish picker; copy/save; repeat last area; powered-off display handling | Cross-monitor areas, size presets |
| Screenshot editing | Movable marks, side and corner resize handles, inline multiline labels, contextual styles, crop, blur/redaction, layers, undo/redo, readable tall-image editing and editable drafts | Pinning, curved arrows |
| Screenshot text | Copy text with T; hide possible secrets with H in the picker or Shift+H in the editor; optional local Tesseract | Selectable OCR regions |
| Record and stop | Display or region, computer sound and selected microphone, countdown, pause/resume, optional webcam overlay; controls outside the capture or shortcut/bar stop when no safe placement exists | Window-follow capture |
| Video review | Playback, trim and middle cuts, sound on/off, timed marks, crop, camera placement, recovery drafts, undo/redo, copy/save MP4 and short looping GIFs | Animated zoom and speed sections |
| Distribution | Published Arch package, source archive, PKGBUILD, checksums, desktop entry and license notices | Optional sharing and updates need their own design |

## MatteShot comparison

Omaframe matches inline label typing, direct mark editing, hover/selection
feedback, an Esc ladder and a first-run welcome. It also provides multiline
labels, resize handles, redo, duplicate, layers, middle-section video cuts
and optional shortcut setup.

MatteShot excludes its recording control through Windows capture APIs.
Omaframe's KMS capture includes the composited display, so it places Stop
outside the capture or uses the configured stop key and Omarchy bar.

## Since 0.3.0

Version 0.4.0 adds labeled Pause/Resume controls and a timer that excludes
paused intervals. Optional shortcut setup adds Alt+Shift+Print for pause/resume
when that key is free, including recordings without a visible control.
Instance-specific commands remain available for scripts and custom keys.
Stop works while paused. The recorder suite covers repeated transitions,
rejected or lost replies, normal finalization and preserving custom shortcuts.

Version 0.4.0 also centers button contents, labels the video mark tools, and
keeps editor controls and recording options reachable in smaller windows.
Settings and capture menus close with Esc, and the sample opens in Edit.
Unsaved video edits prompt before opening another file, starting a capture,
or quitting, including commands from capture shortcuts. Save and continue
waits for a successful export; a failed save keeps the edits open.
Version 0.5.0 adds automatic recovery drafts.

GPU Screen Recorder 6.1.0 already supports the private `set-paused` control and
a shared pause-aware video/audio clock. See its [6.1.0 control documentation](https://git.dec05eba.com/gpu-screen-recorder/tree/README.md?h=6.1.0).
Omaframe waits for the backend's reply before changing its displayed state.
If a sent request loses its reply, it saves the recording because the pause
state cannot be confirmed. Fixture and isolated UI checks do not measure
physical microphone quality or GPU-specific capture timing.

## Since 0.4.0

Version 0.5.0 adds manual video crop, automatic recovery drafts,
optional webcam overlay and GIF export. Crop applies to the whole clip.
Drafts retain edits and reference the original video, which must stay in
place. Camera is off by default and records a separate track using recorded time, so pause,
trim and cuts apply to both tracks. Review lets you move, resize or hide the
camera before exporting a new MP4. Keep the original video, camera track
and camera sidecar together to reopen camera edits.

GIF export makes a silent loop with the same edits, at up to 720 pixels and
15 fps for clips up to 30 seconds. It keeps the editor open and leaves an
existing MP4 export available. Copy or show the GIF after saving.

## Since 0.5.0

Version 0.5.1 improves first-run guidance, small-screen capture controls,
recording options, screenshot labels, keyboard focus and folder pickers.
Export progress and cancellation stay above the video preview. Opened videos
can return to the start screen, and Copy video selects the original after
undoing back to an unchanged clip. Failed screenshot saves can retry with a
new folder without losing the capture.

## Since 0.5.1

Version 0.6.0 adds per-tool styles with remembered choices, text and shape
colors, opacity, arrowheads and contrast outlines. Side handles resize one
dimension at a time, while text corners scale the font. Crop and pen editing
use the same movement and resize controls. Capture hints no longer block
buttons, settings borders fit correctly and recording review closes reliably
after annotation edits.

## Possible next work

Animated zoom, automatic focus, captions and clip assembly remain backlog.

## Validation notes

For 0.6.0, all nine suites passed in an isolated desktop. Annotation styles
were checked for persistence, undo, rendering and exported video appearance.
Isolated UI checks covered contextual controls, custom colors, settings,
small windows and closing with Super+W. Physical camera, audio and
GPU-specific recording behavior were not retested.

For 0.5.1, all nine suites passed in an isolated desktop, with focused
clipboard and failed-save regressions rerun after the fixes. UI checks
covered first launch, retained settings, area/window/whole-display capture,
editing, pause/resume and export in light/dark themes and smaller windows.
Recording checks used fixtures, not physical camera or audio devices.

For 0.5.0, all nine suites passed in an isolated desktop for crop, recovery
and camera changes. Focused GIF export and navigation tests then passed,
including output dimensions, looping, edits, cancellation and the duration
limit. Light and dark small-window checks used fixture recordings. GIF file
copying was checked through the isolated clipboard. Current CI builds on
Arch and runs seven headless suites, OCR pattern tests and install checks.
These checks do not establish physical camera acquisition or GPU behavior.

All eight test suites passed in an isolated desktop for 0.4.0. Light and dark
themes, small windows, pause/resume controls, file drops, unsaved-edit choices
and failed-save retry were checked there using fixture recordings. CI at
that release built on Arch and ran six headless suites, OCR pattern tests
and install checks.

The 0.3.0 release notes record seven passing suites in an isolated desktop,
isolated UI checks and owner-desktop video mark checks. Powered-off monitor
repeat and a fresh Omarchy stable desktop were not retested for 0.3.0.
These are coverage limits, not pending owner tasks or release blockers.
Keep historical measurements in the validation documents; investigate
reported regressions without requiring a manual hardware matrix per release.
