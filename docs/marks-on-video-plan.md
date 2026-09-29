# Marks on video, secret hiding, and edge room

Date: 2026-09-28

Status: all phases are built. Where the build differs from this plan, a note in the phase says why.

## Goal

Three additions that keep Omaframe fast and small:

1. The screenshot marks (blur, redact, arrow, text and the rest) work on recordings too, during review, with a start and end time.
2. After a screenshot, Omaframe looks for secrets in the background and offers to hide them with one key.
3. Captures with a flat colored edge get a little extra room automatically.

The rule for all three: the quick path (Print, click, number, paste) gets no new steps. Each new feature is either automatic or one key, and nothing is shown when there is nothing to do.

## Order

| Phase | What | Why this order |
| --- | --- | --- |
| 0 | Pull the mark model and canvas out of `Studio` and `Main.qml` | Video marks need the same tools without copying 800 lines |
| 1 | Blur and redact on video | Fixes a real problem today: a recording that shows a secret has to be redone |
| 2 | Arrows, text and the other marks on video | Same machinery as phase 1 plus a PNG overlay |
| 3 | Secret hiding and copy text for screenshots | Independent of video, can be built in parallel with 1 and 2 |
| 4 | Automatic edge room | Small renderer change, any time |

## Phase 0: a reusable mark document

Today the marks live inside `Studio` (`m_edits`, undo and redo, selection, hit testing, text editing, the `Q_INVOKABLE` edit methods in `src/studio.hpp`), and the canvas that draws and drags them is inline in `qml/Main.qml` (around lines 900 to 1160), bound directly to `studio`.

1. Move the mark state and its methods into a new `MarkDocument` class (`src/marks.hpp`, `src/marks.cpp`): the edit list, the undo and redo stacks, selection, `edit`, `addStroke`, `hitAt`, `selectAt`, move, resize, nudge, duplicate, delete, layer order, the text-edit methods, and the selected mark's color, size and text settings. It needs a base image for `annotationBounds` and text sizing, so it holds one.
2. `Studio` owns a `MarkDocument` and exposes it as `studio.marks`. QML and the tests call the mark methods there instead of on `studio`. The document reports `edited` and `message` signals, and `Studio` turns them into a re-render, a dirty draft and a status line. Crop stays in the edit list, because the undo history and saved drafts already treat it as an edit. Video simply does not offer the crop tool.
3. Move the canvas into `qml/MarkCanvas.qml` with `doc`, `tool`, `locked`, `workingSize` and `sourceSize` properties and a `toolRequested` signal. `Main.qml` uses it with `doc: studio.marks`.
4. Done when the existing renderer, pipeline and manual editor checks pass with no visible change.

This is the riskiest refactor in the plan. Keep it a pure move with no behavior change, in its own commit.

## Phase 1: blur and redact on video

### Model

Add `start` and `end` (seconds on the source clip) to `Frame::Edit`. Screenshots ignore them. Video marks are stored in source time, so trims and cuts never move them.

`Video` owns a second `MarkDocument`. Its base image is a blank image the size of the frame, since the marks only need the size (for text in phase 2). Coordinates are already normalized from 0 to 1, so the marks do not depend on the video's size.

Marks live for the review session only. There are no drafts for video in this version.

### Interaction

In review, pressing a tool key (G for blur, R for redact, the same keys as the editor) pauses playback and puts `MarkCanvas` over the video at the current frame. Drawing works exactly as it does on a screenshot. Esc returns to select, then out of marking, like the editor.

- Blur and redact cover the whole clip by default. That is the safe choice: a secret that shows for two seconds is easy to miss if the mark only lasts one.
- The selected mark shows its time range as a thin bar under the timeline, with drag handles on both ends.
- With a mark selected, I and O set that mark's start and end at the playhead. With no mark selected they keep trimming the clip. Delete already works this way (it removes whatever is selected), so this is consistent.
- During playback, each mark shows only inside its range.

### Keys and undo in review

Review already binds Space, I, O, Delete, Left/Right, Home/End and Ctrl+Z in `qml/VideoPane.qml` (around line 295), and it keeps its own undo stack in QML for trims and cuts. Two things have to be settled before phase 1 is built:

- One undo history. Ctrl+Z in review has to undo the last change, whether it was a cut or a mark. Either the review stack records mark changes as entries that call into the `MarkDocument`, or cuts move into C++ next to the marks. The first is smaller.
- Arrow keys. With a mark selected, Left/Right move the mark, like the editor. With nothing selected, they keep stepping through time.

### Preview

