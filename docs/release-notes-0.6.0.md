# Omaframe 0.6.0

Annotation styles and easier editing, with fixes since 0.5.1:

- Capture hints sit below the bar and no longer block buttons. Recording
  controls keep the hints outside the captured area.
- Side handles resize width or height independently. Text wraps when resized
  from the sides; corner handles scale the font. Crop frames and pen strokes
  are easier to move and resize, with Shift for straight movement.
- Contextual style controls offer label colors and backgrounds, open or
  filled arrowheads, contrast outlines, shape fills, highlight opacity,
  step colors and blur strength. Choices are remembered for each tool.
- Style panels have a clear preview, close when you click outside and fit
  smaller windows. Settings rows and shortcut borders have proper spacing.
- Fixed recording review getting stuck when closing after annotations,
  including Super+W and the save-and-close action.

Install or update with the attached Arch package or the stable
`omaframe-x86_64.pkg.tar.zst` asset. Reopen Omaframe after updating.

The build and all nine test suites passed in an isolated desktop. UI checks
covered annotation controls, settings, smaller windows and closing the editor.
Rendering and video export were checked with fixtures; physical camera,
audio and GPU-specific recording behavior were not retested.

Omaframe remains a beta. GPU Screen Recorder 6.1.0 remains the minimum.
