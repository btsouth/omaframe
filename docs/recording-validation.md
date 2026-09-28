# Recording validation, 2026-09-26

## Implemented

- Native recording setup and Screenshot/Video capture-bar switch.
- Region or display target, microphone selection, independent desktop/mic toggles, cursor toggle, optional countdown.
- Owned GPU Screen Recorder child process; SIGINT stops that child only. Another running recorder blocks launch.
- Timer/Stop pill below or above the region, otherwise on a second display. Actual Hyprland layer geometry is checked before recording. Missing/overlapping control blocks launch.
- No safe placement requires a keyboard-stop acknowledgement. Display geometry changes stop the recording.
- Saved MP4 is probed and automatically opens in the trim editor. Audio remains intact by default. Optional startup-pop cleanup copies video, mutes the first 400 ms and fades audio in over 50 ms, without loudness normalization.
- Selected microphone uses an explicit source ID. Clean Desktop Microphone is preferred when available. An unavailable saved source requires user selection; there is no default_input fallback.

## Verified at the time

Build succeeded. All three CTest suites passed in omabox (recording, renderer, pipelines), 4.53 seconds. Recording tests cover single/dual-display placement including negative monitor coordinates, explicit audio arguments, process ownership and valid handoff, actual-control absence blocking launch, countdown cancellation, and decoded audio silence followed by retained signal after cleanup.

Inspected recording setup, region mode, keyboard-only acknowledgement, recording pill, and completed clip editor in the isolated 1600x1000 desktop. The region was x250,y180,900x520; the compositor placed Stop at x559,y716,280x56, outside the capture ending at y700. Clicking Stop completed the simulated recording and opened the clip. The separate --stop-recording IPC command also returned success for an active simulated full-display recording.

A simulated backend supplied a known video file in UI tests. These checks prove controller/UI behavior, not hardware capture or microphone quality. Latest screenshot selector mapping measurements: 233, 171, 197 ms. Escape cancellation passed all three trials after moving Escape handling to a window shortcut.

## Local integration

Alt+Print now attempts Omaframe stop, then legacy recorder stop, then opens Omaframe recording setup. Plain Print remains screenshot capture. Omarchy's Screenrecord submenu has a Record with Omaframe entry, and its Stop row tries the owned controller first. Legacy recording/webcam rows remain available. Hyprland reload/configerrors were clean and live bindings showed both Omaframe actions. Menu rendering was not checked on the real desktop.

Backups of bindings.lua and omarchy-menu.jsonc use suffix .bak.omaframe-record-20260926-000437. No packaged Omarchy files were modified.

## Owner acceptance still needed

Record a region with desktop and Clean Desktop Microphone audio. Click Stop and check first/last frames, speech level, sync, and saved MP4 playback. Repeat on the second physical monitor and for a full display, stopping from the Omarchy bar or Alt+Print. Real GPU capture, mixed/fractional-scale coordinates, HDR, device disconnect, long sessions, and physical audio quality are not established by omabox tests.

Pause, camera integration, and separate audio tracks are not implemented. A full capture on the only display cannot have a visible Stop control without recording it, so this build uses the acknowledged hotkey fallback.

## Screenshot while recording, 2026-09-27

Omaframe now accepts Print Screen screenshot captures while its own recording is active. Finishing or cancelling the screenshot leaves the recorder running and keeps its Stop path available. The editor controls remain usable during recording.

An isolated 1920×1080 omabox run used a simulated recorder that wrote a valid MP4. It covered recording, screenshot selection, editor entry, finish change, PNG save, a second capture and cancellation, then `--stop-recording` and MP4 editor handoff. The recorder process stayed alive through both screenshots. Recording tests passed 11/11 and pipeline tests passed 16/16 in the box. Real GPU capture during a concurrent screenshot still needs a physical check.

## First-frame readiness, 2026-09-27

The recorder now requests GPU Screen Recorder's first-frame timestamp sidecar.
The UI enters Recording only after that timestamp appears; an MP4 header or a
running process alone no longer counts. If no frame arrives within 12 seconds,
Omaframe stops its own recorder and reports the failed output. On stop it also
checks that FFmpeg can decode a frame before opening the clip. The timestamp
sidecar is removed after use.

