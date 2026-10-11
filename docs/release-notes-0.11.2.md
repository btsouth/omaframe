# Omaframe 0.11.2

Opening a video in the editor now shows its first frame straight away. Before,
the picture stayed empty until you pressed play, and a frame that did appear
could vanish again when the filmstrip finished loading.

The filmstrip loads faster. Its frames are now read several at a time, so a
2 minute 1440p recording shows its filmstrip in under 2 seconds instead of
about 5. High bitrate clips under 90 seconds gain the most: one 85 second
clip went from about 7 seconds to under 2. The strip shows that it is loading
until its frames arrive, and a frame that takes too long no longer leaves the
whole strip empty.

Validated in an isolated Omarchy desktop with a real 141 second 2560x1440
60 fps recording and a shorter one, including opening a second video in the
same window and playback. A new test checks that an opened video waits paused
on its frame and stays there when the filmstrip arrives. CI ran the eighteen
headless suites on both Omarchy stable and current Arch.

Update through `omarchy update` or `sudo pacman -Syu`, then reopen Omaframe.
Existing captures, settings and drafts are preserved.
