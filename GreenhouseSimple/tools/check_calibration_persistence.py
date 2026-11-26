from pathlib import Path
import json
import re
import time
import serial

root = Path(__file__).resolve().parents[1]
dry = json.loads((root / 'build/soil_dry_reference.json').read_text())['mean_raw']
wet = json.loads((root / 'build/soil_wet_reference.json').read_text())['mean_raw']
p = serial.Serial(port=None, baudrate=115200, timeout=0.1)
p.dtr = False
p.rts = False
p.port = '/dev/ttyUSB0'
lines = []
state = None

def collect(seconds):
    global state
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        line = p.readline().decode('utf-8', 'replace').strip()
        # ESP8266 ROM output uses a different baud rate and can share a line
        # with the first application message.
        calibration_start = line.find('CALIBRATION ')
        if calibration_start >= 0:
            line = line[calibration_start:]
        if not line.startswith(('BOOT ', 'CALIBRATION ', 'DIAG ', 'PCF ', 'BUTTON ')):
            continue
        print(line, flush=True)
        lines.append(line)
        if line.startswith('DIAG SIMPLE '):
            state = {k: int(v) for k, v in re.findall(
                r'(io|UV|WARM|FANS|selected)=(\d+)', line)}

with p:
    try:
        # Explicit normal reset after the port is open, so early EEPROM-load
        # output is captured instead of depending on CP2102 auto-reset timing.
        p.rts = True
        time.sleep(0.1)
        p.reset_input_buffer()
        p.rts = False
        collect(8)
        p.write(b'?')
        collect(1)
        assert f'CALIBRATION LOADED dry={dry} wet={wet} mask=3' in lines
        assert state == {'io': 1, 'UV': 0, 'WARM': 0, 'FANS': 0, 'selected': 0}
        # Navigation only; leave the actual sensor page visible to the user.
        p.write(b'2')
        collect(0.4)
        p.write(b'2')
        collect(0.7)
        p.write(b'?')
        collect(1)
        assert state == {'io': 1, 'UV': 0, 'WARM': 0, 'FANS': 0, 'selected': 2}
        assert any('calibrated=1 ' in line for line in lines if line.startswith('DIAG SOIL '))
        assert any(line.startswith('DIAG TEMPERATURE fresh=1 ') for line in lines)
        assert not any(line.startswith('PCF LOST') for line in lines)
        result = 'PASS: measured calibration loaded from EEPROM after restart; sensor page displayed; all load commands OFF.'
        print(result, flush=True)
        lines.append(result)
    finally:
        (root / 'build/minimal_persistence_check.log').write_text('\n'.join(lines) + '\n')
