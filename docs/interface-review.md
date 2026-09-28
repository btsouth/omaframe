# Interface review, 2026-09-27

A full pass over every visible surface of the 0.2 preview, as a new user and
as a daily user. Evidence is from the working tree that matched the installed
binary (`~/.local/bin/omaframe`, SHA-256 `915b0beb…`), run in an isolated
omabox at 1920×1080 with a stub recorder standing in for GPU capture.
Screenshots are under ignored `evidence/review-2026-09-27/before/`.

Priority: **P0** blocks a public release, **P1** is a disruptive
inconsistency that should ship fixed, **P2** can follow.

## Recording

| # | P | Problem | Evidence |
| --- | --- | --- | --- |
| R1 | P0 | On a single display, a full-display recording puts the timer/Stop bar inside the video, top right, and covers the Omarchy bar's status icons. Setup says so, but the owner does not want any Omaframe control in the video. | `of-10-recdisplay.png`, `of-12-recording.png`, layer at 1672,16 over a 24 px bar |
| R2 | P0 | With a second display, the control goes to the far-right top corner of the other display, not the edge next to the recorded display. | `Recording::placeStop` uses `right - width - gap` for every other display |
| R3 | P0 | Region placement only tries below and above the area. A tall region falls through to the other display or into the video. Nothing avoids the bar's reserved strip. | `placeStop` |
| R4 | P0 | Opening recording setup silently edits `~/.config/hypr/bindings.lua` and evaluates Lua in the live compositor. There is no consent or visible onboarding step. | setup reads "Alt+Print is set up…" on first open, `of-09-recsetup.png` |
| R5 | P1 | The fastest path to record is five steps: Print, Tab, a centered setup dialog, "Select area & record", drag. Tab leaves the selector for a dialog that covers the screen. | `Studio::recordInstead` |
| R6 | P1 | Setup has two identical "Select area & record" buttons, two status lines that repeat each other, and about 250 px of empty panel. | `of-09-recsetup.png` |
| R7 | P1 | In a tiled desktop there is rarely empty desktop to click, so a full-display screenshot or recording from the selector is a hidden gesture. Clicking empty space in video mode records the display as a region, not as the display. | `Selection.qml`, `Recorder::regionSelected` |
| R8 | P1 | If Omaframe exits abnormally, its GPU Screen Recorder child keeps running and the MP4 is never finalized. | no parent-death signal on the child |
| R9 | P1 | A failed start leaves a header-only file in the recordings folder and tells the user it was "retained". | `Recorder::validateResult` |
| R10 | P2 | Only Alt+Print is recognized as a stop shortcut. A user who binds another key to `omaframe --record` is told there is no stop path. | `Recording::hasStopShortcut` |

## Video review

| # | P | Problem | Evidence |
| --- | --- | --- | --- |
| V1 | P0 | Exporting from recording review closes the window and quits. There is no confirmation, no file name, and no way to copy or open the result. | `of-17-exported.png`, process gone |
| V2 | P1 | Exports are named `Omaframe-<time>.mp4`, unrelated to `Recording-<time>.mp4`, so the folder gains two files with no link between them. | `~/Videos/Omaframe` listing |
| V3 | P1 | "Keep original" is the primary action for an unedited recording. It reads like a choice between files. The recording is already saved; the action is Done. | `of-13-review.png` |
| V4 | P1 | Cutting is a separate mode. The layout jumps, the confirm button is disabled until a drag, and clicking the filmstrip means different things in and out of the mode. | `of-14-cutmode.png` |
| V5 | P1 | Review opens as a half-width tile next to whatever was focused. | `of-13-review.png`, 941 px wide |
| V6 | P2 | No way to put a finished clip on the clipboard for pasting into chat or a file manager. | |

## Screenshot editor

