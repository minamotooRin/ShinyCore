"""Build five crisp, original four-frame Barrage enemy sprites."""

from pathlib import Path
import sys

from PIL import Image, ImageDraw


def scout(d, phase):
    lift = (0, -1, 0, 1)[phase]
    d.polygon([(3, 3), (0, 1 + lift), (1, 4), (0, 6), (3, 5)], fill="#9B496C")
    d.polygon([(4, 3), (7, 1 + lift), (6, 4), (7, 6), (4, 5)], fill="#9B496C")
    d.rectangle((2, 2, 5, 5), fill="#F290AB")
    d.rectangle((3, 1, 4, 2), fill="#FFD3CB")
    d.point((3, 4), fill="#452C46")
    d.point((4, 4), fill="#452C46")


def runner(d, phase):
    tail = (0, 1, 0, -1)[phase]
    d.polygon([(3, 0), (6, 4), (4, 5), (3, 4), (2, 5), (0, 4)], fill="#9D5B35")
    d.polygon([(3, 0), (5, 3), (3, 4), (1, 3)], fill="#FFCB77")
    d.point((3, 2), fill="#FFF2BC")
    d.line([(2, 4), (1, 6 + min(0, tail))], fill="#F59054")
    d.line([(4, 4), (5, 6 + min(0, -tail))], fill="#F59054")


def gunner(d, phase):
    d.polygon([(4, 0), (6, 0), (7, 2), (9, 3), (9, 6), (7, 7),
               (6, 9), (3, 9), (2, 7), (0, 6), (0, 3), (2, 2)], fill="#5E477E")
    d.rectangle((3, 2, 6, 7), fill="#A27BD7")
    d.rectangle((2, 4, 7, 5), fill="#D0AEF4")
    d.rectangle((4, 3, 5, 6), fill="#342D5A")
    if phase in (1, 3):
        d.point((0, 4), fill="#E6D2FC")
        d.point((9, 5), fill="#E6D2FC")
    else:
        d.point((4, 4), fill="#F4E7FF")
        d.point((5, 4), fill="#F4E7FF")


def brute(d, phase):
    reach = (0, 1, 0, -1)[phase]
    d.polygon([(1, 2), (4, 4), (5, 3), (9, 3), (10, 4), (13, 2),
               (13, 8), (11, 8), (10, 13), (4, 13), (3, 8), (1, 8)], fill="#754750")
    d.polygon([(2, 3), (5, 5), (9, 5), (12, 3), (11, 9), (9, 12),
               (5, 12), (3, 9)], fill="#D17D5B")
    d.rectangle((5, 5, 9, 8), fill="#F5B77E")
    d.rectangle((6, 6, 8, 7), fill="#553747")
    d.rectangle((0, 8 + reach, 2, 11 + reach), fill="#EC986D")
    d.rectangle((12, 8 - reach, 14, 11 - reach), fill="#EC986D")
    d.point((6, 10), fill="#FFE3A4")
    d.point((8, 10), fill="#FFE3A4")


def guardian(d, phase):
    glow = ("#E89564", "#FFC98C", "#FFF0B8", "#FFC98C")[phase]
    # A broad crowned silhouette stays distinct from the small flying enemies.
    d.polygon([(11, 0), (14, 5), (19, 2), (18, 8), (23, 10), (19, 14),
               (22, 20), (16, 19), (12, 23), (8, 19), (2, 20), (5, 14),
               (0, 10), (5, 8), (4, 2), (9, 5)], fill="#805268")
    d.polygon([(11, 2), (14, 7), (17, 5), (16, 9), (21, 11), (17, 14),
               (19, 18), (15, 17), (12, 21), (9, 17), (5, 18), (7, 14),
               (2, 11), (7, 9), (6, 5), (9, 7)], fill="#D4926D")
    d.polygon([(12, 5), (17, 10), (15, 17), (12, 19), (8, 16), (7, 10)], fill=glow)
    d.polygon([(11, 8), (15, 10), (14, 15), (11, 17), (9, 14), (9, 10)], fill="#403549")
    d.rectangle((10, 10, 13, 11), fill="#FFE5AE" if phase == 2 else "#FF91A0")
    d.point((11, 14), fill="#FFCB77")
    d.point((13, 14), fill="#FFCB77")


def build(output):
    output.mkdir(parents=True, exist_ok=True)
    for name, size, draw in (("scout", 8, scout), ("runner", 7, runner),
                             ("gunner", 10, gunner), ("brute", 15, brute),
                             ("guardian", 24, guardian)):
        sheet = Image.new("RGBA", (size * 4, size))
        for frame in range(4):
            cell = Image.new("RGBA", (size, size))
            draw(ImageDraw.Draw(cell), frame)
            sheet.paste(cell, (frame * size, 0))
        sheet.save(output / f"{name}-v1.png", optimize=True)


if __name__ == "__main__":
    build(Path(sys.argv[1] if len(sys.argv) > 1 else "examples/barrage/assets"))
