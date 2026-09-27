"""Rebuild Crossing's short original PCM cues without external dependencies."""
import argparse
import math
from pathlib import Path
import struct
import wave

RATE=22050
# Seconds, start/end Hz, noise blend. Every cue has a short attack and decay.
CUES={
    "jump":(.14,260,720,.06),
    "land":(.11,190,75,.46),
    "switch":(.24,440,660,.04),
    "error":(.24,390,190,.18),
    "rescue":(.38,640,110,.24),
}

def samples(duration,start,end,noise):
    count=round(duration*RATE)
    phase,seed=0.0,17
    values=[]
    for i in range(count):
        progress=i/(count-1)
        frequency=start*(end/start)**progress
        phase+=math.tau*frequency/RATE
        seed=(1664525*seed+1013904223)&0xffffffff
        hiss=seed/0xffffffff*2-1
        tone=math.sin(phase)+.16*math.sin(phase*2)
        envelope=min(1,i/(RATE*.006))*(1-progress)**1.7
        values.append((tone*(1-noise)+hiss*noise)*envelope)
    gain=.68*32767/max(abs(value) for value in values)
    return struct.pack('<'+'h'*count,*(round(value*gain) for value in values))

def build(output):
    output.mkdir(parents=True,exist_ok=True)
    for name,parameters in CUES.items():
        with wave.open(str(output/(name+'.wav')),'wb') as sound:
            sound.setparams((1,2,RATE,0,'NONE','not compressed'))
            sound.writeframes(samples(*parameters))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output',type=Path)
    build(parser.parse_args().output)