| # | P | Problem | Evidence |
| --- | --- | --- | --- |
| E1 | P0 | Text is entered and re-edited in a modal, unthemed white dialog that hides the image. You cannot see the label while typing it. | `of-04-textdlg.png` |
| E2 | P1 | When a mark is selected, its properties push the tool palette below the fold. The tools move depending on what is selected. | `of-05-text.png` |
| E3 | P1 | Font size spin box, sliders and dialog buttons use unthemed light Qt controls. | `of-05-text.png` |
| E4 | P1 | A newly drawn mark is selected but shows no handles unless you switch to Select. There is no hover feedback on marks. | `Main.qml` `selectedOutline.visible` |
| E5 | P1 | Escape jumps straight out of Edit instead of peeling one layer (text, selection, tool). | `Main.qml` Escape shortcut |
| E6 | P1 | The quick editor opens as a half-width tile. | `of-08-quickedit.png` |
| E7 | P2 | Three stacked headings and descriptions say the same thing ("Edit screenshot", "THE EDITOR / Annotate", tool hint). | `of-03-edit.png` |
| E8 | P2 | Tool shortcuts are not shown anywhere in the editor. | |

## First run, home and settings

| # | P | Problem | Evidence |
| --- | --- | --- | --- |
| H1 | P1 | No first-run orientation. The launcher entry starts a frozen selector with no explanation; the studio opens on a fake "Noon workspace" sample titled "Choose a finish". | `of-01-studio.png`, desktop entry `Exec=omaframe %f` |
| H2 | P1 | Print still opens Omarchy's own screenshot tool after install. Only Alt+Print is taken over, and only when recording setup is opened. | box `hyprctl binds` |
| H3 | P1 | The studio has no way to start a recording. New capture offers only screenshots. | `of-02-menu.png` |
| H4 | P1 | After a quick copy and save, every window disappears with no confirmation of where the file went. | chooser flow |
| H5 | P1 | Every accepted capture silently keeps an unredacted private copy forever. Nothing in the app reads these back. | `Studio::accept`, `originals/` |
| H6 | P2 | Folder settings live in a footer link whose dialog title always says "screenshots", even for videos. There is no settings surface. | `Main.qml` folder dialog |
| H7 | P2 | Recent edits show a generic name ("Region capture") with no preview. | `Main.qml` draft rows |

## Capture boundaries and fullscreen behavior

GPU Screen Recorder records a display or region through KMS capture
(`gsr-kms-server`). It reads the composited framebuffer, so every layer on the
recorded display is in the video, including overlay-layer surfaces and the
Omarchy bar. Hyprland's `no_screen_share` layer rule applies to screencopy and
portal capture, not KMS, and it draws a black box rather than removing the
surface. There is therefore no way to keep a visible control on the recorded
display out of a full-display video. The only capture-free positions are
outside a region on the same display, or on another display.

Fullscreen windows cover the bar (top layer) but not overlay-layer surfaces.
A control on another display stays visible above a fullscreen game there. A
single-display full-display recording has no capture-free visible surface at
all while a fullscreen app covers the bar; a keyboard shortcut is the one
dependable stop path in that case.

The stock Omarchy bar shows its recording icon for any process whose command
line starts with `gpu-screen-recorder`, which includes Omaframe's recorder.
Clicking it runs `omarchy-capture-screenrecording --stop-recording`, which
sends SIGINT to the recorder. Omaframe then finishes normally. That script
also posts its own "Screen recording saved" notification with an empty file
path, because it did not start the recording.

## What this pass changed

