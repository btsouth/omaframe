# Recording validation, 2026-09-26

## Implemented

- Native recording setup and Screenshot/Video capture-bar switch.
- Region or display target, microphone selection, independent desktop/mic toggles, cursor toggle, optional countdown.
- Owned GPU Screen Recorder child process; SIGINT stops that child only. Another running recorder blocks launch.
- Timer/Stop pill below or above the region, otherwise on a second display. Actual Hyprland layer geometry is checked before recording. Missing/overlapping control blocks launch.
- No safe placement requires a keyboard-stop acknowledgement. Display geometry changes stop the recording.
- Saved MP4 is probed and automatically opens in the trim editor. Audio startup-pop cleanup copies video, mutes the first 400 ms and fades audio in over 50 ms, without loudness normalization.
- Selected microphone uses an explicit source ID. Clean Desktop Microphone is preferred when available. An unavailable saved source requires user selection; there is no default_input fallback.

## Verified

Build succeeded. All three CTest suites passed in omabox (recording, renderer, pipelines), 4.53 seconds. Recording tests cover single/dual-display placement including negative monitor coordinates, explicit audio arguments, process ownership and valid handoff, actual-control absence blocking launch, countdown cancellation, and decoded audio silence followed by retained signal after cleanup.

Inspected recording setup, region mode, keyboard-only acknowledgement, recording pill, and completed clip editor in the isolated 1600x1000 desktop. The region was x250,y180,900x520; the compositor placed Stop at x559,y716,280x56, outside the capture ending at y700. Clicking Stop completed the simulated recording and opened the clip. The separate --stop-recording IPC command also returned success for an active simulated full-display recording.

A simulated backend supplied a known video file in UI tests. These checks prove controller/UI behavior, not hardware capture or microphone quality. Latest screenshot selector mapping measurements: 233, 171, 197 ms. Escape cancellation passed all three trials after moving Escape handling to a window shortcut.

## Local integration

Alt+Print now attempts Omaframe stop, then legacy recorder stop, then opens Omaframe recording setup. Plain Print remains screenshot capture. Omarchy's Screenrecord submenu has a Record with Omaframe entry, and its Stop row tries the owned controller first. Legacy recording/webcam rows remain available. Hyprland reload/configerrors were clean and live bindings showed both Omaframe actions. Menu rendering was not checked on the real desktop.

Backups of bindings.lua and omarchy-menu.jsonc use suffix .bak.omaframe-record-20260926-000437. No packaged Omarchy files were modified.

## Owner acceptance still needed

Record a region with desktop and Clean Desktop Microphone audio. Click Stop and check first/last frames, speech level, sync, and saved MP4 playback. Repeat on the second physical monitor and for a full display with Stop on the other monitor. Real GPU capture, mixed/fractional-scale coordinates, HDR, device disconnect, long sessions, and physical audio quality are not established by omabox tests.

Pause, camera integration, and separate audio tracks are not implemented. A full capture on the only display cannot have a visible Stop control without recording it, so this build uses the acknowledged hotkey fallback.
