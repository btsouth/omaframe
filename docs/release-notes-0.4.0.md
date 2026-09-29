# Omaframe 0.4.0

Pause and resume recordings, clearer controls, and protection for unsaved
video edits. Changes since 0.3.0:

- **Pause and resume.** Pause keeps the same recording open and leaves the
  paused time out of the saved video and audio. The timer counts recorded
  time, and Stop still saves the clip while paused.
- **Optional pause shortcut.** Enable capture shortcuts in Settings for
  Alt+Shift+Print Screen, including recordings without a visible control.
  Setup preserves custom bindings. Scripts can use `--pause-recording`,
  `--resume-recording` and `--toggle-recording-pause`.
- **Simpler recording controls.** Labeled Pause, Resume and Stop buttons,
  centered contents, clearer sound and microphone options, and recording
  setup that stays reachable in smaller windows.
- **Cleaner editing.** Named video mark tools, editor controls that fit
  smaller windows, keyboard access to recent drafts, menus that close with
  Esc, and a sample image that opens directly in Edit. Settings now show the
  actual app version.
- **Protect video edits.** Opening another file, starting a capture or
  quitting offers Cancel, Discard edits or Save and continue. File drops and
  capture shortcut commands use the same prompt. Save and continue waits
  until the export succeeds.
- **Keep edits after errors.** Failed saves keep the edits available to
  retry and show a readable message. A failed file open preserves the current
  editor and its saved state.
- **Basic CI.** Arch builds, six headless suites, OCR pattern tests and
  install-layout checks run on pull requests and pushes to main.

Install or update with:

```sh
curl -fLo /tmp/omaframe.pkg.tar.zst https://github.com/btsouth/omaframe/releases/latest/download/omaframe-x86_64.pkg.tar.zst && sudo pacman -U /tmp/omaframe.pkg.tar.zst
```

All eight test suites passed in an isolated desktop. Light and dark themes,
small windows, recording controls and save/discard/cancel flows were checked
there with fixture recordings. These checks do not measure physical GPU or
microphone behavior.

Omaframe remains a beta. Video edits have no draft recovery, and there is no
webcam overlay or zoom yet. GPU Screen Recorder 6.1.0 remains the minimum.
