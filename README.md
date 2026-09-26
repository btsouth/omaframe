# Omaframe

A native screenshot and recording finisher for Omarchy and Arch/Hyprland. Qt 6, C++20, QML. Local files, no accounts, no uploads, no watermark.

This is a working **0.2 preview**. It is a separate app from Omaroll. Shortcut integration is opt-in. Recording uses GPU Screen Recorder with Omaframe's own setup, Stop control, and editor handoff.

## Try it

```sh
./run
./run --capture
./run --studio
./run --record
./run --screen
./run /path/to/image.png
./run /path/to/recording.mp4
```

Launching with no arguments captures a region immediately. Drag on any display, then click a finish or press **1–9**. Omaframe saves the full-resolution PNG, copies it, and closes. Paste into your destination. **Enter** uses your last finish. **9** keeps the image without a border.

Press **E** or click **Edit** in the chooser to crop, annotate, or redact. **Escape** or **Back to finishes** returns to the chooser with all edits intact. **R** retakes; **Escape** in the chooser or selection cancels without changing the clipboard. Repeated launches during a capture reuse the current session.

Use `./run --studio` for the full editor, sample images, and video tools. Open a file directly, or drop one into the studio. The app remembers your finish, padding, canvas ratio, and output folders.

The command for a Print Screen binding is the absolute path to `build/omaframe --capture` (or `omaframe --capture` after installation). This checkout does not modify your existing Omarchy shortcuts.

## What works

- Native Wayland capture through `ext-image-copy-capture`, with a frozen region selector on each connected display. Regions are selected within one display at a time.
- Eight finishes: Paper, Slate, Deep, Aurora, Adaptive, Outline, Studio, Ambient. Raw keeps the edited image without a border.
- Full-resolution PNG output, rounded corners, soft shadows, content-derived Adaptive colors, padding, and five canvas choices.
- Crop, arrows, highlighting, opaque redaction, numbered steps, and text labels. Undo and redo retain the original image in memory.
- Automatic save and clipboard copy on acceptance. Saving and rendering run off the UI thread. Files are committed atomically, with unique filenames.
- Open MP4, WebM, MKV, MOV, and M4V recordings. Play, scrub a filmstrip timeline, trim by dragging its handles or typing times, optionally remove audio, and export MP4.
- Displays are named by position and model ("Left · Dell S2721DGF · 2560 × 1440") instead of connector names like DP-2.
- Video export uses H.264 CRF 18 and AAC, preserves all audio tracks when enabled, supports cancellation, and leaves the input file unchanged. Odd video dimensions are rounded down by at most one pixel for H.264 compatibility.

## Record a video

Choose **Video** on the capture bar or run `omaframe --record`. Select an area or display, toggle desktop audio and microphone independently, choose whether to show the cursor, and start with an optional countdown. Settings are remembered.

A small timer and **Stop** button sit outside the recorded region on the same display. When there is no room for it, such as a full-display recording, there is no on-screen control: click the recording icon in the Omarchy bar or press the hotkey. Without the Omarchy shell, setup requires acknowledging the keyboard stop method. Bind `omaframe --record` to Alt+Print to open setup and stop an active recording; `omaframe --stop-recording` only stops a recording owned by the running app.

Stopping saves the MP4 and opens it in the video editor for trimming. The recorder uses explicit audio source IDs, prefers an available Clean Desktop Microphone on first use, and does not normalize loudness. To retain this desktop's startup-pop suppression, the first 400 ms of audio are muted and fade in over 50 ms. Video frames are copied unchanged during that cleanup. Desktop and microphone audio currently share one mixed track.

Webcam recording remains available through Omarchy's existing menu. There is no pause, automatic zoom, or capture exclusion for an on-screen control yet.

## Omarchy theme

