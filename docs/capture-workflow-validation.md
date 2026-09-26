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
