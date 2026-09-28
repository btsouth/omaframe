# Omaframe 0.2.0

Screenshots and screen recordings for Omarchy and Hyprland. Everything stays
on your computer.

## Screenshots

Press Print, click a window, drag an area, or press F for the whole display.
Pick a finish with 1 to 9 and it is copied and saved. Press E first to crop,
blur, redact, draw or add labels. Labels are typed right on the image, and
every mark stays movable and resizable. Annotated shots are kept as drafts you
can reopen.

## Recording

Press Alt+Print, or Tab in the screenshot selector. Turn on sound, the
microphone or a countdown right in the bar, then click a window, drag an area
or press F. The Stop button is always placed outside what you are recording,
or on your other monitor. With nothing to spare on a single monitor, a
countdown tells you how to stop and then gets out of the way. Alt+Print or the
recording icon in the Omarchy bar stops it.

After stopping, trim the ends or cut out parts, then Copy and close. The video
goes on your clipboard, ready to paste into a chat or folder.

## First run

The Omaframe window explains the basics and offers to point Print and
Alt+Print at Omaframe. It only replaces Omarchy's default actions for those
keys, backs up your bindings file first, and never touches a custom binding.

## Install

Install `omaframe-0.2.0-1-x86_64.pkg.tar.zst` with `sudo pacman -U`, or build
from the attached `PKGBUILD` with `makepkg -si`. Check the files against
`SHA256SUMS`.

## Requirements

Omarchy or Arch with Hyprland, GPU Screen Recorder 6.1.3 or newer, FFmpeg and
wl-clipboard. libnotify is optional.

## Known limits

No pause, webcam overlay, zoom, or annotations on video yet. A whole-display
recording includes the Omarchy bar, since it is part of the screen.