Redact is a plain filled rectangle in QML. Blur previews with `MultiEffect` over a `ShaderEffectSource` of the `VideoOutput` limited to the mark's rectangle. The preview does not have to match the export pixel for pixel, but the export must be at least as blurred.

### Export

`Video::exportClip` and `exportEdited` in `src/video.cpp` build an ffmpeg command. Marks change it like this:

- If there are any marks, skip the stream-copy path (`streamCopy` at `src/video.cpp:419`). Marked clips are always re-encoded.
- Apply the marks to `[0:v:0]` first, before trim and concat, so the times in the filters are source times and cuts need no conversion.
- Redact: `drawbox=x:y:w:h:color=0x151a20:t=fill:enable='between(t,START,END)'`, using the same color as the screenshot redaction.
- Blur: `split` the stream, `crop` the rectangle, `gblur` with a sigma of three times the screenshot radius (`Frame::blurRadius`), and `overlay` it back with the same `enable` range. One chain per blur mark. A sigma equal to the screenshot radius left the text in a 720p recording almost readable while the preview looked fully hidden, so the export is deliberately stronger than the preview.
- The trims currently read `[0:v:0]` several times, which ffmpeg allows for inputs. After the mark filters, the result is a filter output, so it needs `split=N` before the trims.
- Convert normalized coordinates to pixels using the probed video size, rounded to even numbers so the rectangles line up with yuv420p chroma.

Put the filter-graph builder in a free function (`videoMarkFilters(edits, size)`) so it can be tested without a player.

### Tests

- Unit tests for the filter builder: one blur, one redact, several of each, marks that end before a cut, marks inside a removed range, odd video sizes.
- A pipeline test that records or generates a short clip with a known colored square (ffmpeg `testsrc` works), exports with a redact over it, and checks that the square's pixels in the output are the redact color inside the range and unchanged outside it.
- Manual check in omabox: record a window showing a fake token, blur it, cut the middle, export, and watch the result.

## Phase 2: other marks on video

Arrow, box, text and steps, as icon buttons after Blur and Redact with the keys A, B, T and N. Line, oval, highlight and pen stay on screenshots: O already sets the end of a clip in review, and four tools cover pointing at something in a recording. Marks use the screenshot defaults; there are no color or size controls on video.

- Default range: from the playhead to the end of the clip. Most marks point at something that stays on screen, and one rule for every mark is easier to learn than a different default per tool. To end a mark earlier, select it and press O at the moment it should disappear, or drag the end of its bar under the timeline. There is no duration field or "until end" toggle.
- While a mark is selected and its range has not been changed, the status line says "Shows until the end. Press O to end it here." This is the only place the shorter option is explained, and it only appears when it applies.
- Export: each mark is drawn alone with `Frame::applyEdits` on a clear frame, cut down to the pixels it covers, and saved as a PNG in a temporary folder. Each PNG is an FFmpeg input, laid over the video with `overlay` at its place and with its own `enable` range. Blur and redact stay as filters from phase 1 and come first, so an arrow drawn over a blurred area stays sharp. One picture per mark instead of one per time range keeps the preview and the export the same pictures.
- Preview: the same pictures, served to QML through the `videomarks` image provider and shown while the playhead is in range.
- Step numbers count every step mark in the clip, not only the visible ones, so the numbers match what was drawn.

No keyframes, no motion, no fade in or out, and marks do not follow moving content. Say so in the README next to the existing "There is no pause, webcam overlay..." line, and remove "annotation on video" from it.

## Phase 3: secret hiding and copy text for screenshots

### Detection

When a screenshot area is chosen, start OCR in the background while the finish picker is showing. Never make the picker wait for it. If the user picks a finish before OCR finishes, the result is saved as normal and the OCR result is thrown away.

- Run `tesseract` with TSV output to get word boxes, using `${OMARCHY_OCR_LANGS:-eng}` like `omarchy-capture-text` does. Run it at low priority, like the video export.
- Match these, joining neighboring words on a line first so a token split by OCR is still found:
  - Emails
  - JWTs (three base64url parts, the first starting with `eyJ`)
  - AWS access keys (`AKIA` or `ASIA` plus 16 characters)
  - GitHub tokens (`ghp_`, `gho_`, `ghs_`, `ghu_`, `github_pat_`)
  - Slack tokens (`xoxb-`, `xoxp-`), Stripe keys (`sk_live_`, `rk_live_`), Anthropic and OpenAI style keys (`sk-ant-`, `sk-`)
  - Private key headers (`-----BEGIN ... PRIVATE KEY-----`): hide the header and every line below it until the END line
  - Card numbers that pass the Luhn check, IBANs that pass the mod 97 check
  - Public IPv4 addresses (not private, loopback or link-local ranges)
