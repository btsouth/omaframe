# Omaframe 0.5.1

A polish and bug-fix release for screenshots and recordings. Changes since
0.5.0:

- Clearer first-run guidance, optional shortcuts and recent edits ordered
  newest first. Existing settings and dismissed guidance stay intact.
- Capture controls fit smaller screens. Recording options keep Start
  visible, and paused recordings show both elapsed time and Resume.
- Cleaner screenshot editing, better label placement and keyboard controls.
  Failed saves let you change the folder and retry the retained capture.
- Video and GIF export progress and Cancel stay visible above the preview.
  Typing in time fields no longer triggers editor shortcuts.
- Copy video works for opened originals and current exports. Undoing back
  to the original copies the original rather than an older edited export.
- Back returns opened videos to the start screen. Settings, folder pickers
  and camera controls have clearer labels, focus and light/dark contrast.

Install or update with the attached Arch package or the stable
`omaframe-x86_64.pkg.tar.zst` asset.

The build and all nine test suites passed in an isolated desktop. UI checks
covered first launch, retained upgrade settings, area/window/whole-display
capture, editing, pause/resume and export in light/dark themes and smaller
windows. Recording tests use fixtures; physical camera, audio and
GPU-specific capture behavior were not retested.

Omaframe remains a beta. GPU Screen Recorder 6.1.0 remains the minimum.
