from pathlib import Path
import re
import time
import serial

root = Path(__file__).resolve().parents[1]
log = root / 'build/round_ui_hardware_check.log'
p = serial.Serial(port=None, baudrate=115200, timeout=0.1)
p.dtr = False
p.rts = False
p.port = '/dev/ttyUSB0'
lines = []
state = None

def collect(seconds):
    global state
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        line = p.readline().decode('utf-8', 'replace').strip()
        if not line.startswith(('BOOT ', 'PCF ', 'DIAG ', 'BUTTON ',
                                'REQUEST ', 'ALL OFF', 'SOIL ')):
            continue
        print(line, flush=True)
        lines.append(line)
        if line.startswith('DIAG SIMPLE '):
            state = {k: int(v) for k, v in
                     re.findall(r'(io|UV|WARM|FANS|selected)=(\d+)', line)}

def query():
    p.write(b'?')
    collect(0.8)
    if not state or state['io'] != 1:
        raise RuntimeError('PCF connection not confirmed')
    if any(state[k] for k in ('UV', 'WARM', 'FANS')):
        raise RuntimeError('Loads changed by external action; navigation check stopped')

with p:
    try:
        collect(8)
        query()
        if state['selected'] != 0:
            raise RuntimeError('Initial page not WARM; user may be using controls')
        for command, expected in (
                (b'2', 1), (b'2', 2), (b'2', 3), (b'2', 0),
                (b'1', 3), (b'1', 2), (b'1', 1), (b'1', 0)):
            # Navigation only. Never send the load-activation command.
            p.write(command)
            collect(0.4)
            query()
            if expected == 2:
                collect(2.0)
                query()
            if state['selected'] != expected:
                raise RuntimeError('Page differs from expected; user may be using controls')
        if not any(line.startswith('DIAG SOIL fresh=1 ') for line in lines):
            raise RuntimeError('No fresh ADS1115 measurement')
        if any(line.startswith('PCF LOST') for line in lines):
            raise RuntimeError('PCF link was lost during navigation')
        if not any('0x20 0x40 0x48 (3 found)' in line for line in lines):
            raise RuntimeError('Expected I2C addresses not confirmed')
        message = ('PASS: four pages in both directions, WARM at boot/end; '
                   'all load commands OFF; soil fresh; all three I2C addresses.')
        print(message, flush=True)
        lines.append(message)
    finally:
        log.write_text('\n'.join(lines) + '\n')
