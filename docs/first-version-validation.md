# First-version validation

Date: 2026-09-25. Build: 0.1.0 preview. Local source under `/home/bts/Projects/omaframe`.

## Automated checks

Built with CMake/Ninja, GCC 16.2.1, and Qt 6.11.2 on Arch/Omarchy. Final clean test command:

```sh
omabox run --net isolated -- ctest --test-dir build --output-on-failure
```

Result: **2/2 suites passed**, 2.02 seconds. There are 13 substantive test cases, plus QtTest setup/cleanup.

- Renderer: Raw pixel preservation; all finish dimensions and source content; aspect ratios without cropping; opaque redaction replacing alpha and color; crop/edit coordinate ordering; reversed selections; bounded previews.
- Pipelines: crop undo/redo; saved PNG and clipboard equality; imported metadata removal; input file preservation; newest preview winning rapid changes; all thumbnail results available; accurate trim duration; preservation of two audio tracks; audio removal; metadata removal from clips; export cancellation removing partial files; bad inputs and invalid ranges; real native Wayland output capture.

The tests use generated images and a synthetic video with two sine-wave audio tracks. No personal captures are used as fixtures.

## Observed UI checks

All app launches, input, clipboard operations, and captures ran inside an isolated omabox desktop, with networking isolated. The real desktop was not used.

- Layout inspected at 1440×1000 and 1024×768. The finish sidebar scrolls at the smaller size; capture and accept controls remain visible.
- Eight finish thumbnails show the actual image. Light and dark samples inspected. Padding and canvas controls are exposed below the finishes.
- Region capture freezes the display before selection. An 800×550 selection produced an 800×550 source. The app was absent from the frozen capture.
- `--capture` starts directly in selection; Escape restores the studio. `--screen` returned the entire 1440×1000 display.
- Selecting a region did not change the previous clipboard before acceptance.
- Copy-and-save created a full-resolution PNG. Its SHA-256 matched the clipboard bytes. A later check after closing the app confirmed that the copied PNG remained available.
- The final example PNG and clipboard both had SHA-256 `c3e50e95ae446f85114973b745adb840099acea416e6ddf0f398338e5f3cece0`.
- Saved original backup permissions checked as `0600`.
- Arrow drag, text entry/acceptance, and numbered-step placement inspected in the live editor. Crop, redaction, undo/redo, and source preservation also have automated coverage.
- Open Media file picker successfully opened a recording by path. A discovered Enter-shortcut conflict was fixed and retested: accepting the picker opens the file without saving the screenshot behind it.
- Recording playback advanced normally; Space paused it. Numeric trim fields updated the kept duration. The selected 1.2–4.8 second interval exported as a 3.600000 second H.264/AAC MP4 at 1280×800.
- Normal window closure, capture cancellation, and error recovery checked. The selection window no longer prevents the main app from quitting.

## Saved artifacts

Local evidence is in `evidence/`, intentionally excluded from Git:

- `omaframe-first-version.png`: running screenshot studio.
- `omaframe-recording.png`: running recording trimmer.
- `compact.png` and `compact-video.png`: smaller layout checks.
- `captured-region.png`, `cli-capture.png`, `entire-display.png`: native capture checks.
- `text-applied.png`, `steps.png`, `arrow.png`: editing checks.
- `finished-example.png`: actual exported image, 1510×1030, about 221 KiB.
- `trimmed-example.mp4`: actual exported 3.6-second clip.

## Boundaries of this validation

The box has one virtual display and no physical audio devices. Multi-monitor selection, rotated monitors, fractional scaling, HDR/color management, real audio playback, microphones, and webcams are not hardware-verified. The encoder tests prove audio-track presence and preservation, not audible playback through the user's devices.

This version opens finished recordings. It does not start, stop, replace, or automatically receive files from Omarchy's recorder. Window/scrolling capture, OCR, pinning, editable project reopening/history, zoom/cursor processing, and captions remain roadmap work.

Imported color profiles are not yet normalized by a dedicated color-management pipeline. The first version targets ordinary SDR screenshot workflows.

No shortcuts or desktop configuration were installed. No release, repository push, or external publication was performed. The existing MatteShot, Omaroll, and Omasnap worktrees retain their baseline state.
