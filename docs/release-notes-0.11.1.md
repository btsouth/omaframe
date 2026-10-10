# Omaframe 0.11.1

Omaframe 0.11.0 did not start on Omarchy stable or rc. It was built against
Qt 6.12, which only edge has, and failed with "version `Qt_6.12' not found".
This release is built against the Qt 6.11 that stable and rc ship, so it
starts on stable, rc and edge. Nothing else changed.

The package now requires the Qt it was built against, so pacman refuses an
install that would fail to start instead of letting it through.

Validated by building in a clean Arch container on Omarchy's stable mirror,
checking that the binary needs nothing newer than Qt 6.11, and installing and
starting it offscreen in that container. CI ran the eighteen headless suites
on both Omarchy stable and current Arch.

Update through `omarchy update` or `sudo pacman -Syu`, then reopen Omaframe.
Existing captures, settings and drafts are preserved.
