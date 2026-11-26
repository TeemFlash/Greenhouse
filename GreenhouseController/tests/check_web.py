from pathlib import Path
import subprocess
import tempfile
source = (Path(__file__).resolve().parents[1] / 'GreenhouseController/Web.h').read_text()
script = source.split('<script>', 1)[1].split('</script>', 1)[0]
with tempfile.NamedTemporaryFile('w', suffix='.js') as f:
    f.write(script)
    f.flush()
    subprocess.run(['node', '--check', f.name], check=True)
print('PASS: embedded web JavaScript syntax')
