# Local Arch package validation, 2026-09-27

This is a historical validation record. Results and open checks apply to the
versions named below. Untested configurations are coverage limits, not pending
owner tasks; see [the current release policy](../RELEASING.md).

The uncommitted working tree was archived with
`packaging/build-package.sh --working-tree`. `makepkg` verified the source
SHA-256 and built `omaframe-0.2.0-1-x86_64.pkg.tar.zst` without installing on
the host. The package contains the binary, desktop entry, icon, Omaframe MIT
license, and retained Omasnap MIT license. `desktop-file-validate` passed and
`ldd -r` reported no missing or undefined libraries. `namcap` reported only
standard implicitly supplied libraries and dependencies used through CLI or
QML at runtime.

The extracted package binary launched in isolated omabox at 1366×768. It
captured a native 750×430 region, saved a PNG, and copied identical PNG bytes
to the clipboard. No QML runtime error appeared in the app log.

An isolated Arch Linux base container synchronized the current core and extra
repositories, installed the declared runtime dependencies with signature and
dependency checks enabled, then installed the local package with `pacman -U`.
`pacman -Qkk omaframe` reported 15 files with none altered, `ldd` found no
missing shared libraries, and the package binary matched the rebuilt artifact.
The repository supplied `gpu-screen-recorder` 6.1.3-1. A package reinstall
preserved capture and settings sentinels. The clean install exposed two tools
used directly by Omaframe, `pgrep` and `xdg-open`, so `procps-ng` and
`xdg-utils` were added as explicit dependencies. The container does not test
GPU capture, a logged-in desktop, or an Omarchy upgrade.

Pacman installed and removed the package in a temporary root with dependency
checks disabled because that root had no repository database. It registered
version 0.2.0-1, removed its binary and desktop entry on uninstall, and left
an unrelated sentinel file intact. This tests package paths and removal, not
fresh dependency resolution on a supported system.

The last committed source was also packaged locally as 0.2.0-0. Pacman
installed that baseline in a separate temporary root, upgraded it to the
working-tree 0.2.0-1 candidate, then downgraded it back. The binary SHA-256
changed to the candidate and returned to the baseline value. A capture-file
sentinel and settings-file sentinel retained their SHA-256 values through
upgrade, rollback, and uninstall. Dependency checks were disabled because the
temporary root had no repository database. This does not replace a fresh
system install and upgrade with real dependencies or a real user profile.

The build is a local candidate, not a clean tagged release. Exact source and
package hashes should be recorded again after the final working-tree changes
or when building from a release tag. Physical GPU capture and both-monitor
selection remain unverified.

## Local package and installed binary, 2026-09-27 18:07

After export validation and the owner's silent recording inspection, the
working tree was packaged again. Source archive SHA-256:
`203303c625578a3d5efd506229a8907641de1d86e9112a6d112eea64d9af7c61`.
Package SHA-256:
`52842fe686b2ed74be2bb376e8e3b4e77614740d8c1ad67966fcf399da7a4cb1`.
The packaged binary SHA-256 is
`e5bc414b1936839bb080edda9689bf8bc249acf431799b6e5e85a53aabf1b961`,
which matched `~/.local/bin/omaframe` after that install. The
previous binary was backed up as `~/.local/bin/omaframe.bak-20260927-1808`.
No app was launched on the owner's desktop during installation.

All five CTest suites passed in omabox before packaging. `desktop-file-validate`
passed on the packaged desktop entry; `ldd -r` found no missing libraries or
undefined symbols in the installed binary. `namcap` reported implicitly
supplied libraries and runtime dependencies accessed through QML or external
commands. This run did not check a fresh Omarchy session with real hardware
or an upgrade path.

## Local package and installed binary, 2026-09-27 18:28

