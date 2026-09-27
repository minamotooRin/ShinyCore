"""Rebuild the small original PNG and JSON sources for this TSX example."""

import json
from pathlib import Path

from PIL import Image, ImageDraw


root = Path(__file__).resolve().parent / "assets"
root.mkdir(parents=True, exist_ok=True)
sheet = Image.new("RGBA", (80, 16))
draw = ImageDraw.Draw(sheet)
for tile, base in enumerate(("#293C52", "#32485C", "#95745F", "#38577A", "#4A7292")):
    x = tile * 16
    draw.rectangle((x, 0, x + 15, 15), fill=base)
    if tile < 2:
        draw.line((x + 2, 3, x + 11, 3), fill="#3C5670")
        draw.line((x + 6, 11, x + 14, 11), fill="#1F3348")
    elif tile == 2:
        draw.rectangle((x + 1, 1, x + 14, 14), outline="#D6AF7A")
        draw.line((x + 3, 8, x + 13, 8), fill="#694F4B")
    else:
        glow = "#9AD9DF" if tile == 3 else "#E0F7C1"
        draw.polygon([(x + 8, 2), (x + 13, 8), (x + 8, 14), (x + 3, 8)], fill="#142B44")
        draw.polygon([(x + 8, 4), (x + 11, 8), (x + 8, 12), (x + 5, 8)], fill=glow)
sheet.save(root / "ground.png", optimize=True)

flower = Image.new("RGBA", (18, 24))
d = ImageDraw.Draw(flower)
d.line((9, 19, 9, 9), fill="#91C69D", width=2)
d.polygon([(9, 8), (3, 13), (5, 5), (9, 2), (13, 5), (15, 13)], fill="#59B3AC")
d.polygon([(9, 4), (12, 9), (9, 13), (6, 9)], fill="#D9F3B0")
d.ellipse((5, 17, 13, 21), fill="#455B53")
flower.save(root / "flower.png", optimize=True)

cells = []
for y in range(8):
    for x in range(16):
        cells.append(3 if x in (0, 15) or y in (0, 7) else
                     4 if (x, y) == (8, 4) else
                     13 if (x, y) in ((4, 3), (11, 5)) else
                     1 + (x + y) % 2)
world = {"orientation": "orthogonal", "tilewidth": 16, "tileheight": 16,
         "tilesets": [{"firstgid": 1, "source": "ground.tsx"},
                      {"firstgid": 6, "source": "flower.tsx"}],
         "layers": [{"type": "tilelayer", "name": "court", "width": 16,
                     "height": 8, "data": cells, "offsetx": 24, "offsety": 20}]}
(root / "world.json").write_text(json.dumps(world, indent=2) + "\n", encoding="utf-8")