- Leave out phone numbers, dates, version strings and hashes. They cause too many false matches for an automatic feature.
- Allow for common OCR mix-ups (`0`/`O`, `1`/`l`/`I`) inside known prefixes, so `ghp_` read as `9hp_` is still caught.
- Put the patterns in a plain function (`findSecrets(words) -> rects`) with no Qt Quick dependency, so it is easy to test.

### Interaction

- If something is found, the picker shows one line: "2 possible secrets. H hides them." Nothing appears when nothing is found.
- H adds a normal redact mark over each match, grown a few pixels in each direction. They are ordinary marks: they show in the preview, are undoable, and can be moved or deleted in the editor. Then the user picks a finish as usual.
- The same action is in the editor as "Hide secrets" with the same key.
- Wording never claims the image is clean. OCR misses things, and the hint says "possible secrets", not "all secrets".

### Copy text

The OCR result is already there, so Ctrl+Shift+C in the picker copies the text and closes nothing. If OCR has not finished, it waits for it and shows "Reading text...".

### Privacy and packaging

- OCR text and match results stay in memory. They are never written to disk or into drafts.
- `tesseract` and `tesseract-data-eng` become optional dependencies in the PKGBUILD. Without them, the feature is simply absent: no hint and no error.

### As built

- Keys: H in the picker, Shift+H in the editor (H is already the highlight tool there), and T in the picker for copying text. Ctrl+Shift+C already means "copy and save" in the main window, so using it for text in the picker would have meant two things in two windows.
- The picker shows a "Hide N possible secrets" button with an H key label, like the Edit button and its E, rather than a line of text.
- Tesseract runs with `--psm 6`, as `omarchy-capture-text` does. Automatic layout read aligned terminal output column by column and dropped private key headers.
- Captures up to about 4 megapixels are doubled before reading, and tall ones are read in up to four overlapping bands at once. Numbers are in `docs/capture-workflow-validation.md`.
- Besides the header rule, two or more lines in a row of one long base64 run (not hex) count as a private key body, for keys whose header is off screen.
- A secret counts as hidden once a redaction or blur covers 90 percent of it, so undoing the redactions brings the button back.

### Tests

- Pattern tests with positive and negative cases for every rule, including near misses (a 16-digit number that fails Luhn, a Git commit hash, a version like `1.2.3.4`, `127.0.0.1`).
- An OCR test that renders fake secrets with `QPainter` into an image in a few fonts and sizes, runs tesseract, and checks that the rectangles cover them. Skip it when tesseract is not installed.
- Timing check: OCR on a 4K capture must not delay the picker or the save. Record the numbers in `docs/capture-workflow-validation.md`.

## Phase 4: automatic edge room

Some window captures end right at their content, which looks cramped inside a frame. When a capture's outer edge is a single flat color, `Frame::compose` extends the image outward with that color before rounding the corners and adding the shadow.

- Sample a strip a few pixels wide along each edge. Count an edge as flat only if nearly all of it is within a small color distance of its median.
- Extend flat edges only, by an amount tied to the image size (start around 3 percent of the short side, capped), and never on Raw.
- No setting. If it looks wrong on real captures, tune the threshold instead of adding a control.
- Renderer tests: a flat-edged image gets wider, a photo-edged image does not, and Raw is unchanged.

### As built

- An edge only gets what it is missing: the room is 4 percent of the short side, at least 12 and at most 48 pixels, minus how far the flat color already runs inward. A capture that already has room, or is one color throughout, keeps its size. At 3 percent a 300 pixel dialog gained 9 pixels, too little to notice.
- The first and last 16 pixels of each edge (a tenth on short edges) are left out of the flatness check, so a rounded window corner showing the wallpaper does not make the edge busy.
- Captures shorter than 120 pixels are left alone.
- `Frame::outputSize` takes the room, so the size shown before saving and the 80 megapixel check match the saved file.

## Left out on purpose

Code cards, several shots on one card, a magnifier, a gradient editor, saved presets, aspect ratio buttons, a title bar, and a scripting API. Also crop on video, marks that move, and per-mark animation. Each one would add a panel or a decision to the quick path.

A finish built from the current Omarchy theme colors would fit, but it would be a tenth finish and the picker has keys 1 to 9. Decide separately whether it should replace one of the existing finishes.

## Open questions

1. Should the secret hint in the picker also be read out through the notification after a quick capture, for people who paste without looking at the picker?
2. Should H also be offered in video review after a recording, using OCR on sampled frames? That is a much larger job and is not in this plan.
