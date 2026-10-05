#!/bin/bash
set -euo pipefail
meter_runtime=$(mktemp -d)
trap 'kill "$meter_server" 2>/dev/null || true; wait "$meter_server" 2>/dev/null || true; rm -rf "$meter_runtime"' EXIT
export XDG_RUNTIME_DIR="$meter_runtime"
export PULSE_SERVER="unix:$meter_runtime/native"
printf 'load-module module-native-protocol-unix socket=%s/native auth-anonymous=1\n' "$meter_runtime" > "$meter_runtime/server.pa"
pulseaudio -n --daemonize=no --exit-idle-time=-1 --use-pid-file=no --disable-shm --log-target=file:"$meter_runtime/server.log" -F "$meter_runtime/server.pa" &
meter_server=$!
for meter_attempt in {1..50}; do
  if pactl info >/dev/null 2>&1; then break; fi
  sleep 0.1
done
if ! pactl info >/dev/null 2>&1; then cat "$meter_runtime/server.log"; exit 1; fi
"$1"
