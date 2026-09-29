# Omaframe

Beautiful screenshots and clean screen recordings for Omarchy.

Omaframe is small on purpose. Press a key, pick how it looks, paste. When you
need an arrow, a label or a blur, it is one key away. There is no big editor to
learn, no account and no upload. Everything stays on your computer.

<p align="center">
  <img src="docs/media/hero.png" width="760" alt="A dashboard screenshot in Omaframe's Aurora finish, with a Best week label, an arrow to the tallest bar and a blurred list">
</p>

## Install

On Omarchy or Arch with Hyprland, one command installs the latest release and
everything it needs:

```sh
curl -fLo /tmp/omaframe.pkg.tar.zst https://github.com/btsouth/omaframe/releases/latest/download/omaframe-x86_64.pkg.tar.zst && sudo pacman -U /tmp/omaframe.pkg.tar.zst
```

If pacman says it cannot satisfy a dependency, your package lists are older
than GPU Screen Recorder 6.1.0. Run `omarchy-update` (or `sudo pacman -Syu` on
plain Arch) and then the command again.

Open Omaframe from the launcher and click **Use Print and Alt+Print**. It only
replaces Omarchy's default actions for those two keys, backs up your bindings
first, and never touches a key you set up yourself.

## Take a screenshot

Press Print Screen. Click a window, drag an area, or press F for the whole
display. Pick a finish with 1 to 9 and it is copied and saved. Paste it
anywhere.

<p align="center"><img src="docs/media/screenshot.gif" width="800" alt="Pressing Print Screen, clicking a window, picking a finish and getting a Screenshot copied notification"></p>

While you pick, Omaframe reads the text in the screenshot. If it spots
something that looks like a secret, such as an API key, a token, an email
address or a card number, a **Hide possible secrets** button appears. Press H
and each one gets a redaction you can still move or undo in the editor. It can
miss things, so look before you share. Press T to copy the text in the
screenshot instead.

## Mark it up

Press E before you pick a finish. Press T and click to type a label right on
the image, then add arrows, boxes, highlights, blur or redaction, or crop.
Every mark stays movable and editable, and Esc takes you back to the finishes.

<p align="center"><img src="docs/media/edit.gif" width="800" alt="Typing a label on a screenshot, drawing an arrow and blurring a list"></p>

## Record your screen

Press Alt+Print Screen, then click a window, drag an area, or press F. The Stop
button always sits outside what you are recording, so it never ends up in the
video. When you stop, trim the ends or cut out a slow part, then **Copy and
close**. The video is on your clipboard.

If something private showed up while you recorded, press G to blur it or R to
cover it, and drag over it. It stays hidden for the whole clip. To point
something out, pause where it happens and press A for an arrow, B for a box, T
for a label or N for a numbered step. Those show from there to the end. Select
any mark and press I and O to set where it starts and stops.

<p align="center"><img src="docs/media/record.gif" width="800" alt="Recording an area with the Stop button outside it, cutting a part in review and copying the clip"></p>

