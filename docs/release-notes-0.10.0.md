# Omaframe 0.10.0

Omaframe now takes delayed screenshots, keeps a capture history, snaps marks
with Shift, and shows microphone and computer sound levels.

- **Delay timer.** Press Shift+Print, or the Delay button (3, 5 or 10
  seconds), or press T in the selector to start a countdown, so menus,
  tooltips and hover states stay open. `omaframe --delay N` (0 to 30 seconds)
  does the same from a key or script. A small badge shows "Screenshot in N"
  with Cancel. The last choice is remembered, and plain Print Screen stays
  immediate.
- **Capture history.** Press Ctrl+H, run `omaframe --history`, or use the
  launcher to list saved screenshots, recordings, exports and drafts, by day.
  Each row can copy, open in the editor, reveal, resume or move a file to the
  Trash. History only lists files you saved and keeps no extra copies.
- **Angle snapping.** Hold Shift while drawing to snap arrows and lines to 45
  degree steps and turn boxes, highlights, redactions and blurs into squares
  and ovals into circles. A small readout shows while Shift is held, and it is
  never exported.
- **Sound meters.** In video mode the Sound and Mic buttons show a live level
  before and during a recording, and Options shows device names, levels, peaks
  and clip warnings. The meters read the exact inputs handed to the recorder.

**Open** now uses your system file chooser, the same one your other apps show,
and starts in your screenshot folder or the last folder you opened from.
Without a desktop portal, a larger built-in dialog opens instead.

Recordings are written to a hidden file until they finish. A failed or
force-stopped recording that is kept shows as Incomplete, and a recording left
over from a crash is recovered as Incomplete on the next start.

## Fixes

- Clicking Copy and save while finishing a label no longer does nothing.
- Pasted and duplicated marks stay inside the crop.
- Long pen strokes are resampled, so nothing after point 2048 is lost.
- Hiding a secret needs a full redaction; partial covers and blur no longer
  count.
- If a draft cannot be saved, the work is kept open with retry or discard.
- A stuck recorder can be force-stopped after 15 seconds, keeping the partial
  file, and recorder failures use plain messages instead of raw log output.
- Export ffmpeg stops with Omaframe, and abandoned export temp files are
  cleaned up on start.
- Losing the camera during the countdown is reported, and a saved camera that
  is not connected says so instead of switching.
- Scrolling capture keeps at most 400 MiB of retained frames.
- Turning on another shortcut never regenerates a key you bound yourself.
- WebP support is added through qt6-imageformats.

Thanks to @sonlndv for selecting a box by clicking inside it (#27).

Validated with all seventeen headless suites, a real PulseAudio test and the
OCR pattern tests, and on a two monitor Omarchy desktop at 1.25 scale with a
real microphone. Bluetooth headsets and physical cameras were not tested.

Update through `omarchy update` or `sudo pacman -Syu`, then reopen Omaframe.
Existing captures, settings and drafts are preserved.
