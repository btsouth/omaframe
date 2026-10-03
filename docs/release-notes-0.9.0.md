# Omaframe 0.9.0

A click in the finish chooser now selects a finish. It no longer copies, saves
or closes the screenshot, so a stray click right after a capture is harmless.
Double-click a finish, press Enter or press 1 to 9 to copy it. Press E to mark
up the selected finish first. Clicking outside the panel no longer cancels;
Esc and the close button still do. Thanks to @sonlndv, who first proposed
selecting with a click.

In the editor, Ctrl+C, Ctrl+X and Ctrl+V copy, cut and paste the selected mark.
They never copy or close the screenshot. Each paste lands a step further from
the last, so the same arrow can be placed several times at the same size.
Video marks get the same keys, plus Ctrl+D to duplicate. A pasted video mark
starts at the playhead and keeps its length; blur and redact keep their times.

Finishing from the editor now follows **Save screenshots automatically**. With
it off, **Copy** (Ctrl+Enter) copies without saving, and **Copy and save**
(Ctrl+S) still keeps a file. Ctrl+Shift+C no longer finishes.

Validated with all thirteen regression suites, including new tests for the
chooser clicks, mark copy and paste in the editor and video pane, and editor
finishing with automatic saving on and off. Checked in an isolated Omarchy
desktop. Physical GPU, microphone and camera combinations were not retested
for this release.

Update through `omarchy update` or `sudo pacman -Syu`, then reopen Omaframe.
Existing captures, settings and drafts are preserved.
