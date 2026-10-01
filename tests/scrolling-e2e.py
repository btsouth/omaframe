#!/usr/bin/env python3
"""Real Wayland/Chromium scrolling smoke. Requires omabox and ImageMagick.

Run from the checkout: python tests/scrolling-e2e.py /path/to/omaframe
The private box is always torn down. Evidence is retained under $TMPDIR.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess


def run(*args):
    result = subprocess.run(args, text=True, capture_output=True)
    if result.returncode:
        print(result.stdout, result.stderr, flush=True)
        result.check_returncode()
    return result.stdout.strip()


def verify_page(path):
    dimensions = run("magick", "identify", "-format", "%w %h", str(path))
    width, height = map(int, dimensions.split())
    assert 1800 <= width <= 1900, dimensions
    assert height == 4456, f"Missing or repeated page content: {dimensions}"
    raw = subprocess.check_output([
        "magick", str(path), "-crop", f"1x{height}+800+0", "-depth", "8", "RGB:-"
    ])
    pixels = [tuple(raw[i:i + 3]) for i in range(0, len(raw), 3)]
    assert len(pixels) == height
    def near(pixel, color):
        # The compositor can round/color-convert a channel by a few values.
        return all(abs(a - b) <= 5 for a, b in zip(pixel, color))

    assert all(near(p, (23, 52, 84)) for p in pixels[:56]), "Header was obscured"
    assert all(near(p, (220, 227, 236)) for p in pixels[-32:]), "Footer was repeated or lost"
    runs = []
    start = None
    for y, colored in enumerate([max(p) - min(p) > 55 for p in pixels] + [False]):
        if colored and start is None:
            start = y
        elif not colored and start is not None:
            runs.append((start, y - start))
            start = None
    bars = [(y, h) for y, h in runs if h == 24]
    assert len(bars) == 60, f"Expected 60 rows, found {len(bars)}"
    assert bars[0][0] == 100 and bars[-1][0] == 4348, bars
    assert all(bars[i + 1][0] - bars[i][0] == 72 for i in range(59)), bars
    # The centered progress panel would contaminate this column's initial rows.
    center = subprocess.check_output([
        "magick", str(path), "-crop", "1x56+950+0", "-depth", "8", "RGB:-"
    ])
    center_pixels = [tuple(center[i:i + 3]) for i in range(0, len(center), 3)]
    assert all(near(p, (23, 52, 84)) for p in center_pixels), "Progress control leaked into the image"
    return {"width": width, "height": height, "rows": len(bars)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    repo = Path(__file__).resolve().parents[1]
    scratch = Path(os.environ["TMPDIR"])
    evidence = scratch / f"omaframe-scroll-e2e-{os.getpid()}"
    evidence.mkdir()
    box = f"omaframe-scroll-smoke-{os.getpid()}"

    def boxed(*command):
        # Avoid whole-second boundaries in omabox's timed status reader.
        # A timed-out partial read can otherwise drop the first bytes of its
        # successful answer. This still waits for actual rendered stability.
        command = tuple("350ms" if arg in ("1s", "2s") and
                        index > 0 and command[index - 1] == "--quiet" else arg
                        for index, arg in enumerate(command))
        return run("omabox", command[0], "-b", box, *command[1:])

    try:
        run("omabox", "up", box, "--size", "1920x1080@60", "--no-shell",
            "--net", "isolated", "--ro-bind", str(binary.parent), "--ro-bind", str(evidence))
        box_root = Path(boxed("path"))
        boxed("run", "-d", "--wait", "--", "chromium", "--no-first-run",
              "--disable-background-networking", "--user-data-dir=/home/sbx/scroll-fixture",
              f"--app=file://{repo / 'tests/fixtures/scrolling-page.html'}")
        boxed("run", "-d", "--wait", "--", str(binary), "--scroll")
        boxed("shot", "-o", str(evidence / "selector.png"))
        boxed("click", "400", "400")
        boxed("wait", "--timeout", "8s", "layer", "omaframe-scroll-control")
        boxed("shot", "-o", str(evidence / "control.png"))
        boxed("wait", "--timeout", "30s", "layer", "omaframe-finishes")
        boxed("wait", "--timeout", "15s", "still", "--quiet", "2s")
        boxed("shot", "-o", str(evidence / "chooser.png"))
        boxed("keys", "9")
        boxed("wait", "--timeout", "15s", "cmd", "--", "python", "-c",
              "from pathlib import Path; assert list(Path('/home/sbx/Pictures/Omaframe').glob('*.png'))")
        files = list((box_root / "home/Pictures/Omaframe").glob("*.png"))
        assert len(files) == 1, f"Expected one export, found {len(files)}"
        output = evidence / "raw.png"
        shutil.copy2(files[0], output)
        result = {**verify_page(output), "passed": True,
                  "evidence": str(evidence), "output": str(output)}

        # Interruptions use a focused on-demand panel so the browser keeps
        # receiving wheel events. Exercise S, Escape, Enter and Cancel for real.
        for mode in ("escape", "enter", "cancel"):
            boxed("keys", "--window", "title:Omaframe scrolling fixture", "Home")
            launch = "--capture" if mode == "enter" else "--scroll"
            boxed("run", "-d", "--wait", "--", str(binary), launch)
            if mode == "enter":
                boxed("keys", "s")
                boxed("shot", "-o", str(evidence / "selector-s.png"))
            before = len(list((box_root / "home/Pictures/Omaframe").glob("*.png")))
            boxed("click", "400", "400")
            boxed("wait", "--timeout", "8s", "layer", "omaframe-scroll-control")
            if mode == "cancel":
                boxed("click", "1070", "55")
                boxed("wait", "--timeout", "8s", "layer", "omaframe-scroll-control", "--gone")
                assert len(list((box_root / "home/Pictures/Omaframe").glob("*.png"))) == before
            else:
                boxed("click", "825", "50")
                boxed("keys", "Escape" if mode == "escape" else "Return")
                boxed("wait", "--timeout", "8s", "layer", "omaframe-finishes")
                boxed("wait", "--timeout", "8s", "still", "--quiet", "1s")
                boxed("shot", "-o", str(evidence / (mode + "-keeps.png")))
                boxed("keys", "9")
                boxed("wait", "--timeout", "10s", "cmd", "--", "python", "-c",
                      "from pathlib import Path; assert len(list(Path('/home/sbx/Pictures/Omaframe').glob('*.png'))) == " + str(before + 1))
                kept = max((box_root / "home/Pictures/Omaframe").glob("*.png"),
                           key=lambda p: p.stat().st_mtime_ns)
                kh = int(run("magick", "identify", "-format", "%h", str(kept)))
                assert 1000 <= kh < 4456, f"{mode} did not stop early: {kh}"
                boxed("wait", "--timeout", "5s", "layer", "omaframe-finishes", "--gone")
        result.update({"escape_keeps": True, "enter_keeps": True, "cancel_discards": True,
                       "selector_s": True})
        base_count = len(list((box_root / "home/Pictures/Omaframe").glob("*.png")))

        # Reopen the real capture in the actual editor. This also exercises
        # loading tall PNGs independently of the capture/chooser path.
        original = files[0]
        source = str(Path("/home/sbx/Pictures/Omaframe") / original.name)
        boxed("run", "-d", "--wait", "--", str(binary), source)
        boxed("hyprctl", "eval", "hl.dispatch(hl.dsp.window.fullscreen(1))")
        boxed("wait", "--timeout", "5s", "still")
        boxed("click", "--wait", "165", "100")
        boxed("pointer", "--", "move", "1200", "700", "scroll", "600")
        boxed("wait", "--timeout", "5s", "still")
        boxed("shot", "-o", str(evidence / "editor-bottom.png"))
        boxed("click", "1710", "155")
        boxed("pointer", "--", "move", "1040", "866", "down", "left",
              "move", "1180", "884", "up", "left")
        boxed("wait", "--timeout", "5s", "still")
        boxed("shot", "-o", str(evidence / "bottom-annotation.png"))

        def save(count):
            boxed("click", "--wait", "1840", "1044")
            boxed("wait", "--timeout", "15s", "cmd", "--", "python", "-c",
                  "from pathlib import Path; assert len(list(Path('/home/sbx/Pictures/Omaframe').glob('*.png'))) >= " + str(count))
            return max((box_root / "home/Pictures/Omaframe").glob("*.png"),
                       key=lambda p: p.stat().st_mtime_ns)

        annotated = save(base_count + 1)
        shutil.copy2(annotated, evidence / "annotated.png")
        bounds = run("magick", str(original), str(annotated), "-compose", "difference",
                     "-composite", "-alpha", "off", "-threshold", "0", "-format", "%@", "info:")
        geometry = bounds.replace("x", " ").replace("+", " ").split()
        bw, bh, bx, by = map(int, geometry)
        assert bw > 100 and bh > 10 and bx > 1000 and by > 4200, bounds
        verify_page(annotated)

        # Cropping must not shrink the uncropped page to a tiny sliver while
        # the crop tool is active. Undo must restore the exact annotated PNG.
        boxed("click", "1800", "118")
        boxed("pointer", "--", "move", "80", "660", "down", "left",
              "move", "1540", "954", "up", "left")
        boxed("wait", "--timeout", "8s", "still", "--quiet", "1s")
        crop_view = evidence / "crop-editor.png"
        boxed("shot", "-o", str(crop_view))
        pixel = subprocess.check_output([
            "magick", str(crop_view), "-crop", "1x1+200+740", "-depth", "8", "RGB:-"
        ])
        assert sum(pixel) > 550, "Crop tool collapsed the tall page instead of preserving scroll position"
        cropped = save(base_count + 2)
        cw, ch = map(int, run("magick", "identify", "-format", "%w %h", str(cropped)).split())
        assert 1500 < cw < 1884 and 200 < ch < 500, (cw, ch)
        shutil.copy2(cropped, evidence / "cropped.png")
        boxed("keys", "ctrl+z")
        boxed("wait", "--timeout", "8s", "still", "--quiet", "1s")
        restored = save(base_count + 3)
        changed = run("magick", str(annotated), str(restored), "-compose", "difference",
                      "-composite", "-alpha", "off", "-threshold", "0", "-format", "%[fx:mean]", "info:")
        assert float(changed) == 0, f"Crop undo changed the full-resolution annotations: {changed}"
        shutil.copy2(restored, evidence / "restored.png")
        restored_source = str(Path("/home/sbx/Pictures/Omaframe") / restored.name)
        boxed("run", "--", str(binary), restored_source)
        boxed("wait", "--timeout", "8s", "still", "--quiet", "1s")
        boxed("shot", "-o", str(evidence / "reopened.png"))
        result.update({"annotation_bounds": bounds, "crop": [cw, ch],
                       "crop_undo_exact": True, "reopened": True})
        print(json.dumps(result))
    finally:
        if "box_root" in locals():
            for log in (box_root / "home").glob("*.log"):
                shutil.copy2(log, evidence / log.name)
        run("omabox", "down", box)


if __name__ == "__main__":
    main()
