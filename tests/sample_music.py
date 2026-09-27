"""Rebuild bundled sample themes and cross each loop through the real host."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('sample_music', root/'tools/build_sample_music.py')
music = importlib.util.module_from_spec(spec)
spec.loader.exec_module(music)

with tempfile.TemporaryDirectory(prefix='shiny-sample-music-') as folder:
    temp = Path(folder)
    for name, theme in music.THEMES.items():
        rebuilt = temp/f'{name}.ogg'
        music.build(name, rebuilt)
        assert rebuilt.read_bytes() == (root/f'examples/{name}/assets/theme.ogg').read_bytes(), name
        frames = round(8*theme['bar']*60) + 1
        result = subprocess.run([str(binary), str(root/'examples'/name), '--headless',
                                 '--frames', str(frames), '--save-dir', str(temp/name)],
                                capture_output=True, text=True, encoding='utf-8', timeout=30)
        assert result.returncode == 0, result.stderr
        voices = json.loads(result.stdout)['audio']
        assert len(voices) == 1 and voices[0]['path'] == 'assets/theme.ogg'
        assert 0 < voices[0]['position'] < .05, (name, voices)

print('Three sample themes: byte-identical offline rebuilds and host loop boundaries passed')