The isolated test backend reproduced an 88-byte, header-only file and confirmed
that it never enters Recording or opens the editor. The full five-suite CTest
run passed in omabox. Real GPU capture and the reported screenshot-during-video
case still need the owner's physical check.

## Recorder version gate, 2026-09-27

Omaframe now rejects GPU Screen Recorder older than 6.1.3 before creating a
recording file. A test backend reporting 6.1.2 was rejected before launch.
The Arch package declares `gpu-screen-recorder>=6.1.3`. The locally installed
6.1.3 advertises `-write-first-frame-ts`; this is a conservative supported
minimum, not a claim that older versions lack the flag.

## Region setup usability, 2026-09-27

A real-desktop attempt was stopped before any recording was made. The owner
reported that the fullscreen recording setup blocked input and remained over
the content during region selection. No physical GPU or audio result was
established.

The setup is now a centered 680×650 maximum surface with on-demand keyboard
focus. Selecting an area hides it before the selector opens. A valid selected
region starts recording directly with the options already chosen, and the
quick screenshot state is cleared so Print Screen remains available during
recording. The setup returns only if a required option or safe Stop path needs
attention, or if recording fails. In isolated omabox at 1366×768, the compact
setup and unobstructed selector were visually checked. All five CTest suites
passed, including the region-to-recording transition with a simulated recorder.
The owner subsequently reported a playable real recording that was trimmed and
exported. This checks the basic capture and review path, but does not establish
audio quality, both monitors, or a repeat of the earlier 88-byte scenario.

The earlier setup had a translucent border outside the panel. The panel now
fills its layer surface, without the extra translucent margin. The 980×730
isolated omabox check showed the whole panel and its microphone options without
clipping. The new recording review closes after a successful export, while
manual video editing, cancelled exports, and failed exports remain open.

## Middle-section cuts, 2026-09-27

The video editor now accepts multiple time ranges to remove, shows them on the
filmstrip, skips them during playback, and lets users edit, delete, undo, and
redo them. The displayed clip length includes removed sections. Export joins
kept video and each audio track into one MP4 while leaving the original alone.
The pipeline test removed two intervals from a six-second fixture, confirmed
the 3.3-second result with both audio tracks, decoded the output, and checked
source-byte equality. A mute export and a fully removed clip were also checked.
In isolated omabox, a typed 2–3 second cut from an eight-second video previewed
as a skip and exported a playable seven-second MP4. This is not a physical
recording/audio acceptance check.

## Editor presentation pass, 2026-09-27

The first cut editor placed two sets of start/end times on screen and labeled
its confirmation “Remove range.” The editor now keeps the cut controls hidden
until the user chooses “Cut out a section.” In that mode, dragging across the
timeline highlights the unwanted time range, displays its times for adjustment,
and requires a second “Cut out highlighted section” action. The existing cut
is still editable or deletable. Trim times are labeled separately, the final
duration is labeled “Final,” and the audio choice says “Keep sound” or “Muted.”
The screenshot finish screen now says how to choose a style or enter Edit.
Edit opens with Select active so clicking an existing mark edits it instead
of starting an accidental crop. The editor header explains that choice.

In isolated omabox at 980×730, the video editor was checked before selection,
during a 1.9–3.0 second selection, and after the cut. It displayed a 6.9-second
final duration and exported a playable 6.92-second MP4 from an eight-second
sample. The screenshot finish and edit views were also inspected at 980×730.

## Owner recording and export inspection, 2026-09-27

The latest owner-created source was an 8.13-second, 1506×1152, 60 fps H.264
recording. After a middle cut and trim, Omaframe exported a 3.90-second MP4
with 234 frames. FFprobe read its duration and dimensions, and FFmpeg decoded
the entire exported video without error. Frame timestamps were evenly spaced
at 1/60 second. Contact-sheet and first/last-frame inspection showed a normal
scroll jump at the cut, with no visible black flash or corrupt frame. The
captured page occupies the center of the frame with broad dark side margins;
a tighter region would present this example better when shared.

The source and export contain no audio stream, so this run cannot establish
desktop or microphone recording quality. The files do not establish which
physical monitor was used, and screenshot editing during this exact recording
was not independently observed. Two earlier owner-created recording/export
pairs from the same day are also playable and have no audio tracks. These
results confirm repeatable silent real capture and cut/trim export; the
physical audio, second-monitor, and concurrent-screenshot gates remain open.

