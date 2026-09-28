# Omaframe

Screenshots and screen recordings for Omarchy and Arch/Hyprland. Qt 6, C++20,
QML. Local files, no accounts, no uploads, no watermark.

This is version 0.2. Recording uses GPU Screen Recorder with
Omaframe's own capture bar, Stop control and review.

## Install

On Omarchy or Arch, one command installs the latest release and everything it
needs:

```sh
curl -fLo /tmp/omaframe.pkg.tar.zst https://github.com/btsouth/omaframe/releases/latest/download/omaframe-x86_64.pkg.tar.zst && sudo pacman -U /tmp/omaframe.pkg.tar.zst
```

Then open Omaframe from the launcher and choose **Use Print and Alt+Print**.

To build it yourself instead, download `PKGBUILD` from the
[latest release](https://github.com/btsouth/omaframe/releases/latest) and run
`makepkg -si` next to it.

To uninstall, run `sudo pacman -R omaframe` and delete the block between
`-- omaframe:shortcuts:start` and `-- omaframe:shortcuts:end` in
`~/.config/hypr/bindings.lua`. Your screenshots, recordings and settings are
left in place.

## Try it from source

```sh
./run --studio          # the Omaframe window: start screen, editor, video review
./run                   # select a window, area or display and take a screenshot
./run --record          # the same selector, ready to record; again to stop
./run --repeat          # screenshot the last area again
./run --screen          # screenshot the focused display
./run /path/to/image.png
./run /path/to/recording.mp4
```

The launcher entry opens the Omaframe window. The first time, it explains the
workflow and offers to set up the two shortcuts:

| Key | Action |
| --- | --- |
| Print | Take a screenshot |
| Alt+Print | Start recording, or stop the current recording |

Setup replaces only Omarchy's stock Print and Alt+Print actions. It never
touches a custom binding. It backs up `~/.config/hypr/bindings.lua`, appends a
marked block, and activates the keys at once. Delete the block between
`-- omaframe:shortcuts:start` and `-- omaframe:shortcuts:end` to restore
Omarchy's defaults. Nothing is changed until you choose **Use Print and
Alt+Print**, on the start screen, in Settings, or in recording options. To use
other keys, bind them to `omaframe --capture` and `omaframe --record`;
Omaframe recognizes any key bound to those commands.

## Take a screenshot

Press Print. Every display freezes and a capture bar appears.

- Click a window to capture what is visible of it.
- Drag an area.
- Press **F**, or click **Whole display**, for the display under the bar.
  Clicking empty desktop does the same.
- **Tab** switches to video. **Esc** cancels without touching the clipboard.

The finish picker opens next. Press **1–9** or click a card: Omaframe copies
the full-resolution PNG and saves it to `~/Pictures/Omaframe`, then closes and
shows a notification. **Enter** uses your last finish, **9** keeps the image
without a border, **R** retakes, and **E** opens the editor first.

## Mark it up

The editor keeps its tools in one place on the right. Settings for the
selected mark appear below them.

- **Text**: click where the label goes and type on the image, in its real size
  and colors. Enter adds a line. Esc or a click outside finishes it. Click a
  label with the Text tool, double-click it with any tool, or press F2 to
  change its words.
- Every mark stays editable. With any tool, drag a mark to move it and drag a
  handle to resize it. Drawing tools pick up filled areas (highlight, redact,
  blur, box) by their edge, so you can still start a new mark inside one.
  Right-click selects a mark.
- Tools stay armed until you pick another one. **Esc** steps back one layer at
  a time: finish typing, clear the selection, return to Select, then leave the
  editor with your edits kept.
- Tools: Select (V), Crop (C), Arrow (A), Line (L), Box (B), Oval (O),
  Highlight (H), Redact (R), Blur (G), Pen (P), Steps (N), Text (T).
- Selected marks: duplicate (Ctrl+D), delete, change layer, color (presets or
  hex), thickness, blur strength. Labels: size 8–4096 px by field, slider or
  corner handle, caption box or shadow, alignment, box color and opacity.
  Arrow keys nudge by one pixel, Shift+arrows by ten.
- Crop is reversible. Redaction replaces pixels with an opaque fill.
- Undo and redo (Ctrl+Z, Ctrl+Shift+Z) keep the selected mark.

Annotated images autosave as editable drafts. They appear under **Recent
edits** on the start screen with a preview. A draft keeps a private copy of the
unedited capture so you can change your marks later; delete it there when you
are done.

## Record a video

Press Alt+Print, or press Tab in the screenshot selector. The capture bar turns
red and adds **Sound**, **Mic**, the countdown, and a button for more options.
Click a window, drag an area, or press **F** for the whole display, and the
recording starts after the countdown. **D** and **M** toggle sound and the
microphone. Choices are remembered.

Omaframe never puts its own controls in the video:

- For an area, the timer and Stop button sit just outside it, below, above,
  right or left, whichever fits. They never cover the Omarchy bar.
- For a whole display with another display connected, they sit on the other
  display, at the edge that faces the recording.
- When there is no place outside the recording, such as a whole display on a
  single monitor, no Stop button is shown. A countdown says how to stop, then
  disappears before the first frame. Omaframe checks that none of its surfaces
  is over the recorded area before it starts. Stop with Alt+Print, or with
  the recording icon in the Omarchy bar. This case requires a working stop
  shortcut; recording options offer to set up Alt+Print.

The Omarchy bar is part of the desktop, so a whole-display recording includes
it, with its recording icon. Omaframe cannot hide anything on a recorded
display: GPU Screen Recorder captures the composited screen. Clicking the bar
icon stops Omaframe's recording normally; Omarchy's script then also posts its
own generic "Screen recording saved" notification.

Stopping opens the review window. The recording is already saved in
`~/Videos/Omaframe`. **Copy and close** puts it on the clipboard as a file,
ready to paste into a chat or file manager, closes the review, and shows a
notification with the folder. To shorten it first:

- Drag the ends of the filmstrip, type start and end times, or press I and O.
- Drag across the filmstrip to select a part, then choose **Remove this part**
  (or press Delete). Removed parts stay visible and hatched. Click one to
  change its times or restore it.
- **Sound on/off** saves the video with or without sound.
- Undo and redo cover every change.

With changes, the button becomes **Save and copy**. It writes
`Recording-…-edited.mp4` next to the original, which stays as it was, then
copies the new file and closes the same way. If the save or the copy fails,
review stays open with the error. A video opened in the Omaframe window with
Open stays open after **Export video**, with **Copy file** and **Show file**.
Exports that only shorten the end or remove sound copy the original video
without re-encoding. Other edits re-encode with H.264 at CRF 16. Every export
is probed and decoded before it is reported as saved.

If you stop a recording while you are taking or editing a screenshot, the
screenshot is left alone and the review opens once it is finished.

If Omaframe exits during a recording, the recorder is told to finish its file.
A start that produced no video leaves no file behind.

There is no pause, webcam overlay, zoom or annotation on video yet. Webcam
recording remains available in Omarchy's own menu.

## Omarchy theme

Omaframe follows the active Omarchy theme and updates live when it changes. It
reads the theme's `colors.toml` and `shell.toml`, `~/.config/omarchy/shell.toml`,
Hyprland's rounding and the `monospace` font alias. Finishes and exported images
keep their own colors.

## Files and privacy

Screenshots go to `~/Pictures/Omaframe` and recordings to `~/Videos/Omaframe`.
Change either in **Settings**.

Exported images contain only rendered pixels, with source metadata removed.
Original files are never overwritten.

Settings has an optional **Keep an unedited private copy of each capture**,
off by default. When it is on, each accepted capture also saves its unedited
source, including anything you redacted, to
`~/.local/share/Omaframe/Omaframe/originals` with owner-only permissions, until
you delete them there. Drafts keep their own private source copies until you
delete the draft.

## Shortcuts inside Omaframe

| Shortcut | Action |
| --- | --- |
| F / Tab / Esc | Whole display / switch screenshot and video / cancel, in the capture bar |
| D / M | Sound / microphone, in video mode |
| 1–9, Enter | Pick a finish, or the last one, in the picker |
| E / R | Edit / retake, in the picker |
| Ctrl+O | Open an image or video |
| Ctrl+C, Ctrl+S | Copy and save the image; copy, or save and copy, the video |
| Ctrl+Z / Ctrl+Shift+Z | Undo / redo |
| V C A L B O H R G P N T | Editor tools |
| Delete, Ctrl+D, F2 | Delete, duplicate, change the words of the selected mark |
| Arrow keys | Nudge the selected mark (Shift: ten pixels) |
| Space, I / O, Home / End | Play, set start / end, jump, in video review |
| Left / Right | Step 0.1 s (Shift: 1 s) in video review |

## Build on Arch

Dependencies: `base-devel cmake ninja pkgconf qt6-base qt6-declarative
qt6-multimedia qt6-wayland layer-shell-qt wayland wayland-protocols
wl-clipboard ffmpeg gpu-screen-recorder libpulse procps-ng xdg-utils`.
`libnotify` is optional; without it there is no notification after a quick
screenshot.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j 3
./build/omaframe --studio
```

`cmake --install build --prefix ~/.local` installs a user-local copy. An Arch
package can be built from a clean version tag with
`packaging/build-package.sh`. Use `--working-tree` only for a local test
package. See [RELEASING.md](RELEASING.md) for the release gates.

## Validation

The capture, clipboard and recording tests talk to a Wayland compositor and the
session bus, so run them in a throwaway nested desktop rather than your own
session. With [omabox](https://github.com/diogochaves/omabox), for example:

```sh
omabox run --net isolated -- ctest --test-dir build --output-on-failure
```

The suites cover rendering, redaction, edit ordering, undo/redo, inline label
editing, mark hit testing, clipboard/save equality, metadata removal, drafts,
video trimming and cuts, export naming, copy and trash, recording placement on
one and two displays, the countdown that must leave before capture, stop
shortcut detection and consented setup, and failure cleanup.

See the [interface review](docs/interface-review.md), [recording
validation](docs/recording-validation.md), [capture workflow
validation](docs/capture-workflow-validation.md) and the [release
tracker](docs/release-parity.md). Multi-monitor, fractional-scale, audio,
fullscreen-game and real GPU capture behavior still need checks on real
hardware.

## License

MIT. The native Wayland capture implementation is adapted from Omasnap at
`acfb3b5772ccb041b57ccc76e16f1b72719f37e9`, copyright Tobi Lütke. Its license is
retained in [docs/OMASNAP-LICENSE](docs/OMASNAP-LICENSE). MatteShot and Omaroll
informed the workflow and style direction; this app has its own UI and
renderer.
