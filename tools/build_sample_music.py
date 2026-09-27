"""Build the three original sample themes offline with Python and ffmpeg."""
from array import array
import math
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import wave

RATE = 22050
THEMES = {
    'wayfarer': {
        'bar': 2.5, 'lead': 'bell', 'pad_gain': .055, 'bass_gain': .095,
        'lead_gain': .075, 'bass_length': .95, 'lead_length': 1.25,
        'bass_beats': (0, 2),
        'chords': [(50, 53, 57), (46, 50, 53), (53, 57, 60), (48, 52, 55),
                   (43, 46, 50), (46, 50, 53), (50, 53, 57), (45, 49, 52)],
        'melody': [(74, 77, 81, 79), (77, 74, 72, 69), (72, 76, 79, 81),
                   (79, 76, 74, 72), (74, 77, 79, 81), (77, 74, 72, 69),
                   (74, 77, 81, 84), (81, 76, 73, 69)],
    },
    'crossing': {
        'bar': 2, 'lead': 'lead', 'pad_gain': .06, 'bass_gain': .11,
        'lead_gain': .09, 'bass_length': .8, 'lead_length': .9,
        'bass_beats': (0, 2),
        'chords': [(52, 55, 59), (48, 52, 55), (55, 59, 62), (50, 54, 57),
                   (52, 55, 59), (48, 52, 55), (50, 54, 57), (47, 51, 54)],
        'melody': [(76, 79, 83, 79), (76, 79, 84, 83), (74, 79, 83, 86),
                   (78, 81, 86, 81), (76, 79, 83, 88), (79, 76, 72, 76),
                   (78, 81, 86, 84), (71, 75, 78, 83)],
    },
    'barrage': {
        'bar': 1.875, 'lead': 'pulse', 'pad_gain': .035, 'bass_gain': .13,
        'lead_gain': .09, 'bass_length': .55, 'lead_length': .38,
        'bass_beats': (0, 1, 2, 3),
        'chords': [(50, 53, 57), (48, 52, 55), (46, 50, 53), (45, 49, 52),
                   (43, 46, 50), (46, 50, 53), (45, 49, 52), (50, 53, 57)],
        'melody': [(74, 81, 77, 81, 86, 81, 77, 81),
                   (72, 79, 76, 79, 84, 79, 76, 79),
                   (70, 77, 74, 77, 82, 77, 74, 77),
                   (69, 76, 73, 76, 81, 76, 73, 76),
                   (67, 74, 70, 74, 79, 74, 70, 74),
                   (70, 77, 74, 77, 82, 77, 74, 77),
                   (69, 76, 73, 76, 81, 76, 73, 76),
                   (74, 81, 77, 81, 86, 81, 77, 81)],
    },
}


def build(name, path):
    if not shutil.which('ffmpeg'):
        raise RuntimeError('ffmpeg is required to rebuild theme.ogg')
    theme = THEMES[name]
    bar_length = theme['bar']
    count = int(RATE * 8 * bar_length)
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
                decay = {'bell': -3.5, 'bass': -2.4, 'lead': -3.2, 'pulse': -4.5}[shape]
                harmonic = {'bell': .3, 'bass': .08, 'lead': .18, 'pulse': .35}[shape]
                envelope = min(1, t / .012) * math.exp(decay * t)
                if shape in ('lead', 'pulse'):
                    envelope *= (1 - t / duration) ** 2
                    value = math.sin(phase) + harmonic * math.sin(3 * phase)
                else:
                    value = math.sin(phase) + harmonic * math.sin(2 * phase)
                value *= envelope
            index = (int(start * RATE) + sample) % count
            left[index] += gain * value * (1 - pan)
            right[index] += gain * value * (1 + pan)

    for bar, (chord, notes) in enumerate(zip(theme['chords'], theme['melody'])):
        start = bar * bar_length
        for i, note in enumerate(chord):
            tone(start, bar_length, note, theme['pad_gain'], 'pad', (-.2, 0, .2)[i])
        for beat in theme['bass_beats']:
            tone(start + beat * bar_length / 4, theme['bass_length'],
                 chord[0] - 12, theme['bass_gain'], 'bass', -.08)
        for beat, note in enumerate(notes):
            tone(start + beat * bar_length / len(notes), theme['lead_length'], note,
                 theme['lead_gain'] * (1 if beat % 2 == 0 else .76), theme['lead'],
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
    with tempfile.TemporaryDirectory(prefix='shiny-sample-music-', dir=path.parent) as folder:
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
    if len(sys.argv) not in (2, 3) or sys.argv[1] not in THEMES:
        raise SystemExit('usage: build_sample_music.py {wayfarer|crossing|barrage} [output.ogg]')
    name = sys.argv[1]
    output = Path(sys.argv[2]) if len(sys.argv) == 3 else Path(f'examples/{name}/assets/theme.ogg')
    print(f'{output}: {8 * THEMES[name]["bar"]:g}-second loop, source peak {build(name, output):.3f}')
