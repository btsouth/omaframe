# Capture workflow validation

Date: 2026-09-25. Build: 0.2.0 preview. Local, unpublished checkout.

## Behavior

Default launch and `--capture` start region selection with the studio hidden. Each connected display is frozen before any selector opens. Each selector uses its own output name and normalized local coordinates, preserving native pixel resolution. A region stays within one display.

The chooser has eight finishes plus Raw, with the actual capture in every card. Click or 1–9 accepts, saves, copies, and exits. Enter accepts the remembered finish. E opens the editor, Escape returns with edits intact, R retakes, and Escape from the chooser/selector cancels. `--studio` retains the full media editor. Files open directly into that editor.

A per-user, per-Wayland-session local socket prevents duplicate capture sessions. Save/copy failures retain the capture and show the error. Successful acceptance leaves the clipboard available after the app exits.

Adaptive now groups dominant hues, includes fully saturated highlights, downweights neutral UI and near-black noise, and keeps both gradient stops within the captured hue family. Neutral captures receive a neutral frame.

## Automated validation

CMake/Ninja build, GCC 16.2.1 and Qt 6.11.2. Run in an isolated desktop:

```sh
omabox run --net isolated -- ctest --test-dir build --output-on-failure
```

Both suites pass, 2/2 in 3.90 seconds on the final build. New coverage includes bright green, forest green, red near the hue wrap, gold, neutral images with tiny accents, second-output pixel routing with different native resolutions, reversed/invalid regions, atomic capture failure, editor/chooser edit persistence, accepting once during pending preview work, save failure and retry, and cancellation. Existing image, clipboard, native capture, and video pipeline checks remain included.

## Observed UI checks

All checks ran inside omabox with isolated networking, without touching the real desktop.

- Chooser inspected at 1600×1000 and 1280×800. Every card, the editor entry, and the footer fit.
- Native region and full-display capture, E to editor, Escape back, and redaction surviving into every finish preview.
- Number-key acceptance and mouse-card acceptance save and copy, dismiss all windows, and exit the process.
- A second launch while choosing does not replace the capture or create another persistent app instance.
- Clipboard PNG bytes match the saved PNG after the process exits.
- Escape during region selection exits without changing the previous clipboard PNG.
- Adaptive inspected against the same green wallpaper as the reported issue.
- No QML runtime errors in the app logs. The isolated desktop reports expected unavailable audio/accessibility/portal services.

Screenshots are under ignored `evidence/`: `chooser-final.png`, `chooser-1280.png`, `redact-editor.png`, and `redact-chooser.png`. The pre-change source snapshot is `evidence/baseline-v0.1-source.tar.gz`.

Pre-latency-fix binary SHA-256: `d36503e65812a530e74d67401bd5bbd943364294f7e10928e3e39e95eebc105f`.

## Remaining physical acceptance

The box exposes one usable virtual display. An attempted additional headless output had zero pixel dimensions and was removed; it does not count as multi-monitor validation. Automated tests validate per-output routing, but physical dual-monitor selection, mixed scaling, rotated outputs, and HDR/color management still need an owner-run check.

Suggested check: launch capture while focused on the first display, move to the second, select a region, press 5, and paste. Repeat in the other direction, then try E, a crop, Escape, and Enter. Check that the saved PNG has the expected native dimensions.

The initial validation made no system install, shortcut replacement, recorder change, commit, or publication. The owner subsequently requested the real shortcut change: Print Screen now points to this checkout's `build/omaframe --capture`, with the previous binding backed up. Alt+Print remains the existing recording shortcut.


## Startup latency follow-up

The owner observed roughly three seconds before region selection on the real desktop. The old executable initialized the full editor, video player, and sample previews before capture, then waited 220 ms even with no visible window.

The shortcut path now skips the sample entirely, creates only the selection UI first, loads the finish chooser after selection, and loads the editor on demand. Video playback is loaded only for an opened recording. The 220 ms dismissal delay remains for retakes or capture from a visible app window, preventing self-capture during dismissal animations; fresh launches have no artificial delay.

Three process-launch-to-selector-mapping samples in the same 1920×1080 omabox were 919, 802, and 793 ms before the change. Initial revised measurements were 248, 162, and 152 ms. Final-build repeat measurements were 176, 161, and 152 ms. Qt's first-frame presentation signal reported 206, 205, and 194 ms on the final repeat. This is not a physical two-monitor latency measurement, and does not establish the owner's observed three-second delay as fully resolved on that hardware.

