"""Run only inside omabox. Measures process launch to compositor selector mapping."""
import json
import subprocess
import sys
import time

binary = sys.argv[1]
for trial in range(3):
    with open(f'/tmp/omaframe-latency-{trial}.log', 'w') as log:
        start = time.monotonic()
        proc = subprocess.Popen([binary, '--capture'], stdout=log, stderr=log)
        mapped = False
        while time.monotonic() - start < 12:
            if proc.poll() is not None:
                break
            layers = subprocess.run(['hyprctl', '-j', 'layers'], capture_output=True, text=True)
            if 'omaframe-selection' in layers.stdout:
                mapped = True
                print(json.dumps({'trial': trial+1, 'selector_ms': round((time.monotonic()-start)*1000)}), flush=True)
                break
            time.sleep(.01)
        if mapped:
            # Allow initial keyboard focus to settle before cancellation.
            time.sleep(.15)
            subprocess.run(['wtype', '-k', 'Escape'], check=True)
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.terminate()
                proc.wait(timeout=3)
                raise RuntimeError('Escape did not close capture')
        else:
            proc.terminate()
            proc.wait(timeout=3)
            raise RuntimeError('Selector never mapped; inspect /tmp/omaframe-latency log')
        time.sleep(.15)
