# Direct capture validation

How to build and check **Skip the finish chooser**, `--direct`, `--chooser`
and the Ctrl+Print chooser shortcut on an Omarchy machine. The automated
suites already pass on a headless Ubuntu build (19 of 19). What is left needs
a real Hyprland session.

## 1. Build

Install the build dependencies listed in the README under "Build from
source". Get the `direct-screenshot-capture` branch onto the machine (push it
and fetch, or copy the repository), then:

```sh
git switch direct-screenshot-capture
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j 3
```

## 2. Automated tests

The headless suites need no desktop:

```sh
ctest --test-dir build -L headless --output-on-failure
```

Expect 19 of 19 to pass. The desktop suites (capture, clipboard) talk to a
compositor, so run them in a throwaway nested desktop, not your own session:

```sh
omabox run --net isolated -- ctest --test-dir build --output-on-failure
```

## 3. Before the manual checks

- **Quit any running Omaframe** (`pkill -x omaframe`). The build and the
  installed package share one instance per Wayland session. A command from the
  build would otherwise reach the installed version, which answers
  `--direct` and `--chooser` with "invalid".
- **Run the build directly**, as `./build/omaframe …`. Print still runs the
  installed package.
- **Settings are shared** with the installed Omaframe. Turn **Skip the finish
  chooser** off again when you are done.

## 4. Manual checks

Each check names the task it closes in the change's task list. Note anything
that differs from "Expect".

### Setting and Settings window (3.1)

1. `./build/omaframe --studio`, open Settings.
2. **Skip the finish chooser** sits under **Save screenshots automatically**,
   with a note that the secrets prompt is skipped.
3. Turn it on, quit (`pkill -x omaframe`), open Settings again.

Expect: still on.

### Direct capture (5.2)

With the setting on:

1. `./build/omaframe --capture`, click a window.

   Expect: no chooser. **Screenshot copied** notification appears, and the
   clipboard holds the screenshot in Raw, exactly the selection. The chooser
   still opens on the finish you last picked there.
2. Same, but drag an area, then once more pressing F.

   Expect: the same.
3. With **Save screenshots automatically** on, expect a new PNG in the
   screenshot folder. With it off, expect no new file.
4. Click the notification.

   Expect: the editor opens with the screenshot, its finish selectable, and Pin
   available.
5. `./build/omaframe --capture`, press Esc while selecting.

   Expect: nothing copied or saved.
6. `./build/omaframe --screen` and `./build/omaframe --repeat`.

   Expect: both finish without the chooser.

### Overrides (3.2, 5.1, 5.2)

1. Setting on: `./build/omaframe --capture --chooser`, click a window.

   Expect: the chooser appears. Read its title, hint and footer. Report any
   wording that is wrong while the setting is on (3.2).
2. Setting off: `./build/omaframe --capture --direct`, click a window.

   Expect: finishes without the chooser.
3. Right after that, with the setting still off: `./build/omaframe --capture`.

   Expect: the chooser appears. The override applied to one capture only.
4. `./build/omaframe --capture --delay 3 --direct`.

   Expect: countdown, then a direct finish.
5. A running instance honors the flags: start `./build/omaframe --studio`,
   then from a terminal run `./build/omaframe --capture --direct`.

   Expect: finishes without the chooser.
6. These print an error and exit with status 1 (`echo $?`):

   ```sh
   ./build/omaframe --capture --direct --chooser
   ./build/omaframe --record --direct
   ```

### Failure recovery (5.2)

Setting on, nothing running.

- **Copy fails.** Turn **Save screenshots automatically** off, then:

  ```sh
  mkdir -p /tmp/nocopy
  printf '#!/bin/sh\nexit 1\n' > /tmp/nocopy/wl-copy
  chmod +x /tmp/nocopy/wl-copy
  PATH=/tmp/nocopy:$PATH ./build/omaframe --capture
  ```

  Click a window.

  Expect: the chooser opens, showing the copy failure.
- **Save fails.** Turn automatic saving on, then:

  ```sh
  chmod a-w ~/Pictures/Omaframe
  ./build/omaframe --capture
  ```

  Click a window. Use your screenshot folder if you changed it.

  Expect: the chooser opens, showing the failure, and the capture is still
  there. Run `chmod u+w ~/Pictures/Omaframe`, then pick a finish.

  Expect: saves and copies.

### Scroll capture (5.2)

Setting on: `./build/omaframe --scroll`, click a scrollable window.

Expect: the chooser appears with the stitched image.

### Chooser shortcut

This step edits `~/.config/hypr/bindings.lua`. Omaframe backs it up first, next
to the original as `bindings.lua.bak.omaframe-…`.

1. In Settings, under shortcuts, the row **Screenshot with the finish chooser
   (optional)** shows Ctrl+Print. If the key is free, **Use Ctrl+Print for the
   chooser** appears.
2. Press it.

   Expect:
   - The row reads Ready.
   - Print, Alt+Print and Shift+Print are unchanged in `bindings.lua`.
   - Ctrl+Print with the setting on ends in the chooser.
3. If Ctrl+Print is already bound to something else, expect no button and a
   note that Omaframe left it alone.

The new line runs this build. Remove it from Omaframe's block in
`bindings.lua` afterwards, or restore the backup.

## 5. Report back

For each section, say whether it passed. For anything that did not, give what
you did and what happened. Include the `ctest` summary lines.

## Results (2026-10-11)

All checks above passed on Omarchy, including the ones without an inline note:
the two rejected commands exit with status 1, both failures open the chooser
with a working retry, and `ctest` passes. Follow-ups from the notes:

- A direct capture now uses the Raw finish instead of the last-picked one. The
  chooser keeps its remembered finish. This came after the hands-on run and is
  covered by the `quick-copy` suite (19 of 19 headless suites pass). The
  Direct capture steps above describe the Raw behavior.
- The setting is stored in `~/.config/Omaframe/Omaframe.conf` as
  `screenshot/skipChooser`, with Omaframe's other settings. `bindings.lua`
  holds only key bindings and is never changed by the setting.
- Scroll capture's Raw preview not showing the whole stitched image also
  happens on `main`, so it is not part of this change.