After the silent-video setup and status fix, the working tree was packaged
again. Source archive SHA-256:
`7e4689ae0bbb4a58bca2cd4ed5887e9bbff518665afb9c575f92387df6f50d7a`.
Package SHA-256:
`38f249b29cdb48cd1e51fa1c27321a43da25b2fc6ea1966cdd60ac6b2fa4e605`.
The packaged binary SHA-256 is
`45bb186f6fefadd026e7392b70e434dd28035841760d8293fa82e949fcc8c55f`,
which matched `~/.local/bin/omaframe` after that install. The preceding binary is backed up as
`~/.local/bin/omaframe.bak-20260927-1828`. All five CTest suites passed in
omabox before packaging. This remains a local working-tree build.

## False-timeout fix package, 2026-09-27 18:40

The working-tree source archive SHA-256 was
`325c5f4ca1cbced6e78e30af0035439552ada700dc0f22132eeb224bf6ed367f`.
The package SHA-256 was
`29a057e6c85c1bb3a17c78880f3f90a80af283501241eae760acd185b3a53b76`.
Its binary SHA-256 is
`86b251e1395f7fc9097b369295af6e9b62bf92820afe25de5b1821fd66091974`,
which matched `~/.local/bin/omaframe` after that install. The previous binary is at
`~/.local/bin/omaframe.bak-20260927-1840`. The five CTest suites passed in
omabox, including the two-line timestamp marker fixture. The real desktop
app was not launched or focused during installation.

## Faster-export package, 2026-09-27 18:59

The working-tree source archive SHA-256 was
`b386f60aa270d7b30d7c5b8d1d6166d7252c3d97f27f44e5d50726a4316ed5e0`.
The package SHA-256 was
`c1d2213b3d55019b2af129ef2bf06144d106b610309eb9d723b478a50ea42b00`.
Its binary SHA-256 is
`4e4256d7a838b902fa2e987e8684c1f5d1d5f8771f29aa04afee574877525329`,
which matched installed `~/.local/bin/omaframe` after that install. The preceding binary is at
`~/.local/bin/omaframe.bak-20260927-1859`. All five CTest suites passed in
omabox. The package remains a local working-tree build.

## Final faster-export local package, 2026-09-27 19:02

The working-tree source archive SHA-256 was
`ff63ec2d1e2ae566d5d13781b01b65ba3113189f6d6ce43208fdc493878e5d94`.
The package SHA-256 was
`ec19f44fb721f4273b988c1278634a23f38bc4f4e47c86f22293f8ea8b10928a`.
Its binary SHA-256 is
`24423e19e98f9f5667d4fc289bc13239dcad2b35fe62ca7bfdf2b7d2c82ae938`,
matching installed `~/.local/bin/omaframe`. The preceding binary is at
`~/.local/bin/omaframe.bak-20260927-1902`. The pipeline suite passed in
omabox after the final end-trim precision adjustment. The other four suites
passed before that one-line adjustment. This remains a local working-tree
package.

## Recording shortcut fix, 2026-09-27 20:34

The working-tree package SHA-256 is
`c4180d2e2ecb69890022419c625e607a48215a8c1567dbb3ef38b0398bb9a063`.
Its binary SHA-256 is
`c9374768f7025c85b481967d00362acfd14984d69d4684446075e173af805a99`,
matching `~/.local/bin/omaframe`. The previous binary is backed up as
`~/.local/bin/omaframe.bak-20260927-shortcut-onboarding`. All five test
suites passed in omabox. The recording suite includes a simulated first-use
shortcut install and an Omarchy Lua binding. An isolated UI check showed
that a live Lua Alt+Print binding enables full-display recording. Omabox uses
its own Hyprland config, so the automatic config-file path was verified by
the simulated test rather than a physical desktop run. The real desktop
config was not changed or reloaded during this install.

## Interface pass package, 2026-09-27 22:12

Built from the working tree with `packaging/build-package.sh --working-tree`.
Source archive SHA-256:
`73868768c5991f66d9a71b40146b2ccce2cd3e57d01ff67609aded4b604db269`.
Package SHA-256:
`7ffde1b775520f30778ecc5f9274a8aeaed45692a3a5474cdb1bc3925500defb`.
Packaged binary SHA-256:
`1fef477e866252364df480398895474622a3c2c163ff55b9d81ee7b63605eff0`, now
installed as `~/.local/bin/omaframe`. The previous binary (`915b0beb…`) is at
`~/.local/bin/omaframe.bak-20260927-interface-pass`. The older
`~/.local/share/applications/io.github.btsouth.omaframe.desktop` from an earlier
user-local install was left as it was; the packaged entry now opens the
Omaframe window.

