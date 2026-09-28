# Omarchy stable compatibility, 2026-09-28

The 0.2.3 patch removes the edge-only `o.rebind` dependency. Stable's
`omarchy-settings` 4.0.4 package contains `o.bind`; replacing a binding uses
`hl.unbind` first. The new preflight refuses unsupported APIs before writing
configuration. Tests reject the old helper and cover unchanged configuration
when the required API is missing, custom bindings, backups and marked-block
migration.

All five CTest suites passed inside omabox. In a 1366x768 isolated desktop,
we loaded helpers.lua extracted from stable's omarchy-settings 4.0.4-1 package
and explicitly removed o.rebind. First-run setup installed both shortcuts.
Reloading hypr.bindings kept exactly one screenshot and one recording binding,
with no configuration errors. This checks stable helper compatibility on the
host compositor; it is not a fresh stable desktop or real GPU recording test.

Stable mirror versions checked: Hyprland 0.56.2-2, Qt base 6.11.2-3,
Qt declarative 6.11.2-1, Layer Shell Qt 6.7.4-2, FFmpeg 2:9.0.1-4,
GPU Screen Recorder 6.1.0-1. The runtime dependencies installed in an isolated
Arch container using stable-mirror.omarchy.org with package signatures enabled.

The stable recording indicator checks the same bare gpu-screen-recorder
process name and uses the same stop command as edge. Omaframe already supplies
that process name. Window placement dispatches have legacy fallbacks. Capture
uses Wayland protocols supplied by the shared Hyprland 0.56.2 version. The
recorder's 6.1.0 floor and argument coverage remain covered by the recording
suite; physical capture/audio on that recorder version remains unverified.

No additional stable/edge mismatch was found in this bounded integration audit.
