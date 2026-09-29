# Omaframe release and MatteShot parity tracker

Current release: **0.4.0**. This tracks shipped behavior and possible future
work. It is not a release checklist. See [RELEASING.md](../RELEASING.md) for
the current release policy and [ci.md](ci.md) for automated coverage.

Reference: local MatteShot `01f16a2` (0.21.1) README and source, reviewed on
2026-09-27. The apps use different capture APIs and operating systems.

| Workflow | Omaframe 0.4.0 | Future work |
| --- | --- | --- |
| First run | Start screen, one-time explanation and optional Print/Alt+Print setup that preserves custom bindings | Custom key recorder in Settings |
| Capture and finish | Window, area or display; finish picker; copy/save; repeat last area; powered-off display handling | Cross-monitor areas, size presets |
| Screenshot editing | Movable and resizable marks, inline multiline labels, crop, blur/redaction, layers, undo/redo and editable drafts | Pinning, scrolling capture, curved arrows |
| Screenshot text | Copy text with T; hide possible secrets with H in the picker or Shift+H in the editor; optional local Tesseract | Selectable OCR regions |
| Record and stop | Display or region, computer sound and selected microphone, countdown, pause/resume; controls outside the capture or shortcut/bar stop when no safe placement exists | Window-follow capture and camera |
| Video review | Playback, trim and middle cuts, sound on/off, timed marks, undo/redo, copy/save MP4; confirmation before leaving unsaved edits | Draft recovery, crop/zoom, speed sections and GIF export |
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
keep editor controls and recording options reachable in smaller windows.
Settings and capture menus close with Esc, and the sample opens in Edit.
Unsaved video edits prompt before opening another file, starting a capture,
or quitting, including commands from capture shortcuts. Save and continue
waits for a successful export; a failed save keeps the edits open. Video
draft recovery remains future work.

GPU Screen Recorder 6.1.0 already supports the private `set-paused` control and
a shared pause-aware video/audio clock. See its [6.1.0 control documentation](https://git.dec05eba.com/gpu-screen-recorder/tree/README.md?h=6.1.0).
Omaframe waits for the backend's reply before changing its displayed state.
If a sent request loses its reply, it saves the recording because the pause
state cannot be confirmed. Fixture and isolated UI checks do not measure
physical microphone quality or GPU-specific capture timing.

## Possible next work

Manual crop/zoom is a possible later addition. Camera, automatic focus,
captions, clip assembly and other large editor features remain backlog.

## Validation notes

All eight test suites passed in an isolated desktop for 0.4.0. Light and dark
themes, small windows, pause/resume controls, file drops, unsaved-edit choices
and failed-save retry were checked there using fixture recordings. CI builds
on Arch and runs six headless suites, OCR pattern tests and install checks.

The 0.3.0 release notes record seven passing suites in an isolated desktop,
isolated UI checks and owner-desktop video mark checks. Powered-off monitor
repeat and a fresh Omarchy stable desktop were not retested for 0.3.0.
These are coverage limits, not pending owner tasks or release blockers.
Keep historical measurements in the validation documents; investigate
reported regressions without requiring a manual hardware matrix per release.