The package holds the binary, the desktop entry, the icon and both licenses.
`desktop-file-validate` passed, `ldd -r` found nothing missing, and `namcap`
reported only implicitly satisfied libraries and runtime dependencies used
through commands or QML. `libnotify` is an optional dependency. All five CTest
suites passed in omabox before packaging. The extracted package binary ran in
omabox at 1366×768: start screen, a whole-display capture from the open window,
finish 9, a return to the window, and clipboard bytes equal to the saved PNG.
No app was launched on the real desktop.

## Review-copy package, 2026-09-27 22:25

After recording review was changed to copy the video and close, the working
tree was packaged again. Source archive SHA-256:
`3208c72a44640ebb14989c9ca13ba77d2b016519949410739cead99ffabf15fb`.
Package SHA-256:
`6cd901dd1beb904cf0eafc8536afc9914295a6634560a6c09bb385a3f4dbcebb`.
Packaged binary SHA-256:
`0dfaa27c4c09d3c1b228a47c6f4e6944b974a65bfc1da474666f73effbad5b03`, now
installed as `~/.local/bin/omaframe`. The previous binary (`1fef477e…`) is at
`~/.local/bin/omaframe.bak-20260927-review-copy`. All five CTest suites passed
in omabox, `desktop-file-validate` passed, `ldd -r` found nothing missing, and
`namcap` showed only the usual warnings. In omabox, the extracted package
binary recorded an area with the stub recorder, trimmed it, and closed on
Save and copy with the edited file on the clipboard.

## Real-desktop test package, 2026-09-27 23:10

Built after the fixes found on the owner's desktop (recorder process name for
the Omarchy bar, F following the pointer, review sized from its own monitor
and moved off special workspaces, display capture name). Source archive
SHA-256: `faba952f02cc892e2bedd6d47a1a56bead9b1a269bd1309c208a86d93c0ea533`.
Package SHA-256:
`b127751ba312e73e567d585027e711f6a852e41e1a9fce46e87330a4cd1e51ce`.
Packaged binary SHA-256:
`f0c32904992ff55ed6a6ceb2766f041c02fac14ec99a8d123d8bbe2a3849da64`, installed
as `~/.local/bin/omaframe`. Earlier binaries are at
`~/.local/bin/omaframe.bak-20260927-before-barname` and
`omaframe.bak-20260927-review-copy`. All five CTest suites passed in omabox;
`desktop-file-validate`, `ldd -r` and `namcap` were clean apart from the usual
warnings.

## Fresh-system lifecycle, 2026-09-27 23:30

In a fresh `archlinux:base` Docker container with signature checks and
dependency resolution: `pacman -Syu`, then the last committed source packaged
as 0.2.0-0 installed with all dependencies from core and extra (969.56 MiB,
including hyprland 0.56.2, qt6-base 6.11.2 and gpu-screen-recorder 6.1.3).
A test user got sentinel captures, videos, settings, a draft and a
`bindings.lua` with an Omaframe block. Upgrading to the candidate 0.2.0-1
(binary `f0c32904…`) reported 15 files and none altered. The binary linked
fully, ran `--version` and `--help` offscreen, and every tool it calls was
present (`notify-send` absent, as an optional dependency). The desktop entry
validated. A rollback to 0.2.0-0, a reinstall and `pacman -R` all worked; the
package's files were removed and every sentinel, including the shortcut block,
was byte-for-byte unchanged after upgrade and after uninstall. This does not
test a logged-in desktop session.

## Review-fix package, 2026-09-27 23:50

