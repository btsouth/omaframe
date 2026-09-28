# Omaframe release and MatteShot parity tracker

Reference: local MatteShot `01f16a2` (0.21.1) README and source, reviewed again
on 2026-09-27 for editor interaction. This tracks working behavior, not the
larger product-plan backlog. The apps use different capture APIs and run on
different operating systems.

| Workflow | Omaframe 0.2 preview | Before a public release | Toward MatteShot parity |
| --- | --- | --- | --- |
| First run | Start screen with Screenshot, Record and Open; a one-time explanation; consented setup of Print and Alt+Print that replaces only Omarchy's stock bindings | Real-desktop check that the keys survive a theme change and a new login | Custom key recorder in Settings |
| Freeze, pick window/area/display, finish, copy/save | Window click, drag, **F** or empty click for the whole display; finish picker; copy, save and notification | Both physical monitors, fractional scale, clipped and overlapping windows | Cross-monitor areas, size presets |
| Screenshot editing | Tools stay in one place; labels typed on the image; every mark movable and resizable with any tool; hover and selection feedback; Esc steps back one layer; right-click selects; multiline text; direct size; styles; layers; duplicate; undo/redo; drafts with previews | Real captures in light and dark themes | OCR, pin, tabs, curved arrows |
| Record and stop | Video mode in the capture bar with sound, mic and countdown; Stop placed outside every recording, on the facing edge of another display, or not at all with a countdown that leaves before capture; Omarchy bar icon and any bound key stop it; child finishes its file if Omaframe dies | Physical proof that no control is in any video, on one and two monitors and over a fullscreen game; sound and sync by ear | Pause, window-follow capture, camera, GIF |
| Video review | Play, trim, select and remove parts (modeless), sound on/off, undo/redo for all edits; Copy and close, or Save and copy to `…-edited.mp4`, then clipboard, close and a notification; studio videos stay open | Long clips, cancellation and export time on hardware | Speed sections, crop/zoom, timed annotations and redaction |
| Distribution | CMake install, desktop entry that opens the window, license notices, SHA-checked Arch package | Fresh dependency resolution and lifecycle checks | Updates and optional sharing need a separate design and trust gate |

## MatteShot comparison

What MatteShot does that Omaframe now matches: inline caption typing that
commits on click away, tools that stay armed, a context hint under the
preview, a palette that loads the selected mark's values, hover and selection
feedback with matching cursors, an Esc ladder, and a first-run welcome. It
also records with its Stop pill excluded from capture by Windows. Omaframe
cannot exclude a surface from KMS capture, so it places the control where the
recording is not.

Omaframe goes further in some places: multiline labels with a caret, resize
handles on labels, redo, duplicate, layers, removing middle sections of a
video, and consented shortcut setup.

Not planned for the first release: OCR, pinned screenshots, tabs, speed
sections, video annotations, and GIF export. They are backlog, not hidden
controls.

## Video editing sequence

The first release's video review covers playback, precise trim, removing
pauses, sound on/off, a dependable MP4 export and sharing the file. The next
slice should add crop/zoom and timed redaction, then timed text, arrows and
highlights. A video mark needs explicit start and end times, direct
move/resize/edit controls, undo/redo, and matching preview and exported frames
before it counts as supported. Numbered steps belong in that timed system.
Static image finishes are optional video appearance presets later.

## Release blockers

See the prioritized list at the end of [interface-review.md](interface-review.md).