The editor now probes the completed export and decodes its first frame before
renaming the temporary file or reporting success. An encoder that exits zero
after writing an invalid tiny file is rejected. The five CTest suites passed
in omabox after this change, including the new invalid-export regression test.

## Silent recording setup, 2026-09-27

Recording setup now states “Video will be silent” beside the audio switches
when both are off. The notice disappears when desktop audio is enabled. This
was visually checked in omabox at 980×730 and 1366×768 without clipping.
The video editor labels the brief post-export verification stage “Checking
clip” instead of leaving an “Exporting clip” label at 99%.
Setup status now updates when audio switches change. An unavailable desktop
audio warning clears when desktop audio is turned off, and starting a new
setup requires a fresh target selection. The switch and warning transition
were visually checked in omabox at 980×730; a recording regression test covers
the unavailable-sink case.

An attempted second virtual display in the local NVIDIA omabox appeared as
0×0, so it could not validate live second-display capture. The geometry and
fractional-scale fixtures still pass, and physical second-monitor acceptance
remains open.

## Owner second-monitor recording with audio, 2026-09-27

The owner reported recording on the second physical monitor. The saved source
is a 5.515-second, 1190×876 H.264 MP4 at 60 fps with one stereo AAC track.
The exported MP4 is also 5.515 seconds. Both files decoded completely without
FFmpeg errors. The export has 327 video frames with regular 1/60-second frame
timestamps, and its encoded video and audio packets match the source. Sampled
frames show the expected desktop content without visible corruption.

The audio is not silent: measured mean level was -25.4 dB and peak was
-10.3 dB, with no sustained interval below -45 dB detected. Audio packet
timestamps run from 0.011 to 5.494 seconds without an unusual gap. This
establishes a usable recorded audio signal and preserved audio on export.
The single mixed track does not reveal whether desktop and microphone sources
were both enabled or how they sounded to the owner. The MP4 itself does not
identify the display; second-monitor provenance comes from the owner's report.
This run was a region capture. Full-display stopping, concurrent screenshot
editing, and a fresh-system capture remain release checks.

## False 12-second stop, 2026-09-27

The owner reported repeated automatic stops after about 11 seconds. Three
newly saved recordings were valid, with roughly 11.6 seconds and 696–700
video frames each. One observed live first-frame timestamp sidecar was 66
bytes long. GPU Screen Recorder's documented sidecar has a header line and a
second line of numeric timestamps; Omaframe had tried to parse the whole file
as exactly two numbers. It therefore waited for a marker it had already
received and stopped the working recorder at its 12-second timeout.

The readiness check now parses the final timestamp line. The simulated
recorder test writes the documented two-line format, and all five CTest suites
pass in omabox. The header-only, 88-byte recording regression still requires
a real first-frame marker and stays blocked. After installation, the owner
recorded a 32.75-second, 1,965-frame clip on the real GPU. It passed the
former timeout point and exported successfully, confirming that this false
stop no longer occurs in that workflow.

## Faster clip export, 2026-09-27

An owner-created 32.75-second, 2524×1246, 60 fps recording was trimmed to
31.05 seconds by removing about 1.7 seconds from the start. The old export
re-encoded all 1,863 kept frames and took about 10 seconds by the owner's
observation. A matching offline FFmpeg run with the faster veryfast/CRF 16
setting took 5.96 seconds. It kept the same duration and frame count and
decoded fully; its 20.7 MB file was about 15% larger than the previous
18.0 MB export. Full-clip luma SSIM against the source differed by less than
0.0001 from the previous export. The editor uses that setting for start trims
and middle cuts now.

When a compatible H.264 MP4 keeps the first frame, shortening only the end or
removing audio now copies the original video packets instead of re-encoding.
The same 31.05-second test range copied in 0.06 seconds and decoded fully.
The pipeline test confirms exact encoded video packet prefixes and both-audio
and muted results. Start trims still re-encode so removed frames are not
retained as hidden MP4 preroll. Physical playback and timing on the faster
installed build remain to be observed.

## Visible recording control, 2026-09-27

The compact timer and Stop control now stays visible for full-display capture.
If another display is available, the control moves there and is outside the
recorded display. On one display, it appears in a corner of the recording;
setup states that it will be included in the video. Omarchy's Alt+Print
shortcut remains another way to stop. The control is checked in Hyprland's
layer list before recording starts.

