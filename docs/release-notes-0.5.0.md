# Omaframe 0.5.0

Crop recordings, return to unfinished edits, add a webcam overlay and export
short demos as GIFs. Changes since 0.4.0:

- **Video crop.** Press C and drag to crop the whole clip. Preview with V,
  reset the crop or undo it.
- **Recovery drafts.** Video edits save automatically in Recent edits,
  including trim, cuts, sound, marks, crop and camera placement. Reopen a
  draft to continue. Removing a draft keeps the original and exported files.
- **Optional webcam overlay.** Choose a camera in recording Options. It
  starts off. After recording, drag the overlay while playback is paused;
  use Camera to change its size, corner or visibility. Pause, trim and cuts
  stay aligned with the screen recording. Camera failures leave screen
  recording running.
- **Looping GIF export.** Export GIF includes your edits and camera overlay,
  then offers Copy GIF and Show file. GIFs are silent, loop continuously and
  use up to 720 pixels on the longest edge at 15 fps. Keep the edited clip
  within 30 seconds. The editor stays open and existing MP4 exports remain
  available. GIFs can be larger than MP4.
- **Cleaner review.** More preview space in small windows, menus that close
  with Esc and shortcuts that stay out of the way while menus are open.

Drafts reference the original video, so keep it in place. For camera edits,
keep its webcam MP4 and camera JSON sidecar alongside it.

Install or update using the attached Arch package or
`omaframe-x86_64.pkg.tar.zst` from the latest release.

All nine suites passed in an isolated desktop for crop, drafts and camera.
Focused GIF and navigation tests passed afterward, with light/dark UI and
clipboard checks using fixtures. CI builds on Arch and runs seven headless
suites, OCR pattern tests and install checks. Physical camera acquisition
and GPU-specific capture behavior were not retested.

Omaframe remains a beta. Animated zoom and automatic focus are not included.
GPU Screen Recorder 6.1.0 remains the minimum.
