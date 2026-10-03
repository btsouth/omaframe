# Releasing Omaframe

Omaframe 0.8.1 is a screenshot and recording beta.
See [the release notes](docs/release-notes-0.8.1.md) for what ships.

Releases use automated regression checks and accurate notes about known
limitations. Manual monitor power changes, hardware acceptance sessions and
fresh-desktop walkthroughs are not required from the owner. Unusual setups
are handled through bug reports and targeted fixes. Existing validation
documents preserve what was actually tested; they do not define additional
release blockers.

## Build an exact package

`packaging/build-package.sh` requires a clean checkout at the tag matching
`CMakeLists.txt`, currently `v0.8.1`. It archives that tag, computes the
source SHA-256, fills `packaging/PKGBUILD.in`, and runs `makepkg` without
installing dependencies or changing the host package database. The output
contains the source archive, a resolved PKGBUILD, and an Arch package.

For a disposable package from current uncommitted work:

```sh
packaging/build-package.sh --working-tree /path/to/output
```

The script labels this as a local working-tree package. Do not publish it as
the tagged release. Record the source archive and package SHA-256 before
sharing a candidate.

## Automated checks

- GitHub CI builds the app and all test binaries on Arch, runs eleven
  headless suites plus OCR pattern tests, and checks the installed desktop entry, icon and licenses.
  See [docs/ci.md](docs/ci.md) for coverage and local commands.
- Run the complete thirteen-suite CTest set in an isolated omabox for changes
  to capture, clipboard or desktop integration. These tests use a simulated
  recorder and fixture camera frames; they do not establish real GPU,
  microphone or physical camera behavior.
- For scrolling capture, run `python tests/scrolling-e2e.py /path/to/omaframe`.
  It creates its own isolated desktop and checks a real Chromium page,
  interruptions, annotation placement, cropping, undo and reopening.
- When changing packaging, check dependency resolution and the affected
  install, upgrade or uninstall path in a disposable Arch environment.
  Keep captures, drafts and settings intact.

## Publish

Prepare the version, tag and concise release notes. Build from the exact tag,
record checksums, and attach the source archive, resolved PKGBUILD, versioned
package, stable `omaframe-x86_64.pkg.tar.zst` alias and `SHA256SUMS`.
Download the published assets back and verify their checksums. Describe
validation performed and known limitations without treating untested
hardware combinations as release blockers.

The [package repository](https://github.com/btsouth/pkgs) picks up a
published release within the hour and serves the versioned package to users
through pacman. Run `gh workflow run publish.yml -R btsouth/pkgs` to publish
it straight away.

Publishing still requires the owner's instruction. CI does not tag, install
on the owner's machine or publish releases.

GPU Screen Recorder 6.1.0 is the minimum, in the package and at runtime.
Before raising this floor, check what Omarchy's stable and RC channels serve
(`stable-mirror.omarchy.org` and `rc-mirror.omarchy.org`) so supported users
can install the package.