The recording suite passed 19 tests in omabox, including single-display
placement, a simulated two-display recording with a control on the other
display, and a missing-control failure. An isolated 1280×800 UI check showed
the 232×48 control during countdown. GPU Screen Recorder cannot capture the
virtual omabox display with this machine's graphics driver, so the actual
pixel exclusion and behavior over a physical fullscreen game remain owner
acceptance checks.

## Recording control kept out of every video, 2026-09-27

GPU Screen Recorder records a display or region through KMS capture, which
reads the composited framebuffer. Every layer on the recorded display is in
the video, including overlay surfaces and the Omarchy bar. Hyprland's
`no_screen_share` rule does not apply to KMS capture and draws a black box
where it does apply. So the only capture-free places for a control are outside
a region on the same display, or on another display.

`Recording::placeStop` now:

- places the control beside a region, trying below, above, right, then left,
  inside the display's usable area (Hyprland's `reserved` space for the bar is
  excluded, so the control no longer covers the bar);
- otherwise uses the nearest other display, at the edge that faces the
  capture, aligned with the capture's top below that display's bar;
- otherwise returns nothing. There is no fallback inside the video.

With no control, a 420 px countdown pill sits below the bar on the recorded
display and says how to stop. It is hidden before capture. Launch then checks
Hyprland's layer list, up to twelve times over about a second, and refuses to
start while any `omaframe-*` surface intersects the capture. Such a recording
requires a working stop shortcut. Recording options explain why and offer a
one-click Alt+Print setup. Choosing the whole display, by **F**, by clicking
empty desktop, or in options, records the display itself (`-w NAME`) at native
resolution instead of a same-sized region.

Automated coverage in the recording suite: ten placement fixtures (below,
above, tall regions to the right and left, single display with no control,
near-full single display, left, right and below neighbors, a region too large
for its display), countdown placement, launch refusal while the countdown is
still reported over the display, refusal when a control is reported over the
area, a whole-display recording with no control, and the dual-display case
with the control at the facing edge. The stub reports layer coordinates in
global space, as Hyprland does; the check also accepts display-local values.

In omabox with a stub recorder (not GPU capture):

- A 600×300 region at 500,300 on 1920×1080 put the control at 683,616, below
  the area.
- A whole display on the single virtual monitor showed "Recording in 3 ·
  Alt+Print stops it" at 749,40, then no Omaframe layer while recording. The
  recorder received `-w WAYLAND-1`.
- With a fullscreen window on the display, the bar was hidden and the overlay
  Stop control stayed visible above the window, outside the area. Stopping
  opened review, which Hyprland made fullscreen in place of the game window.
- In a box with Omarchy's stock bar, the bar's recording icon appeared while
  Omaframe recorded. Clicking it ran `omarchy-capture-screenrecording
  --stop-recording`, which sent SIGINT; Omaframe finished and opened review.
  Omarchy's script also posted its own "Screen recording saved" notification
  without a file, because it did not start the recording.

Omabox has one virtual display and no GPU capture, so none of this proves the
pixels of a real video or the second-monitor placement on physical displays.

## Capture-bar recording and consented shortcuts, 2026-09-27

Tab in the screenshot selector now stays in the selector. Video mode adds
Sound, Mic, countdown and an options button to the bar and loads audio and
display state in the background. A drag or window click starts recording
straight away; a choice made before loading finishes is applied when it does.
`omaframe --record` (Alt+Print) opens the selector in video mode. The old
centered setup is now "Recording options", opened from the bar or when a
recording needs attention, with one primary action and a height that fits.

Recording setup no longer edits Hyprland bindings on its own. `ShortcutSetup`
reports what Print and Alt+Print do and binds them only on request, from the
start screen, Settings or recording options. It replaces only Omarchy's stock
"Screenshot" and "Screenrecording" actions, backs up `bindings.lua`, migrates
the previous `omaframe:recording-shortcut` block, activates the keys with
`hyprctl eval`, verifies them, and restores the file if they do not appear.
Any key bound to `omaframe --record` or `--stop-recording` counts as a stop
key and is named in the UI.

In omabox, setup replaced the box's stock bindings live and Print and
Alt+Print then drove capture, recording and stop. Omabox's own Hyprland config
does not load the user's `bindings.lua`, so a theme switch there reverted to
Omarchy's defaults. On a normal Omarchy config, `hyprland.lua` requires
`hypr.bindings` after the defaults and the bootstrap clears cached `hypr.*`
modules on reload, so the block should survive; that needs a real-desktop
check.

