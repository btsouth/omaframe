# Omaframe 0.2.3

Fixes shortcut setup on Omarchy stable. Earlier versions called `o.rebind`,
which stable does not provide. Setup now uses `hl.unbind` and `o.bind`, and
checks support before changing your bindings file.

Install or update with:

```sh
curl -fLo /tmp/omaframe.pkg.tar.zst https://github.com/btsouth/omaframe/releases/latest/download/omaframe-x86_64.pkg.tar.zst && sudo pacman -U /tmp/omaframe.pkg.tar.zst
```

Open Omaframe from the launcher and click **Use Print and Alt+Print**.
Your bindings file is backed up first; custom shortcuts are left alone.
If an earlier attempt failed, close and reopen Omaframe before trying again.

Validated with all five test suites, live shortcut setup using Omarchy
4.0.4's helpers without `o.rebind`, and dependencies from the stable mirror.
Real GPU recording with stable's GPU Screen Recorder 6.1.0 has not been
retested. Omaframe remains a screenshot and basic recording beta.
