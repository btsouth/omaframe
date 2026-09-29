# Omaframe 0.3.0

Marks on recordings, hiding possible secrets in screenshots, and a fix for
displays that are turned off.

- **Mark up recordings.** In review, press G to blur or R to cover something
  private for the whole clip. Pause and press A, B, T or N for an arrow, box,
  label or numbered step from there to the end. Select a mark and press I and
  O to set when it shows.
- **Hide possible secrets.** While you pick a finish, Omaframe reads the
  screenshot. If it spots something that looks like an API key, token, email,
  card number, IBAN, public IP or private key, a **Hide possible secrets**
  button appears and H redacts them. Shift+H does the same in the editor. It
  can miss things, so look before you share.
- **Copy text.** Press T in the picker to copy the text in the screenshot.
  What Omaframe reads stays in memory and is never saved.
- **Room at the edges.** When a capture stops right at a plain background, the
  finishes carry that background out a little. Raw is unchanged.
- **Displays that are off.** Print Screen no longer gets stuck waiting on a
  monitor that is off, for example while streaming with Sunshine and
  Moonlight, and `--repeat` on an area of a display that is off asks you to
  select again. Thanks to Diogo Chaves for finding and fixing this (#1).

Secret hiding and copy text need `tesseract` and `tesseract-data-eng`, which
Omarchy already has. Without them those two features are simply not there.

Install or update with:

```sh
curl -fLo /tmp/omaframe.pkg.tar.zst https://github.com/btsouth/omaframe/releases/latest/download/omaframe-x86_64.pkg.tar.zst && sudo pacman -U /tmp/omaframe.pkg.tar.zst
```

Validated with all seven test suites in an isolated desktop, the secret
picker, editor, video review and edge room checked by hand there, and video
marks on the owner's desktop. Not retested: repeating an area on one of two
monitors while that one is off, and a fresh install on Omarchy stable.
Omaframe remains a screenshot and basic recording beta.
