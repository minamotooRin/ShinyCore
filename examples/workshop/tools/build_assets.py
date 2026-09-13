"""Regenerate original fixtures; optional font subsetting needs fonttools and an input Noto Sans SC font."""
import argparse
import binascii
import math
from pathlib import Path
import struct
import subprocess
import wave
import zlib

ROOT = Path(__file__).resolve().parents[1]
CHARS = '星灯工坊移动跳跃下落保存读取进度房间返回已收集开始继续中文像素世界箱子平台斜坡欢迎'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--font', type=Path, help='NotoSansSC[wght].ttf; preserve its OFL.txt beside generated assets')
    parser.add_argument('--ogg', action='store_true', help='use locally installed ffmpeg to regenerate theme.ogg')
    args = parser.parse_args()
    assets = ROOT / 'assets'
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', binascii.crc32(kind + data) & 0xffffffff)
    pixels = bytearray()
    for y in range(16):
        pixels.append(0)
        for x in range(48):
            color = [(38, 68, 77), (100, 149, 131), (200, 157, 89)][x // 16]
            pixels.extend((*[min(255, int(v * (1.35 if y == 0 else 1))) for v in color], 255))
    (assets / 'tiles.png').write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 48, 16, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(pixels)) + chunk(b'IEND', b''))
    with wave.open(str(assets / 'chime.wav'), 'wb') as output:
        output.setparams((1, 2, 22050, 0, 'NONE', 'not compressed'))
        output.writeframes(struct.pack('<6615h', *[int(math.sin(i / 22050 * math.tau * 660) * (1 - i / 6615) * 7000) for i in range(6615)]))
    if args.ogg:
        subprocess.run(['ffmpeg', '-y', '-i', str(assets / 'chime.wav'), '-c:a', 'libvorbis', str(assets / 'theme.ogg')], check=True)
    if args.font:
        from fontTools.ttLib import TTFont
        from fontTools import subset
        from fontTools.varLib.instancer import instantiateVariableFont
        font = instantiateVariableFont(TTFont(args.font), {'wght': 400}, inplace=True)
        options = subset.Options(); options.name_IDs = ['*']
        selected = subset.Subsetter(options)
        selected.populate(text=''.join(chr(c) for c in range(32, 127)) + CHARS)
        selected.subset(font)
        font.save(assets / 'workshop.ttf')


if __name__ == '__main__':
    main()