## Recovery, 2026-09-27

The recorder child now gets `PR_SET_PDEATHSIG` with SIGINT. In omabox, killing
Omaframe with SIGKILL during a stub recording made the recorder exit instead of
staying orphaned. A start or stop that produces no readable video, including
the 88-byte header-only case, now removes files under 64 KiB and says no file
was kept.

## Review after export, 2026-09-27

Saving an edited recording no longer closes review or quits. The clip is named
`<recording>-edited.mp4` (then `-edited-2`, and so on) beside the original. The
review shows the name, length and size, with Copy file (a `text/uri-list`
clipboard entry), Show file, Trash original (`QFile::moveToTrash`) and Done.
Done returns to the studio when the recording started there. Cuts are
modeless: drag across the filmstrip to select, then Remove this part; click a
removed part to edit or restore it; the ruler scrubs. Undo covers trims, cuts
and sound. The pipeline suite covers naming, the second export, the clipboard
entry and moving the source to the Trash.

## Review ends with the clipboard, 2026-09-27

Owner follow-up: exporting from recording review should close the window and
put the video on the clipboard. Review now ends like a quick screenshot.
**Copy and close** (no changes) and **Save and copy** (after an edit) put the
file on the clipboard as a `text/uri-list` entry, close review or return to the
studio when the recording started there, and post "Video copied" or "Edited
video copied" with the save folder. A failed export or copy keeps review open
with the error. Videos opened from the studio still stay open after export.
The Trash original action was removed with the in-window success state.

In omabox with a stub recorder: an unchanged recording closed on Copy and
close with the recording's file URL on the clipboard; a recording trimmed to
8 s closed on Save and copy with `…-edited.mp4` on the clipboard; a video
opened with the studio stayed open after export with Copy file and Show file.
The notification omits its icon when the app icon is not installed, so
daemons do not show a placeholder. The pipeline suite checks the clipboard
entry for both the edited clip and an unchanged recording.

## Real desktop run, 2026-09-27 (agent-driven with the owner's approval)

Hardware: DP-2, Dell S2721DGF, 2560×1440 at 165 Hz, left at 0,0; HDMI-A-1,
Dell D2719HGF, 1920×1080 at 144 Hz, right at 2048,0. Both at scale 1.25.
NVIDIA, Hyprland 0.56.2 with Omarchy's Lua config (direct scanout disabled by
the owner's config), GPU Screen Recorder 6.1.3. Input was driven with
`hyprctl` cursor moves, `ydotool` for mouse buttons and hotkeys, and `wtype`
for keys sent to Omaframe's own surfaces. Each recording was checked by
sampling ten frames (0, 0.05, 0.1, 0.25, 0.5, 1 and 2 s, middle, end) for the
longest horizontal run of the control's border color (255,83,69). The same
check on a screenshot with the control visible finds a 290 px run.

