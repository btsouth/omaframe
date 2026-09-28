# Releasing Omaframe

This repository is an unpublished 0.2 preview. A local package is useful for
testing but is not a public release candidate until the physical capture and
package lifecycle gates below pass.

## Build an exact package

`packaging/build-package.sh` normally requires a clean checkout at tag
`v0.2.0` (or the version in `CMakeLists.txt`). It archives that tag, computes
the source SHA-256, fills `packaging/PKGBUILD.in`, and runs `makepkg` without
installing dependencies or changing the host package database. The output
contains the source archive, a resolved PKGBUILD, and an Arch package.

For a disposable package from current uncommitted work:

```sh
packaging/build-package.sh --working-tree /path/to/output
```

The script labels this as a local working-tree package. Do not publish it as
the tagged release. Record the source archive and package SHA-256 before
testing or sharing any candidate.

## Release gates

1. Run all five CTest suites in an isolated omabox. In the box, walk the
   start screen and first-run shortcut setup, a screenshot through the finish
   picker and editor (inline label, move, resize, Esc ladder), a stub-recorder
   region recording and a whole-display recording, and review with a removed
   part, Save and copy, and Copy and close. Check 1366×768 and a light theme.
   Use the exact package binary.
2. On the owner's real desktop, check screenshot window, area and whole
   display selection on both monitors, including fractional scale. Confirm
   paste, exported dimensions, inline label editing, redaction and finishes.
3. Record on real hardware and confirm that nothing from Omaframe is in any
   video:
   - an area, with the Stop button beside it;
   - a whole display with the Stop button on the other monitor, at the edge
     next to the recorded monitor;
   - a whole display with the other monitor unplugged or disabled, where only
     the countdown shows and Alt+Print or the Omarchy bar icon stops it;
   - a fullscreen game on the recorded monitor, with the Stop button still
     reachable on the other monitor.
   Include desktop sound and microphone, check sync by ear, take and edit a
   screenshot while recording, and play the saved and edited MP4s. The
   earlier 88-byte output is not fully resolved until this passes.
4. After first-run shortcut setup on the real desktop, switch the Omarchy
   theme (which reloads Hyprland) and log out and in. Print and Alt+Print must
   still open Omaframe. A custom Alt+Print binding must be left alone.
5. Install the package on a fresh supported Arch/Omarchy test system. Check
   dependencies, launcher (it opens the Omaframe window), icons, file opening,
   upgrade, uninstall and rollback. Preserve captures, drafts and settings
   through upgrade and uninstall. Document removal of the marked shortcut
   block.
6. Review `namcap`, licenses, README claims, exact source and binary hashes,
   and any unsupported behavior. Publish only the verified candidate and
   describe it as a screenshot and basic recording beta.

GPU Screen Recorder 6.1.0 is the minimum, in the package and at runtime. It is
the version Omarchy's stable and RC channels serve, and it supports every
option Omaframe passes, including `-write-first-frame-ts`, which Omaframe needs
before reporting Recording. Before raising this floor, check what those
channels serve (`stable-mirror.omarchy.org` and `rc-mirror.omarchy.org`), or
stable Omarchy users cannot install the package.
