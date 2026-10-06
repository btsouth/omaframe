#!/bin/bash
# Runs the test on a private D-Bus session bus. Where no bus can start (a
# container user without a passwd entry), the test runs without one and skips
# its D-Bus case.
if dbus-run-session -- true >/dev/null 2>&1; then
  exec dbus-run-session -- "$@"
fi
exec "$@"
