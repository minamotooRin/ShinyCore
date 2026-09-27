"""Build Wayfarer's original 20-second forest theme (offline tool; ffmpeg required)."""
from array import array
import math
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import wave

RATE = 22050
BAR = 2.5
LENGTH = 8 * BAR
CHORDS = [(50, 53, 57), (46, 50, 53), (53, 57, 60), (48, 52, 55),
          (43, 46, 50), (46, 50, 53), (50, 53, 57), (45, 49, 52)]
MELODY = [(74, 77, 81, 79), (77, 74, 72, 69), (72, 76, 79, 81),
          (79, 76, 74, 72), (74, 77, 79, 81), (77, 74, 72, 69),
          (74, 77, 81, 84), (81, 76, 73, 69)]


def build(path):
    if not shutil.which('ffmpeg'):
        raise RuntimeError('ffmpeg is required to rebuild theme.ogg')
    count = int(RATE * LENGTH)
    left = array('f', [0]) * count
    right = array('f', [0]) * count

    def tone(start, duration, midi, gain, shape, pan=0):
        frequency = 440 * 2 ** ((midi - 69) / 12)
        end = int(duration * RATE)
        for sample in range(end):
            t = sample / RATE
            phase = 2 * math.pi * frequency * t
            if shape == 'pad':
                envelope = math.sin(math.pi * t / duration) ** 2
                value = (math.sin(phase) + .12 * math.sin(2 * phase)) * envelope
            else:
                decay = -3.5 if shape == 'bell' else -2.4
                harmonic = .3 if shape == 'bell' else .08
                envelope = min(1, t / .012) * math.exp(decay * t)
                value = (math.sin(phase) + harmonic * math.sin(2 * phase)) * envelope
            index = (int(start * RATE) + sample) % count
            left[index] += gain * value * (1 - pan)
            right[index] += gain * value * (1 + pan)

    for bar, (chord, notes) in enumerate(zip(CHORDS, MELODY)):
        start = bar * BAR
        for i, note in enumerate(chord):
            tone(start, BAR, note, .055, 'pad', (-.2, 0, .2)[i])
        for beat in (0, 2):
            tone(start + beat * BAR / 4, .95, chord[0] - 12, .095, 'bass', -.08)
        for beat, note in enumerate(notes):
            tone(start + beat * BAR / 4, 1.25, note,
                 .075 if beat % 2 == 0 else .057, 'bell',
                 -.14 if beat % 2 == 0 else .14)

    peak = max(max(abs(value) for value in left),
               max(abs(value) for value in right))
    scale = .55 / peak
    pcm = array('h')
    for l, r in zip(left, right):
        pcm.append(round(32767 * l * scale))
        pcm.append(round(32767 * r * scale))
    if sys.byteorder != 'little':
        pcm.byteswap()
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='shiny-wayfarer-music-', dir=path.parent) as folder:
        source = Path(folder) / 'theme.wav'
        encoded = Path(folder) / 'theme.ogg'
        with wave.open(str(source), 'wb') as sound:
            sound.setnchannels(2)
            sound.setsampwidth(2)
            sound.setframerate(RATE)
            sound.writeframes(pcm.tobytes())
        subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y',
                        '-i', str(source), '-map_metadata', '-1', '-fflags', '+bitexact',
                        '-c:a', 'libvorbis', '-q:a', '4', str(encoded)], check=True)
        encoded.replace(path)
    return peak


if __name__ == '__main__':
    if len(sys.argv) > 2:
        raise SystemExit('usage: build_wayfarer_music.py [output.ogg]')
    output = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('examples/wayfarer/assets/theme.ogg')
    print(f'{output}: 20-second loop, source peak {build(output):.3f}')