Reproduce inside omabox with `python tests/manual/capture-latency.py ./build/omaframe`. Optional `OMAFRAME_PROFILE_STARTUP=1` logs application initialization, capture request, captured pixels, and first selector frame. The script starts three processes, measures the compositor layer, and cancels each with Escape.

Both automated suites pass after the change. Native selection, chooser, deferred editor opening, return and acceptance were checked in the box. A generated MP4 opens with the deferred video player and correct trim range. The existing real Print Screen binding already points to the rebuilt executable; no new binding or background service is required.

## Screenshot editor parity slice, 2026-09-27

The editor now has line, box, and oval tools, plus Select to move, resize, or
delete marks. Undo/redo includes those changes. Crop is a reversible frame over
the original image: it can be redrawn while viewing the full image or cleared
without discarding annotations. Annotations stay in original-image coordinates
when the frame changes. Select now exposes Delete, label editing, color, and
size controls. Text and step marks have resize handles; labels can be rewritten.
The color and size controls apply to text, steps, arrows, lines, boxes, and
ovals. Opaque redaction stays fixed.

All five current CTest suites pass in isolated omabox. Renderer tests cover
shape outlines and crop/annotation coordinates; the pipeline test covers
move, resize, delete, undo, and clear crop. In the 1920×1080 box, a box was
drawn, moved, reframed, selected, resized, deleted, and restored with undo.
At 1366×768 the expanded toolbar and Clear crop action remained visible. An
oval plus crop were saved from the final build; the PNG showed the full-size
oval at the expected framed position, and clipboard PNG bytes matched the
saved file exactly. No QML runtime errors appeared in the app log.

The follow-up pipeline checks label re-editing, movement, resize, color, undo,
and step resize/delete. In a 1366×768 isolated desktop, an existing label was
rewritten, recolored, enlarged by its handle, moved, deleted, and restored with
undo. The Select and property controls remained visible and the app log had no
QML runtime errors. Owner acceptance remains needed on actual captured windows
and both physical displays, especially fractional scale. OCR, scrolling
capture, pins, and persistent edit projects remain outside this slice.

## Text and selection follow-up, 2026-09-27

The editing controls moved to a side panel, leaving a larger image on screen.
Selected objects show their properties first, with the tool list below. Text
starts at a readable size for the source image and has a direct 8–4096 px
control and presets. The editor accepts multiple lines and wraps long labels.
It limits the requested font size if the label would extend beyond the image,
with a status message explaining the adjustment. Text has caption-box and
shadow styles, alignment, independent text and box colors, and box opacity. Four
corner handles enlarge or shrink text; a dashed outline and pixel readout show
the proposed size while dragging. Small labels use one compact handle so the
text remains grabbable for movement. Arrow keys nudge the selection by one
source pixel, or ten with Shift.

Freehand pen and blur now join the annotation tools. Pen strokes can be
selected to change color or size, move, resize, delete, undo, and redo. Blur
regions use a two-pass softening filter with an editable strength. Both were
checked visually in the isolated editor on the sample screenshot.

The next editor pass added duplicate and one-step layer order controls for
selected annotations. Undo and redo restore the selected mark. Text, shapes,
and pen strokes accept a custom six-digit hex color; text caption boxes have
their own custom color. The isolated 980×730 editor showed duplication and
layer movement, and text and box hex fields updated both the preview and their
displayed values after selecting a preset.

The current isolated CTest suite passes 5/5. At 1366×768, the isolated desktop
showed the larger canvas, multiline label, 192 px and corner-resized labels,
caption/shadow controls, and a long label fitted to the source height. At
980×730, the inspector and alignment controls remained usable with scrolling.
A 192 px label exported as a 1360×880 PNG; its saved bytes matched the copied
PNG. Physical capture acceptance and visual checks on real screenshots are
still needed before a release claim.

## Editor, start screen and settings pass, 2026-09-27

The text dialog is gone. Labels are typed on the image in their own font size,
colors and caption style; the rendered copy is hidden while typing and
replaced on commit. Enter adds a line, Esc or a click outside finishes, and
typed words are applied even while a preview is still rendering. Text commits
leave the Text tool armed.

Marks are now editable with any tool. The selected mark keeps its outline and
handles in every tool except Crop. Drawing tools pick up filled areas only by
their edge, so a new highlight or redaction can start inside an old one.
Hovering a mark shows a dashed outline and a move cursor; right-click selects.
Esc peels one layer: typing, drag, selection, tool, then Edit. Tools sit at
the top of the sidebar with their keys and never move; the selected mark's
settings follow them. Font size, sliders and confirmations use themed
controls. Finish thumbnails are skipped while the editor is open.