Omaframe's chrome follows the active Omarchy theme and changes the moment you switch themes, with no restart. It reads the same sources as the Omarchy shell: `colors.toml` for the palette, the theme's `shell.toml` for menu, popup, control and recording roles (with `~/.config/omarchy/shell.toml` overrides on top), Hyprland's `decoration:rounding` for corners, and the `monospace` font alias. Secondary text is derived to stay readable in every stock theme, light or dark. Finishes and rendered images keep their own colors; only the app around them is themed.

The capture bar is an Omarchy popup framed in the active border color. **Tab** switches between screenshot and video, and video mode marks the selection in the theme's recording color.

## Files and privacy

Finished images default to `~/Pictures/Omaframe`. Clips default to `~/Videos/Omaframe`. Use **Change** beside the folder in the footer to choose another directory.

Accepting an image also saves a **private, unedited original** under the Qt application data directory, normally `~/.local/share/Omaframe/Omaframe/originals`. These originals can contain details you redacted from the finished image. They use owner-only file permissions. They are retained until you delete them; there is no automatic cleanup in 0.2.

Finished image exports contain only rendered pixels, with imported image text metadata removed. Redaction replaces pixels with an opaque fill. Original files are never overwritten. There is no project/history browser yet; closing the app discards the editable operation history, while accepted originals and finished files remain on disk.

## Shortcuts

| Shortcut | Action |
| --- | --- |
| 1–9 | Choose a finish, copy, save, and close the chooser |
| E / R | Edit / retake from the chooser |
| Ctrl+O | Open an image or recording in the studio |
| Enter | Accept the remembered finish in the chooser; copy and save in studio Finish view |
| Ctrl+S | Save image or export the selected clip |
| Ctrl+Shift+C | Copy and save image |
| Ctrl+Z / Ctrl+Shift+Z | Undo / redo in Edit view |
| C / A / H / R / N / T | Crop / arrow / highlight / redact / steps / text |
| Tab | Switch the capture bar between screenshot and video |
| Escape | Return from quick editor to chooser; cancel chooser or region selection |
| Space | Play or pause a recording |
| I / O | Set the clip start / end at the playhead |
| Left / Right | Step the playhead 0.1 s (Shift: 1 s) |
| Home / End | Jump to the clip start / end |

## Build on Arch

Dependencies: `base-devel cmake ninja pkgconf qt6-base qt6-declarative qt6-multimedia layer-shell-qt wayland wayland-protocols wl-clipboard ffmpeg gpu-screen-recorder libpulse`. Other image formats depend on the Qt image plugins installed on your system.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j 3
./build/omaframe
```

An optional user-local install is supported with `cmake --install build --prefix ~/.local`. No shortcut bindings are installed automatically.

## Validation

Tests that use the desktop must run in omabox:

```sh
omabox run --net isolated -- ctest --test-dir build --output-on-failure
```

The suites cover rendering geometry, opaque redaction, edit ordering, undo/redo, clipboard/save equality, source preservation, metadata removal, concurrent preview requests, native output capture, video trimming, multiple audio tracks, mute, cancellation, and bad inputs. See [capture workflow validation](docs/capture-workflow-validation.md) and [first-version validation](docs/first-version-validation.md) for the manual UI checks and remaining hardware checks.

## What comes next

Recording setup, safe Stop placement, and automatic editor handoff are implemented. See [recording validation](docs/recording-validation.md) for tested behavior and remaining hardware checks.

The [product plan](docs/product-plan.md) includes window and scrolling capture, OCR, pinning, reusable projects/history, recording pause, zoom/cursor improvements, captions, and richer media editing. Those are future work, not controls hidden in this build. Multi-monitor, fractional-scale, color-managed/HDR, physical microphone, and webcam behavior still need real hardware acceptance.

## License

MIT. The native Wayland capture implementation is adapted from Omasnap at `acfb3b5772ccb041b57ccc76e16f1b72719f37e9`, copyright Tobi Lütke. Its license is retained in [docs/OMASNAP-LICENSE](docs/OMASNAP-LICENSE). MatteShot and Omaroll informed the workflow and style direction; this app has its own UI and renderer.