| Case | Result |
| --- | --- |
| Area 800×450 at 300,250 on DP-2 | Stop at 583,716, below the area. Video 1000×562 at 60 fps. Longest border-colored run 0 px in every sample. Copy and close put the file URL on the clipboard and exited. |
| Whole DP-2, HDMI connected | Recorder got `-w DP-2`. Stop at 2064,40: HDMI's left edge, next to DP-2, below its bar. Video 2560×1440; first frame shows no selector or control; longest run 10 px (a small icon in the recorded UI). Stopped by clicking the bar's recording icon. |
| Whole HDMI (by accident, see below) | Stop at 1800,40: DP-2's right edge, next to HDMI. Video 1920×1080, clean. Stopped with Alt+Print. |
| Fullscreen test pattern on DP-2, recording DP-2 | Stop stayed visible on HDMI while the fullscreen window covered DP-2. Alt+Print stopped it while the fullscreen window had focus. Review then took over fullscreen (Hyprland policy). Video shows only the pattern. |
| Screenshot, then screenshot with an edited label, during an area recording | Both saved; the recorder kept running; the Stop control stayed; clicking Stop finished a 40.6 s video that decodes completely. No 88-byte file. |
| Sound and sync | Computer sound on, a 12 s clip with a white flash and a 1 kHz beep each second played fullscreen. 12 of 12 flashes and beeps matched. Audio led video by a median 25.8 ms (spread 54 ms including mpv's start), with no drift. |
| Single monitor (HDMI disabled with `hl.monitor`) | The countdown pill appeared at 813,40: "Recording in 3 · Alt+Print stops it". During the recording there was no Omaframe layer at all, and the bar showed its recording icon. Alt+Print stopped it. The first 60 frames have zero pill-colored pixels where the pill was; all ten samples are clean. HDMI was restored (it needs `disabled = false` in the same call). |
| Theme switch and back | After `omarchy-theme-set` to Tokyo Night and back to Osaka Jade, Print and Alt+Print still ran Omaframe. These are the owner's own binding lines, loaded the same way as Omaframe's block. |

Found and fixed during the run:

- **The Omarchy bar could not see Omaframe's recorder.** QProcess started it as
  `/usr/bin/gpu-screen-recorder`, and both the stock indicator and the owner's
  indicator match `^gpu-screen-recorder`. So there was no bar icon, and
  Omarchy's stop could not reach it. The recorder now starts through
  `bash -c 'exec -a gpu-screen-recorder …'`, keeping one process for its PID,
  SIGINT and the parent-death signal. After the fix the bar icon appeared and
  clicking it stopped the recording.
- **F picked whichever display had keyboard focus**, not the one under the
  pointer. With the pointer on DP-2 it recorded HDMI. F now uses the pointer's
  display, read from `hyprctl cursorpos` when the selector opens and updated
  as the pointer moves.
- The floated review was sized for the capture's display rather than the one
  Hyprland opened it on. It now sizes itself from the monitor it lands on.
- A whole-display screenshot was named "Region capture". It is now "Display
  capture".

`wtype` does not trigger Hyprland keybindings; `ydotool` (a uinput keyboard)
does. That is a test-tooling detail, not a product issue.

Long recording and export, same session:

- A 800×450 area on DP-2 recorded for 10:09.4 (36,558 frames at 60 fps, AAC
  stereo). It decodes completely; video and audio streams differ in length by
  0.09 s. Omarchy's screensaver started during the recording and was
  recorded; the recorder was unaffected.
- **Found and fixed:** review opened while the screensaver's special
  workspace was shown and stayed hidden there afterwards, fullscreen. Floating
  a review or quick editor now moves it off a special workspace to the
  display's active workspace and focuses it.
- A 1 s start trim forced a full re-encode of the 10-minute clip. Encoding
  took about 19 s (file created 23:08:34, finished 23:08:53); Save and copy
  finished, including the export checks, and closed review in about 33 s.
  The 608.4 s result decodes completely, its streams agree within 0.09 s,
  and its file URL was on the clipboard.

After the fixes, the packaged binary picked the display under the pointer for
F on both monitors: 1920×1080 with the pointer on HDMI, 2560×1440 on DP-2.

## Code review fixes, 2026-09-27

A focused review of the riskiest paths in the uncommitted change (recorder
states, bindings editing, file deletion, the review flow and QML references),
done in the main session after the real-desktop run, found and fixed:

- **Stopping a recording mid-screenshot stranded the app.** The recorder's
  hide request hid every Omaframe surface. Stopping while a screenshot
  selector was open cleared it without cancelling, so the studio stayed busy
  and refused new captures until restart, and an open screenshot edit was
  replaced by the review. The recorder now hides only its own surfaces. A
  recording that completes during a screenshot opens its review after that
  screenshot is saved or cancelled, with a notification in between. Checked in
  omabox with a stub recorder: stop with the selector open, F, 9, then the
  screenshot notification and the review; Copy and close exited cleanly.
- **A bindings block without its end marker** would have been removed up to
  the end of `bindings.lua`. Setup now refuses and leaves the file unchanged.
  Covered by the recording suite.
- **Clicking a trim handle** without moving it added an empty undo step.

Also rechecked: the parent-death signal still reaches the recorder through
the `bash exec` wrapper (SIGKILL of Omaframe in omabox made the recorder
exit), and `/usr/bin/gpu-screen-recorder` has no file capabilities, so exec
keeps that signal. Every `studio.`, `video.`, `recorder.` and `shortcuts.`
reference in the QML resolves to a declared property or method. `qmllint`
reports only layout-positioning style warnings.
