# Omaframe 0.11.0

Omaframe now pins screenshots to your screen, and opens a screenshot again
from its notification.

- **Pin a screenshot.** Press P on the finish chooser, Pin (Ctrl+P) in the
  editor, or Pin in History. The pin lifts off what you captured and settles
  in the farthest corner, above your windows on every workspace. Drag it
  anywhere, across displays too, resize it from a corner, scroll to zoom and
  Ctrl+scroll to fade it. Right-click for Copy, Save, Actual size and Click
  through, which lets clicks reach the window below. A pin is the screenshot
  with your marks and crop; Shift+P keeps the selected finish, for showing it
  on a call. Pinning copies and saves nothing, and Omaframe exits when the
  last pin closes.
- **Edit from the notification.** Click Screenshot copied to open that
  screenshot in the editor again, with its marks and finish still editable
  (#37).

## Fixes

- On Qt 6.12 the video playhead no longer jumps back to the start just after
  a recording opens, and a resumed draft keeps its trimmed start.
- Playing to the end of a trimmed clip now returns to the trim start, as
  intended.
- Step numbers keep the order you placed them in saved video drafts too.

Validated with the eighteen headless suites in CI on Qt 6.12, the desktop
suites in an isolated Omarchy desktop, and pins on a two monitor Omarchy
desktop at 1.25 scale.

Update through `omarchy update` or `sudo pacman -Syu`, then reopen Omaframe.
Existing captures, settings and drafts are preserved.