| # | Status | Change |
| --- | --- | --- |
| R1 | Fixed in code, needs hardware proof | No Omaframe control is ever placed inside a capture. A whole display on one monitor shows only a countdown that is hidden, and checked gone, before capture. |
| R2 | Fixed in code, needs hardware proof | Other-display placement uses the edge that faces the recording, below that display's bar. |
| R3 | Fixed | Regions try below, above, right and left inside the usable area; the bar's reserved strip is avoided. |
| R4 | Fixed | Shortcuts change only when the user asks. Print and Alt+Print are offered together during first run, in Settings and in recording options. |
| R5 | Fixed | Tab or Alt+Print keeps the selector open in video mode with sound, mic and countdown in the bar; a drag starts recording. |
| R6 | Fixed | Recording options has one primary action, no repeated status, and fits its content. |
| R7 | Fixed | **F** and a Whole display button capture or record the display; a whole-display selection records the display natively. |
| R8 | Fixed | The recorder is signaled to finish if Omaframe dies. |
| R9 | Fixed | Files under 64 KiB with no readable video are removed and the message says so. |
| R10 | Fixed | Any key bound to `omaframe --record` or `--stop-recording` is recognized and named. |
| V1 | Fixed | Review ends like a quick screenshot: Copy and close, or Save and copy after an edit, puts the file on the clipboard, closes, and notifies with the folder. Failures stay open. |
| V2 | Fixed | Exports are `<recording>-edited.mp4`, never overwriting. |
| V3 | Fixed | The unchanged-recording action is Copy and close; an edited one is Save and copy. |
| V4 | Fixed | Cutting is modeless: drag to select, Remove this part, click a removed part to change or restore it. Undo covers trims, cuts and sound. |
| V5, E6 | Fixed | Quick editor and review float at a comfortable size on their display (Hyprland). |
| V6 | Fixed | Review copies the video to the clipboard as a file; studio videos have Copy file. |
| E1 | Fixed | Labels are typed on the image. |
| E2, E7, E8 | Fixed | Tools stay at the top with their keys; one heading; properties below. |
| E3 | Fixed | Themed size field, sliders, confirmations, tool tips and palettes. |
| E4, E5 | Fixed | Selection and handles show in every tool; hover outline and move cursor; right-click selects; Esc ladder. |
| H1 | Fixed | Start screen with a one-time explanation; the launcher opens it. |
| H2 | Fixed | First-run setup offers Print with Alt+Print. |
| H3 | Fixed | New capture offers Record video. |
| H4 | Fixed | A notification names the save folder after a quick screenshot. Captures started from the studio return to it. |
| H5 | Changed | Keeping private originals is an explicit setting, off by default. |
| H6 | Fixed | Settings holds both folders; the footer link opens the right one. |
| H7 | Fixed | Recent edits show a preview. |

Found and fixed during verification: cancelling a capture started from the
studio used to quit the whole app; a recording started from the studio and
routed through recording options quit when review ended instead of returning; a stale
hover outline stayed behind after a drag; the settings popup clipped its right
column at 1366×768. After-state screenshots are under ignored
`evidence/review-2026-09-27/after/`.

## Release blockers, in priority order

The owner's desktop run on 2026-09-27 (see recording-validation.md) cleared
most hardware gates: no control in any video on one or two monitors or over a
fullscreen window, Stop at the facing edge of the other monitor, sound with
about 26 ms lead and no drift, a 10-minute recording and its re-encoded
export, screenshots on both monitors at 1.25 scale, screenshot editing during
a recording, and shortcuts surviving a theme change. What remains:

1. **Review of the uncommitted diff**: a focused pass over the riskiest paths
   (recorder states, bindings editing, file deletion, review flow, QML
   references) found and fixed three bugs (see recording-validation.md, "Code
   review fixes"). It was not a line-by-line read of all 6,700 lines.
2. **Microphone.** The owner recorded a real fullscreen game on 2026-09-28:
   whole display with computer sound, 21.7 s, decodes fully, and the Stop
   control is absent from every sampled frame. The microphone was off, so
   mic capture is still unverified on hardware.
3. ~~Fresh-system package lifecycle~~: passed in a fresh Arch container with
   dependency resolution (install, upgrade, rollback, uninstall, user files
   untouched). A launcher check in a real fresh session is still open.
4. ~~Shortcut persistence across a new login~~: the owner restarted on
   2026-09-28 and Print and Alt+Print still opened Omaframe.
5. ~~Tag and publish~~: v0.2.0 is a GitHub pre-release at
   https://github.com/btsouth/omaframe/releases/tag/v0.2.0.

Should follow soon after, not blocking: an outline around the recorded area
(kept outside it), live preview of a mark while it is dragged, hardware
encoding for faster edited exports, a replace-the-original option for trims,
and tests for pointer-display selection and moving off special workspaces.