[Watch the full 50-second demo](https://github.com/btsouth/omaframe/releases/download/v0.2.1/omaframe-demo.mp4)

## Keys

| Where | Key | Does |
| --- | --- | --- |
| Anywhere | Print Screen | Take a screenshot |
| Anywhere | Alt+Print Screen | Start a recording, or stop it |
| Choosing an area | F / Tab / Esc | Whole display / switch to video / cancel |
| Choosing an area to record | D / M | Computer sound / microphone |
| Picking a finish | 1 to 9, Enter | Copy and save with that finish, or the last one |
| Picking a finish | E / R | Mark it up / retake |
| Picking a finish | H / T | Hide possible secrets / copy the text |
| Marking up | V C A L B O H R G P N T | Select, crop, arrow, line, box, oval, highlight, redact, blur, pen, steps, text |
| Marking up | Shift+H | Hide possible secrets |
| Marking up | Ctrl+Z / Ctrl+Shift+Z | Undo / redo |
| Marking up | Delete, Ctrl+D, F2 | Delete, duplicate, rewrite the selected label |
| Reviewing a video | Space, I / O, Delete | Play, set start / end, remove the selected part |
| Reviewing a video | G R A B T N | Blur, redact, arrow, box, label, step. With a mark selected, I / O set when it shows |
| In the window | Ctrl+C or Ctrl+S | Copy and save |

## Good to know

Screenshots go to `~/Pictures/Omaframe` and recordings to `~/Videos/Omaframe`.
Change either in Settings. Saved images contain only the rendered pixels, and
redaction replaces pixels with a solid fill. Your originals are never changed.

The text Omaframe reads for H and T stays in memory. It is never saved, not
even in drafts. Reading needs `tesseract` and `tesseract-data-eng`; without
them the button and T are simply not there. Set `OMARCHY_OCR_LANGS` (for
example `eng+deu`) to read other languages, as Omarchy's own text capture does.

When you add marks, Omaframe keeps an editable draft with a private copy of the
capture, listed under Recent edits so you can change it later. Delete a draft
there when you are done with it.

Recording a whole display on a single monitor leaves no room for a Stop button
outside the video, so Omaframe shows a short countdown that tells you how to
stop, and it is gone before the first frame. Stop with Alt+Print Screen or with
the recording icon in the Omarchy bar. With a second monitor, the Stop button
waits there, at the edge next to the one you are recording.

A whole-display recording includes the Omarchy bar, because it is part of the
screen. Stopping from the bar's icon works normally; Omarchy may also show its
own "Screen recording saved" notice.

You can take and edit screenshots while you record. If you stop a recording in
the middle of a screenshot, its review opens once the screenshot is done.

When a capture stops right at a plain background, the finishes carry that
background out a little so the content is not pressed against the edge. Raw
keeps the capture exactly as it was.

Omaframe follows your Omarchy theme and updates when you switch themes.

There is no pause, webcam overlay or zoom yet. Marks on a video stay in place:
they do not follow something that moves or scrolls.

## Uninstall

```sh
sudo pacman -R omaframe
```

Then delete the block between `-- omaframe:shortcuts:start` and
`-- omaframe:shortcuts:end` in `~/.config/hypr/bindings.lua` to give Print
Screen and Alt+Print Screen back to Omarchy. Your screenshots, recordings and
settings stay where they are.

## Build from source

Build dependencies: `base-devel cmake ninja pkgconf qt6-base qt6-declarative
qt6-multimedia qt6-wayland layer-shell-qt wayland wayland-protocols
wl-clipboard ffmpeg gpu-screen-recorder libpulse procps-ng xdg-utils`.
`libnotify` is optional and adds a notification after a quick screenshot.
`tesseract` and `tesseract-data-eng` are optional and add secret hiding and
copying text.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j 3
./build/omaframe --studio
```

`omaframe` with no options takes a screenshot, `--record` starts or stops a
recording, `--repeat` captures the last area again, `--studio` opens the
window, and a file path opens that image or video. You can also build the
package yourself: download `PKGBUILD` from the
[latest release](https://github.com/btsouth/omaframe/releases/latest) and run
`makepkg -si`. See [RELEASING.md](RELEASING.md) for how releases are made.

The capture, clipboard and recording tests talk to a Wayland compositor and the
session bus, so run them in a throwaway nested desktop rather than your own
session. With [omabox](https://github.com/diogochaves/omabox), for example:

```sh
omabox run --net isolated -- ctest --test-dir build --output-on-failure
```

Test notes and hardware results are in [docs/](docs/), starting with the
[recording validation](docs/recording-validation.md).

## License

MIT. The native Wayland capture code is adapted from Omasnap at
`acfb3b5772ccb041b57ccc76e16f1b72719f37e9`, copyright Tobi Lütke, and its
license is kept in [docs/OMASNAP-LICENSE](docs/OMASNAP-LICENSE). MatteShot and
Omaroll informed the workflow and style; Omaframe has its own interface and
renderer.