After the code review fixes. Source archive SHA-256:
`7631b26ccf8a61c38a9efdefe57f9b337f04d6b5194eade66ff2d1e8a8b4572e`.
Package SHA-256:
`38452f52783b20557037ff23330a049c898a423fc7a286d899c8bd8900c934f0`.
Packaged binary SHA-256:
`3db9fcd50b935d68c97ad2e121933f20d43a50c738756099b095108b248181fa`, installed
as `~/.local/bin/omaframe`; the previous binary is at
`~/.local/bin/omaframe.bak-20260927-real-test`. All five CTest suites passed
in omabox; `desktop-file-validate`, `ldd -r` and `namcap` were clean apart
from the usual warnings. The lifecycle container run above used the previous
candidate; the changes since then are in application code only, not package
layout.

## Release v0.2.0, 2026-09-28

Built from tag `v0.2.0` (commit `5a98612`) with `packaging/build-package.sh`
and published as a GitHub pre-release at
https://github.com/btsouth/omaframe/releases/tag/v0.2.0.

| File | SHA-256 |
| --- | --- |
| `omaframe-0.2.0-1-x86_64.pkg.tar.zst` | `c145c28a10ff510e61449ccdba39e1e6b560ac2ba0f1c1e2eace2a4d3bf4758f` |
| `omaframe-0.2.0.tar.gz` | `5cc2ccf37d47a2a8d7c6fafed77302f9f2b3e7bfcaa6e0a90b5ed1fa101dc956` |
| `PKGBUILD` | `3bc8b208c47b71add2762dba0a4b19c74363864875f34add1e1381cdd2c62c28` |
| `usr/bin/omaframe` in the package | `52eebbbd742569e99c2ea0edff0f3e4f522fa5d03a0fe9ac93545b1081b569bc` |

`desktop-file-validate`, `ldd -r` and `namcap` were clean apart from the usual
warnings. The assets were downloaded back from the release and matched
`SHA256SUMS`. The published `PKGBUILD` alone downloaded the source archive from
the release, passed its checksum and built the package. The release binary is
installed as `~/.local/bin/omaframe`.

## Release v0.2.1, 2026-09-28

Published as the latest full release, with the opaque-window fix and a stable
asset name for a one-line install. Package SHA-256
`1e1d8abda876672891a1f1a93e8082e54c1ecb5860453002c80944761034abd9` (also
attached as `omaframe-x86_64.pkg.tar.zst`), source archive
`58f9b2c9067acd4e49054df06a1473890370b67205332572f08efa4474da50fe`, binary
`6a35a33b886bf4bcc7e8e7422c08b32d992ff96fa97ef464f0edc66fe24d52c3`. All five
CTest suites passed. In a fresh `archlinux:base` container the README command
downloaded `releases/latest/download/omaframe-x86_64.pkg.tar.zst`, installed
0.2.1-1 with its dependencies, and `omaframe --version` reported 0.2.1.
`pacman -U` directly on the URL is not offered because pacman requires a
signature for remote packages and the package is unsigned.

## Release v0.2.2, 2026-09-28

A user installing 0.2.1 from the announcement hit `unable to satisfy
dependency 'gpu-screen-recorder>=6.1.3'`. Omarchy's package channels serve
different GPU Screen Recorder versions: stable and RC serve 6.1.0 from their
`extra` snapshots (and 5.12.3 in the `omarchy` repo, which comes after
`extra`), and only edge serves 6.1.3. The 6.1.3 floor was a conservative guess.
6.1.0 supports every option Omaframe passes and writes the same first-frame
file (`<output>.ts` with a `monotonic_microsec realtime_microsec` header), so
0.2.2 requires `gpu-screen-recorder>=6.1.0` in the package and at runtime. The
recording suite now accepts 6.1.0 and refuses 6.0.9.

In a fresh `archlinux:base` container pointed at `stable-mirror.omarchy.org`
with the stable `omarchy` repo added, the README command installed Omaframe
0.2.2-1 with gpu-screen-recorder 6.1.0-1, and `omaframe --version` reported
0.2.2. A real GPU recording with 6.1.0 has not been made; 6.1.3 was the version
used for the hardware checks.
