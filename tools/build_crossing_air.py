#!/usr/bin/env python3
"""Build Crossing's two tiny airborne frames from editable pixel rows."""

from pathlib import Path
import struct
import sys
import zlib


PALETTE = {
    "_": (0, 0, 0, 0), ".": (25, 52, 63, 255),
    "a": (212, 219, 173, 255), "b": (136, 161, 137, 255),
    "c": (176, 191, 155, 255), "d": (122, 155, 119, 255),
    "e": (89, 130, 111, 255), "f": (255, 219, 129, 255),
    "g": (255, 246, 196, 255), "h": (228, 167, 72, 255),
    "i": (57, 89, 92, 255),
}
HEAD = (
    "____________", "____.aa.____", "___.aaaa.___", "___baaaab.__",
    "__.bbbbaab._", "__..bbbbb.._", "___.bccc.___", "___.cccc.___",
)
FRAMES = (
    HEAD + (
        "____.cc.._h_", "___.dddd.eff", "__.eddd.eghf", "__.eddd..hfh",
        "___.ddd.__h_", "___.ddd.____", "___.eee.____", "___.i.i.____",
        "___.i_i.____", "__.ii_ii.___",
    ),
    HEAD + (
        "____.cc..___", "___.dddd.___", "_.eeddddee__", ".eeeddd..ef_",
        "_.eeddd._fgf", "___.ddd._hfh", "___.eee.__h_", "___.i_i.____",
        "__.i___i.___", "_.i_____i.__",
    ),
)


def chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def build() -> bytes:
    if not all(len(frame) == 18 and all(len(row) == 12 for row in frame) for frame in FRAMES):
        raise ValueError("airborne frames must be 12×18 pixels")
    pixels = b"".join(
        b"\0" + b"".join(bytes(PALETTE[key]) for frame in FRAMES for key in frame[y])
        for y in range(18)
    )
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 24, 18, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(pixels, 9)) + chunk(b"IEND", b""))


if __name__ == "__main__":
    if len(sys.argv) > 2:
        raise SystemExit("usage: build_crossing_air.py [output.png]")
    output = Path(sys.argv[1]) if len(sys.argv) == 2 else Path("examples/crossing/assets/keeper-air.png")
    output.write_bytes(build())
    print(output)
