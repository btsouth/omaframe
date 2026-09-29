# Continuous integration

The [CI workflow](../.github/workflows/ci.yml) runs on pull requests, pushes
to `main`, and manual dispatch. It uses a disposable Arch Linux container on
a GitHub-hosted runner, builds the app and every test binary, and checks the
CMake install layout and desktop entry. It uploads CTest results and the test
log, including when tests fail. It does not publish packages or releases.

Six CTest suites have the `headless` label:

| Suite | Coverage |
| --- | --- |
| navigation | Unsaved-edit decisions, repeated commands, save completion, failed-save retry and cancellation |
| recording | Recorder lifecycle, readiness, pause/resume, failure handling, stop placement and shortcut setup that preserves custom bindings, with stub tools |
| displays | Display geometry, window targets and powered-off display filtering from fixtures |
| renderer | Finishes, edge room, annotations and output dimensions |
| theme | Color parsing, fixture themes and theme-change handling |
| video-marks | Timed marks, cuts and rendered video export using FFmpeg fixtures |

Qt GUI tests in this group use CTest's offscreen setting. No compositor,
session bus or GPU capture is required. The theme suite's installed Omarchy
theme collection case skips when that collection is absent; its fixture
cases still run.

A separate step runs the OCR suite's deterministic pattern and TSV parsing
cases, including positive matches, near misses, split tokens and private key
bodies. It saves a separate JUnit report. Full Tesseract recognition of rendered
text depends on the desktop's system fonts and runs in the local suite, not
hosted CI. No recognition assertions are removed from that suite.

The `pipelines` suite has the `desktop` label because it exercises native
Wayland capture and clipboard handoff. It is not run in hosted CI. Run all
eight suites locally inside omabox:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build -j 3
omabox run --net isolated -- ctest --test-dir build --output-on-failure
```

To reproduce the hosted test selection inside the isolated desktop:

```sh
omabox run --net isolated -- ctest --test-dir build -L headless --output-on-failure --no-tests=error
omabox run --net isolated -- env QT_QPA_PLATFORM=offscreen ./build/ocr-tests \
  findsEachKind ignoresNearMisses joinsTokensOcrSplit \
  hidesAWholePrivateKey findsAKeyBodyWithoutItsHeader readsTesseractTsv
```

A passing CI run establishes build and fixture-test results. It does not
establish real GPU recording, microphone quality or physical monitor behavior.
Those configurations do not require owner-run acceptance before release;
reports from users guide targeted fixes.
