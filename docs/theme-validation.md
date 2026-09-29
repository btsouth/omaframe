# Omarchy theme validation

This is a historical validation record. Results and open checks apply to the
versions named below. Untested configurations are coverage limits, not pending
owner tasks; see [the current release policy](../RELEASING.md).

Checked on 2026-09-26 in omabox. No real-desktop theme changes were made.

## What changed

Every color in the QML chrome was hardcoded to one green palette (about 160 literals), fonts were Inter, and corners were 7-20 px. `src/omarchy-theme.cpp` now provides theme roles to QML as `theme`, and the application palette follows it, so stock controls and tooltips in every window match too.

- Palette from `~/.local/state/omarchy/current/theme/colors.toml`, with the shell's fallbacks (`color0/7/4/1/8`) and defaults for a desktop without Omarchy.
- Surfaces from the theme `shell.toml` plus `~/.config/omarchy/shell.toml`: menu background, text, frame and scrim; popup frame (Hyprland's active border); `bar.active` for recording; `[controls]` fill and border alphas for normal, hover, pressed and selected states. References such as `hyprland.active-border`, palette roles and Hyprland gradients resolve as in the shell.
- Secondary text is blended toward the background only as far as a contrast target allows (4.5:1, or proportionally less for low-contrast themes). Theme `muted` values are often border tones (Matte Black's `#333333` on `#121212`). Text on accent and recording fills picks the more readable theme color, or black/white.
- Corners come from `hyprctl getoption decoration:rounding`; the font is the `monospace` alias; the switch is square when corners are sharp, like the shell's.
- `omarchy-theme-set` deletes and replaces the theme directory, so the parent directory and files are watched, changes are debounced, and watches are re-armed after each reload.
- Finish previews, rendered images and exported files are unchanged. The editor's drag guides use theme accent/urgent colors; the rendered annotations do not.

## Checks

- `theme` test suite: color forms and gradients; all 22 stock themes (background, text and accent match `colors.toml`; light/dark mode; muted, faint and on-accent contrast); a live switch performed by remove-and-rename, an in-place `colors.toml` edit, and a second switch after re-arming; shell references, recording color, scrim alpha, control alphas and user overrides; defaults without Omarchy. Full ctest: 4/4 pass.
- Capture bar before/after and mid-drag in Catppuccin Latte, Matte Black, White, Hackerman, Rosé Pine and Everforest: `evidence/theme-capture-bar-before-after.png`, `evidence/theme-capture-bar-gallery.png`. Recording selection uses the recording color: `evidence/theme-recording-selection.png`.
- Studio open while switching Osaka Jade → Catppuccin Latte → Tokyo Night; it updated live without restarting: `evidence/theme-studio-latte-live.png`, `evidence/theme-studio-tokyo-night-live.png`.
- Finish chooser and recording setup in Latte and Matte Black: `evidence/theme-chooser-latte-matte.png`, `evidence/theme-setup-latte-matte.png`. Recording control in its countdown state: `evidence/theme-recording-control-countdown.png`.

## Not verified here

- The live recording state (pulsing dot and Stop). GPU Screen Recorder cannot start in omabox without a GPU, so recording failed after the countdown, as expected there. The same component and colors are used.
- Rounded corners. The box and the owner's Hyprland use `rounding = 0`. A rounding change is read at launch and on theme switches, not on a Hyprland config reload alone.
- A font change through `omarchy-font-set` applies on the next launch, as fontconfig is read at startup.

## Recording setup on the right monitor (2026-09-26)

Owner report: choosing **Video** on the capture bar opened recording setup on the wrong monitor. Region capture shows a bar on every display, but `recordInstead()` did not record which one was used, so setup opened on the previous capture's monitor or the primary screen. The bar now passes its own display (click and Tab), and `--record` (Alt+Print) opens setup on Hyprland's focused monitor.

Verified in omabox with a second nested output (WAYLAND-2, 1280x720 at scale 2) using `hyprctl -j layers`: Video on WAYLAND-2 opened setup on WAYLAND-2; a following capture with Video on WAYLAND-1 opened it on WAYLAND-1, not the stale monitor; with WAYLAND-2 focused, a fresh `--record` opened on WAYLAND-2. The capture bar also overflowed that 640-logical-pixel display; it now drops the key hints, then the prompt, on narrow displays (`evidence/capture-bar-narrow-display.png`). Full ctest: 4/4 pass.
