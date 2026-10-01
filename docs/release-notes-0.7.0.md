# Omaframe 0.7.0

Capture a scrolling page as one screenshot, then finish and edit it in Omaframe.

- Press **S** in the screenshot selector, or run `omaframe --scroll`. Click a
  window or drag a region. Omaframe scrolls and joins the page automatically.
- Move the pointer to take over and scroll by hand. **Done**, Enter or Esc
  keeps what has been captured; **Cancel** discards it.
- Fixed headers and footers are kept once. The progress control stays out of
  the saved image, and detected scrollbars are trimmed away.
- Tall pages fit to width in the editor. Scroll to add annotations or crop;
  exports keep the original resolution, and crop undo restores the full page.
- Capture stops at 32000 pixels on either edge or 200 MiB of image pixels,
  with a note when it stopped early or reached the size limit.
- Preview workers now finish before the studio shuts down, fixing a shutdown
  crash and hang exposed by the scrolling integration tests.

Animated or changing pages may need a smaller region or manual scrolling.
Completely repeating content can leave ambiguous seams; check the result
before sharing.

Install or update with the attached `omaframe-x86_64.pkg.tar.zst` package, then
reopen Omaframe. GPU Screen Recorder 6.1.0 remains the minimum.

Omaframe remains a beta. Automated checks cover fixture recording and camera
behavior; physical camera, microphone and GPU recording combinations were
not retested for this release.
