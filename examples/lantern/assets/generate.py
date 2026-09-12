#!/usr/bin/env python3
"""Rebuild the original tiny pixel sprites; Python standard library only."""
from pathlib import Path
import struct
import zlib

ROOT = Path(__file__).resolve().parent
PALETTE = {
    ".": (0, 0, 0, 0),
    "s": (25, 52, 63, 255),
    "h": (136, 161, 137, 255),
    "H": (212, 219, 173, 255),
    "f": (176, 191, 155, 255),
    "c": (89, 130, 111, 255),
    "C": (122, 155, 119, 255),
    "b": (57, 89, 92, 255),
    "g": (228, 167, 72, 255),
    "G": (255, 219, 129, 255),
    "w": (255, 246, 196, 255),
}


def write_png(name, frames):
    height, frame_width = len(frames[0]), len(frames[0][0])
    assert all(len(frame) == height for frame in frames)
    assert all(len(row) == frame_width for frame in frames for row in frame)
    width = frame_width * len(frames)
    raw = b"".join(
        b"\0" + bytes(channel for frame in frames for pixel in frame[y] for channel in PALETTE[pixel])
        for y in range(height)
    )

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    (ROOT / name).write_bytes(png)


keeper = [
    "............",
    "....sHHs....",
    "...sHHHHs...",
    "...hHHHHhs..",
    "..shhhhHHhs.",
    "..sshhhhhss.",
    "...shfffs...",
    "...sffffs...",
    "....sffss...",
    "...sCCCCs...",
    "...cCCCCcc..",
    "...cCCCsscG.",
    "...sCCCs.GwG",
    "...sCCCs.gGg",
    "...scccs..g.",
    "....bsbs....",
    "....bsbs....",
    "...sb.sbs...",
]
right = []
for index in range(4):
    frame = list(keeper)
    if index == 1:
        frame[15:] = ["....bsbs....", "...bs..bs...", "..sbs..sbs.."]
    elif index == 2:
        frame[15:] = ["....bsbs....", ".....bsbs...", ".....sbss..."]
    elif index == 3:
        frame[15:] = ["....bsbs....", "...sbs.bs...", ".......ss..."]
    right.append(frame)
write_png("keeper.png", right + [[row[::-1] for row in frame] for frame in right])

wisp = [
    "........",
    "...G....",
    "...wG...",
    "..GwwG..",
    ".GwwwG..",
    "..GwwG..",
    "..gGGg..",
    "...gg...",
    "...g....",
    "........",
]
wisps = []
for index in range(4):
    frame = list(wisp)
    if index % 2:
        frame[1] = "....G..."
        frame[2] = "...Gw..."
        frame[8] = "....g..."
    if index > 1:
        frame[3] = "..Gww..."
        frame[4] = "..wwwG.."
    wisps.append(frame)
write_png("wisp.png", wisps)
print("Wrote keeper.png (96 x 18) and wisp.png (32 x 10)")
