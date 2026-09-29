# Omaframe release and MatteShot parity tracker

Current release: **0.3.0**. This tracks shipped behavior and possible future
work. It is not a release checklist. See [RELEASING.md](../RELEASING.md) for
the current release policy and [ci.md](ci.md) for automated coverage.

Reference: local MatteShot `01f16a2` (0.21.1) README and source, reviewed on
2026-09-27. The apps use different capture APIs and operating systems.

| Workflow | Omaframe 0.3.0 | Future work |
| --- | --- | --- |
| First run | Start screen, one-time explanation and optional Print/Alt+Print setup that preserves custom bindings | Custom key recorder in Settings |
| Capture and finish | Window, area or display; finish picker; copy/save; repeat last area; powered-off display handling | Cross-monitor areas, size presets |
| Screenshot editing | Movable and resizable marks, inline multiline labels, crop, blur/redaction, layers, undo/redo and editable drafts | Pinning, scrolling capture, curved arrows |
| Screenshot text | Copy text with T; hide possible secrets with H in the picker or Shift+H in the editor; optional local Tesseract | Selectable OCR regions |
| Record and stop | Display or region, computer sound and selected microphone, countdown; Stop outside the capture or hotkey/bar stop when no safe placement exists | Pause/resume next; window-follow capture and camera later |
| Video review | Playback, trim and middle cuts, sound on/off, timed blur/redaction, arrows, boxes, labels and steps, undo/redo, copy/save MP4 | Crop/zoom, speed sections and GIF export |
| Distribution | Published Arch package, source archive, PKGBUILD, checksums, desktop entry and license notices | Optional sharing and updates need their own design |

## MatteShot comparison

Omaframe matches inline label typing, direct mark editing, hover/selection
feedback, an Esc ladder and a first-run welcome. It also provides multiline
labels, resize handles, redo, duplicate, layers, middle-section video cuts
and optional shortcut setup.

MatteShot excludes its recording control through Windows capture APIs.
Omaframe's KMS capture includes the composited display, so it places Stop
outside the capture or uses the configured stop key and Omarchy bar.

## Next work

Recording pause/resume is the next feature. Verify recorder support and
audio/video timing, keep the control
compact, and cover pause, resume, stop while paused and finalized output with
regression tests. Pause/resume is planned, not part of 0.3.0.

Manual crop/zoom is a possible later addition. Camera, automatic focus,
captions, clip assembly and other large editor features remain backlog.

## Validation notes

The 0.3.0 release notes record seven passing suites in an isolated desktop,
isolated UI checks and owner-desktop video mark checks. Powered-off monitor
repeat and a fresh Omarchy stable desktop were not retested for 0.3.0.
These are coverage limits, not pending owner tasks or release blockers.
Keep historical measurements in the validation documents; investigate
reported regressions without requiring a manual hardware matrix per release.