The studio opens on a start screen instead of a sample image: Screenshot,
Record video and Open a file, a first-run explanation, the shortcut panel, and
Recent edits with previews. The sample is optional. Settings holds shortcuts,
both save folders, the quick-screenshot notification, and the private
originals option, which is now off by default. A capture started from the
studio returns to it after finishing or cancelling instead of quitting. The
quick editor and recording review float at up to 1560×1020 on their display
instead of taking a half-width tile. The desktop entry opens the window.

Checked in omabox at 1920×1080 and 1366×768, in the box's default dark theme and
Catppuccin Latte: first-run setup, Print through the new binding, window
click, finish picker, quick editor, inline label create, move, re-edit and
handle resize, arrow drawn and dragged by its line, Esc ladder back to the
picker, finish 2 saved with a notification and matching clipboard bytes, no
private original written, settings, Recent edits, and video review. No QML
errors appeared in the logs. Real captures on both physical monitors remain
an owner check.

## Real desktop screenshots, 2026-09-27

On the owner's two displays at scale 1.25 (DP-2 2560×1440 left, HDMI-A-1
1920×1080 right), driven by the agent with approval:

- Whole DP-2 with F: 2560×1440 PNG; clipboard bytes equal the file.
- Whole HDMI with F: the finish picker opened on HDMI and reported 1920×1080.
  E floated the editor on HDMI at 1321×760. A label typed on the image matched
  its committed rendering, and Ctrl+C saved a 1920×1080 PNG with the label;
  clipboard bytes equal the file.
- A window click on HDMI (a 1522×826 logical window, 1902.5×1032.5 physical)
  saved 1904×1034.
- A 600×400 logical drag on DP-2 saved 751×500.

Each capture exited the process after saving. F now captures the display
under the pointer; before that fix it used the display whose selector had
keyboard focus.

## Possible secrets and copy text, 2026-09-28

Reading runs `tesseract --psm 6` with TSV output at nice 10, one thread per
process. Captures up to about 4.2 megapixels are doubled first: at 1x, 15 px
screen text lost dots in IP addresses and ran card digit groups together.
Tall images are read in up to four overlapping bands side by side (half the
cores at most). Automatic layout (`--psm 3`) was dropped because it read
aligned terminal output column by column and skipped lines of dashes such as
a private key's header.

`Ocr::read` on this 28-core machine, worst cases filled edge to edge with 15 px
monospace text, and one ordinary UI screenshot:

| Capture | Before bands | With bands |
| --- | --- | --- |
| 1920×1080, full of text | 11.6 s | 4.9 s |
| 2560×1440, full of text | 20.0 s | 7.7 s |
| 3840×2160, 28 px text (scale 2) | 14.1 s | 5.7 s |
| 3840×2160, 15 px text (scale 1) | 31.0 s | 10.5 s |
| 1416×952 Omaframe window | 1.3 s | 0.8 s |

Matching the words for secrets took 0 to 16 ms in every case.

The picker never waits for this. In a 1920×1080 omabox with a terminal full of
text, three `--screen` launches with tesseract on the PATH took 345, 290 and
312 ms to show the picker and 108 to 110 ms from Enter to the saved PNG.
Without tesseract: 304, 297 and 266 ms, and 108 ms. No tesseract process was
left after the app quit. A dense full screen can take several seconds before
the Hide button appears; a region or a normal window is under a second.

Checked in omabox: a terminal showing a GitHub token, an AWS key, an email and
a public IP gave "Hide 4 possible secrets" in the picker; H covered all four in
the saved Raw PNG, and the note said "Hid 4 possible secrets. OCR can miss
some, so check before sharing." T put the terminal's text on the clipboard. In
the editor the same capture showed "Hide 8 possible secrets" (two terminals),
and Shift+H redacted them in one undo step.

## Repeat on a display that is off, 2026-09-28

Follow-up to #1. `--repeat` whose last area is on a display Hyprland reports
with `dpmsStatus: false` now opens a new selection on the displays that are on,
with "That display is off. Select an area again.", instead of waiting for a
frame that never comes. Checked in omabox by writing a last area on WAYLAND-1
into `Omaframe.conf`, turning WAYLAND-1 off with `hl.dsp.dpms`, and running
`--repeat`: the selector opened rather than the cropped picker. A second
headless output stayed 0×0 in the box, so the case with one display on and one
off is covered by the unit test and Diogo's report, not by a box run.
