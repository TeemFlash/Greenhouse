"""Read paused-device diagnostics without printing credentials or driving loads."""
import argparse
import time
import serial

parser = argparse.ArgumentParser()
parser.add_argument('--port', default='/dev/ttyUSB0')
args = parser.parse_args()
port = serial.Serial(port=None, baudrate=115200, timeout=0.3)
port.dtr = False
port.rts = False
port.port = args.port
with port:
    port.reset_input_buffer()
    deadline = time.monotonic() + 15
    next_request = 0
    seen = set()
    while time.monotonic() < deadline:
        if time.monotonic() >= next_request:
            port.write(b'?')
            next_request = time.monotonic() + 2
        line = port.readline(512).decode('utf-8', 'replace').strip()
        if line.startswith(('DIAG ', 'DIAG:', 'PCF ', 'BOOT /', 'PAUSED')) and line not in seen:
            print(line, flush=True)
            seen.add(line)
        if line.startswith('DIAG bus') and 'found)' in line:
            break
    if not any(line.startswith('DIAG') for line in seen):
        print('No diagnostic reply received; no hardware status confirmed.')
