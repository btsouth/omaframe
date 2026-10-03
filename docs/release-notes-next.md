# Next Omaframe release

Screenshot editing is easier to navigate:

- Use the mouse wheel to zoom around the pointer. Hold Shift while scrolling
  to move through a long capture, or use the scrollbars.
- With Select active, drag empty space to pan a zoomed image. Middle-button
  dragging also pans. Use Fit to return to the normal view.
- Click an active tool again to return to Select. Mouse clicks no longer leave
  a keyboard focus outline around the previous tool.
- Crop an already cropped screenshot to trim it further. Each crop has its
  own undo step. Ctrl+Z restores the previous crop; Ctrl+Shift+Z or Ctrl+Y
  reapplies it. Marks outside the crop stay available when you undo.

If the capture bar covers something you need, hold Super and drag it, or
drag the grip at its left edge. Press H or click its H button to hide it, then
press H to bring it back. New captures show the bar again. It stays reachable
when the display size changes, and narrow displays hide extra key hints so the controls fit.

Repeated crops now follow the exact source pixels shown in the editor.
Drawing waits for a changed crop preview to appear, preventing misplaced
marks on large captures while the preview refreshes.
