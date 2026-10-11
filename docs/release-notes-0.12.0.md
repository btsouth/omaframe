# Omaframe 0.12.0

Screenshots can now skip the finish chooser.

- **Skip the finish chooser.** Turn it on in Settings, next to Save
  screenshots automatically. A window, area or whole-display screenshot is
  copied as soon as you select it, in Raw without a frame, and saved too when
  automatic saving is on. The finish you picked in the chooser stays
  remembered for the chooser. The Screenshot copied notification
  confirms it, and clicking it opens the editor, where the finish, Pin and
  marks are still available. The chooser's Hide possible secrets prompt is
  skipped too, so look before you paste. Scrolling captures still end in the
  chooser, and a screenshot that cannot be saved or copied opens the chooser
  with its retry, so nothing is lost. The setting is off by default.
- **A key for the chooser.** Settings can bind Ctrl+Print to a screenshot that
  always ends in the finish chooser, when the key is free. Print, Alt+Print
  and Shift+Print are not changed.
- **`--direct` and `--chooser`.** `omaframe --capture --direct` skips the
  chooser for one screenshot and `omaframe --capture --chooser` always shows
  it, whatever the setting. They work with no option, `--screen`, `--repeat`
  and `--delay`, and a running Omaframe honors them.

Update through `omarchy update` or `sudo pacman -Syu`, then reopen Omaframe.
Existing captures, settings and drafts are preserved.
