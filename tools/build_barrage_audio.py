"""Rebuild Barrage's original, deterministic mono PCM cues using only Python stdlib."""
import argparse
import math
from pathlib import Path
import struct
import wave

RATE = 22050
# Duration, start/end frequency, noise mix. Chords have separate note envelopes.
CUES = {
    'shot': (.075, 1500, 480, .12),
    'hit': (.07, 650, 160, .48),
    'break': (.16, 260, 65, .58),
    'dash': (.22, 180, 1050, .65),
    'hurt': (.28, 190, 58, .24),
    'heal': (.34, 660, 990, 0),
    'wave': (.55, 440, 880, 0),
    'win': (.9, 523.25, 1046.5, 0),
    'lose': (.7, 330, 110, 0),
}


def samples(name, spec):
    duration, start, end, noise = spec
    count = round(duration * RATE)
    seed, phase = 17, 0.0
    values = []
    notes = {'heal': (1, 1.25, 1.5), 'wave': (1, 1.25, 1.5, 2),
             'win': (1, 1.25, 1.5, 2), 'lose': (1, .84, .67, .5)}.get(name)
    for i in range(count):
        t = i / RATE
        progress = i / (count - 1)
        if notes:
            note = min(len(notes) - 1, int(progress * len(notes)))
            local = progress * len(notes) - note
            frequency = start * notes[note]
            envelope = min(1, local / .06) * (1 - local) ** .65
        else:
            frequency = start * (end / start) ** progress
            envelope = min(1, t / .004) * (1 - progress) ** 1.8
        phase += math.tau * frequency / RATE
        seed = (1664525 * seed + 1013904223) & 0xffffffff
        hiss = seed / 0xffffffff * 2 - 1
        tone = math.sin(phase) + .18 * math.sin(phase * 2)
        values.append((tone * (1 - noise) + hiss * noise) * envelope)
    gain = .72 * 32767 / max(abs(value) for value in values)
    return struct.pack('<' + 'h' * count, *(round(value * gain) for value in values))


def build(output):
    output.mkdir(parents=True, exist_ok=True)
    for name, spec in CUES.items():
        with wave.open(str(output / (name + '.wav')), 'wb') as stream:
            stream.setparams((1, 2, RATE, 0, 'NONE', 'not compressed'))
            stream.writeframes(samples(name, spec))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    build(parser.parse_args().output)
