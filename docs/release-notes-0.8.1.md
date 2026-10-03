# Omaframe 0.8.1

You can now turn off **Automatically save screenshots** in Settings. Picking a
finish then copies it to the clipboard and closes the chooser without saving
a screenshot or private original. Automatic saving stays on by default, so
existing users keep the same flow.

Press **Ctrl+C** or click **Clipboard** in the finish chooser to copy the selected
finish without saving, regardless of the setting. **Copy and save** still saves
when automatic saving is off. Edits and redactions appear in the copied image;
editable editor drafts keep their existing autosave behavior.

Copying runs in the background and failures leave the chooser open to retry.
Pending save, backup and clipboard retries retain their original output.

Thanks to [Son Le (@sonlndv)](https://github.com/sonlndv) for
[PR #22](https://github.com/btsouth/omaframe/pull/22), which this feature builds on.
His original commits are included, with co-author credit for the implementation.

Validated with all twelve regression suites, isolated Omarchy clipboard and
chooser checks, and repeated 1080p captures with no meaningful latency regression.
Physical GPU, microphone and camera combinations were not retested for this release.

Update through `omarchy update` or `sudo pacman -Syu`, then reopen Omaframe.
Existing captures, settings and drafts are preserved.
