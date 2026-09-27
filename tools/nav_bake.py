"""Bake streamed-map walkability through the native engine's terrain and navigation code."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import shutil
import subprocess
import tempfile

VERSION = 1


def source_digest(folder: Path, index: dict) -> str:
    digest = hashlib.sha256()
    for name in ['index.json', *(item['path'] for item in index['chunks'])]:
        path = (folder / name).resolve()
        if Path(name).name != name or '\\' in name or not path.is_file() or not path.is_relative_to(folder.resolve()):
            raise ValueError(f'invalid streamed chunk path: {name}')
        digest.update(name.encode('utf-8') + b'\0')
        digest.update(path.read_bytes())
    return digest.hexdigest()


def terrain_halo(index: dict, cw: int, ch: int) -> tuple[int, int]:
    reach_x = reach_y = 0.0
    for layer in index['layers']:
        if layer['type'] != 'tilelayer':
            continue
        for tileset in index['tilesets']:
            offset = tileset.get('tileoffset') or {}
            for tile in tileset.get('tiles', []):
                collision = next((item['value'] for item in tile.get('properties', [])
                                  if item['name'] == 'collision'), 'empty')
                if collision == 'empty' and not tile.get('collision_shapes'):
                    continue
                iw = tile.get('imagewidth', tileset['tilewidth'])
                ih = tile.get('imageheight', tileset['tileheight'])
                vx, vy = [0, iw], [0, ih]
                for shape in tile.get('collision_shapes', []):
                    vx.extend(shape[::2])
                    vy.extend(shape[1::2])
                # Include the flipped forms and the Tiled tile-bottom anchor.
                reach_x = max(reach_x, abs(layer.get('offsetx', 0)) + abs(offset.get('x', 0)) +
                              max(abs(v) for vertex in vx for v in (vertex, iw-vertex)))
                reach_y = max(reach_y, abs(layer.get('offsety', 0)) + abs(offset.get('y', 0)) +
                              index['tileheight'] + max(abs(v) for vertex in vy for v in (vertex, ih-vertex)))
    halo_x, halo_y = max(1, math.ceil(reach_x / cw)), max(1, math.ceil(reach_y / ch))
    if (2*halo_x+1) * (2*halo_y+1) > 256:
        raise ValueError('tile collision extent needs more than 256 source chunks per bake window')
    return halo_x, halo_y


def bounds(index: dict, requested: tuple[int, int, int, int] | None,
           halo: tuple[int, int]) -> list[tuple[int, int]]:
    listed = index['chunks']
    if requested is None:
        if not listed:
            raise ValueError('empty streamed map requires --bounds')
        left = min(item['x'] for item in listed)
        top = min(item['y'] for item in listed)
        right = max(item['x'] for item in listed)
        bottom = max(item['y'] for item in listed)
    else:
        left, top, right, bottom = requested
    if left > right or top > bottom or left-halo[0] < -31250 or top-halo[1] < -31250 \
            or right+halo[0] > 31250 or bottom+halo[1] > 31250:
        raise ValueError('bounds must be ordered and leave the required terrain halo')
    if (right-left+1) * (bottom-top+1) > 16384:
        raise ValueError('navigation bake exceeds 16384 chunks; use --bounds for a smaller region')
    return [(x, y) for y in range(top, bottom+1) for x in range(left, right+1)]


def room_script(coords: list[tuple[int, int]], width: int, height: int, cell: int,
                radius: float, halo: tuple[int, int]) -> str:
    positions = ','.join(f'{{{x},{y}}}' for x, y in coords)
    return f'''local Tiles=require('tiles')
local centers={{{positions}}}
local cw,ch={width},{height}
local cell,radius={cell},{radius!r}
local halo_x,halo_y={halo[0]},{halo[1]}
local view,coverage,pins
local function request(index,frame)
    local center=centers[index]
    local seen,near={{}},{{}}
    pins={{}}
    local function add(x,y)
        x,y=math.tointeger(x),math.tointeger(y)
        local name=x..':'..y
        if not seen[name] then
            seen[name]=true
            sc.stream.request(x,y,frame)
            pins[#pins+1]={{x,y}}
        end
    end
    for dy=-1,1 do for dx=-1,1 do
        local x,y=center[1]+dx,center[2]+dy
        near[#near+1]={{x,y}}
        add(x,y)
    end end
    for dy=-halo_y,halo_y do for dx=-halo_x,halo_x do
        add(center[1]+dx,center[2]+dy)
    end end
    for _,pos in ipairs(near) do
        for _,anchor in ipairs(coverage[pos[1]..':'..pos[2]] or {{}}) do add(anchor.x,anchor.y) end
    end
end
return {{init=function()
    sc.stream.open('world/index.json')
    local metadata=sc.stream.metadata()
    view=Tiles.new(metadata)
    coverage={{}}
    for _,item in ipairs(metadata.object_coverage or {{}}) do
        coverage[math.tointeger(item.x)..':'..math.tointeger(item.y)]=item.anchors
    end
    request(1,0)
end,update=function()
    local chunks={{}}
    local near={{}}
    for i,pos in ipairs(pins) do
        chunks[i]=assert(sc.stream.get(pos[1],pos[2]),'navigation bake chunk not ready')
        if i<=9 then near[i]=chunks[i] end
    end
    local prepared=Tiles.prepare(view,chunks)
    sc.stream.terrain(Tiles.terrain(view,prepared,chunks),Tiles.navigation(view,near,cell))
    local mask=sc.navigation.mask(radius)
    local bx,by=cw//cell,ch//cell
    for row=1,by do
        sc.log('NAVBAKE:'..(sc.tick()+1)..':'..row..':'..mask.rows[by+row]:sub(bx+1,bx*2))
    end
    for _,pos in ipairs(pins) do sc.stream.release(pos[1],pos[2]) end
    if centers[sc.tick()+2] then request(sc.tick()+2,sc.tick()+1) end
end}}
'''


def bake(engine: Path, source: Path, output: Path, selected: tuple[int, int, int, int] | None,
         cell_size: int | None, radius: float) -> dict:
    engine = engine.resolve()
    source = source.resolve()
    if not engine.is_file() or not (source / 'index.json').is_file():
        raise ValueError('engine executable and built map index are required')
    api = json.loads(subprocess.check_output([str(engine), '--api'], text=True, encoding='utf-8'))
    if not api['modules'].get('streaming') or not api['modules'].get('navigation'):
        raise ValueError('engine build requires streaming and navigation modules')
    index = json.loads((source / 'index.json').read_text(encoding='utf-8'))
    if index.get('format') != 3 or index.get('chunk_size') != 32:
        raise ValueError('expected streamed map format 3 / 32 tiles')
    tw, th = index['tilewidth'], index['tileheight']
    if type(tw) is not int or type(th) is not int or not 1 <= tw <= 256 or not 1 <= th <= 256:
        raise ValueError('invalid streamed tile dimensions')
    cell = cell_size or math.gcd(tw, th)
    cw, ch = tw * 32, th * 32
    if type(cell) is not int or not 1 <= cell <= 256 or cw % cell or ch % cell:
        raise ValueError('cell size must be 1..256 and divide both chunk dimensions')
    if 9 * (cw // cell) * (ch // cell) > 16384:
        raise ValueError('three-by-three terrain halo exceeds the 16384-cell navigation limit; increase --cell-size')
    if not math.isfinite(radius) or not 0 <= radius <= min(cw, ch, 4096):
        raise ValueError('radius must fit inside the one-chunk terrain halo')
    halo = terrain_halo(index, cw, ch)
    coords = bounds(index, selected, halo)
    fingerprint = source_digest(source, index)
    identity = hashlib.sha256()
    identity.update(f'{VERSION}:{fingerprint}:{cell}:{radius}:{coords[0]}:{coords[-1]}'.encode('ascii'))
    identity.update(engine.read_bytes())
    identity.update((Path(__file__).resolve().parents[1] / 'lua/shiny/stream_tiles.lua').read_bytes())
    rows = [[] for _ in coords]
    by = ch // cell
    bx = cw // cell
    with tempfile.TemporaryDirectory(prefix='shiny-nav-bake-') as temp:
        project = Path(temp)
        (project / 'world').mkdir()
        for name in ['index.json', *(item['path'] for item in index['chunks'])]:
            shutil.copyfile(source / name, project / 'world' / name)
        shutil.copyfile(Path(__file__).resolve().parents[1] / 'lua/shiny/stream_tiles.lua', project / 'tiles.lua')
        (project / 'project.lua').write_text("return {id='shiny-nav-bake',modules={'streaming','navigation'}}\n", encoding='utf-8')
        for start in range(0, len(coords), 256):
            batch = coords[start:start+256]
            (project / 'main.lua').write_text(room_script(batch, cw, ch, cell, radius, halo), encoding='utf-8')
            with tempfile.TemporaryFile(mode='w+', encoding='utf-8') as diagnostics:
                result = subprocess.run([str(engine), '--headless', str(project), '--frames', str(len(batch))],
                                        stdout=subprocess.PIPE, stderr=diagnostics, text=True, encoding='utf-8')
                diagnostics.seek(0)
                lines = diagnostics.readlines()
            if result.returncode:
                raise ValueError(f'native navigation bake failed near chunk {batch[0]}: {"".join(lines[-8:]).strip()}')
            for line in lines:
                if not line.startswith('[lua] NAVBAKE:'):
                    continue
                _, number, row, data = line.rstrip('\r\n').split(':', 3)
                chunk_index, row_index = start + int(number)-1, int(row)-1
                if not start <= chunk_index < start+len(batch) or row_index != len(rows[chunk_index]):
                    raise ValueError('native navigation bake returned unordered or duplicate rows')
                if len(data) != bx or set(data) - {'.', '#'}:
                    raise ValueError('native navigation bake returned invalid passability')
                rows[chunk_index].append(data)
    if any(len(chunk) != by for chunk in rows):
        raise ValueError('native navigation bake omitted chunk rows')
    data = {'format': 1, 'tool_version': VERSION, 'source_sha256': fingerprint,
            'bake_sha256': identity.hexdigest(), 'chunk_size': 32,
            'tilewidth': tw, 'tileheight': th, 'cell_size': cell, 'radius': radius,
            'terrain_halo': list(halo), 'bounds': [coords[0][0], coords[0][1], coords[-1][0], coords[-1][1]],
            'chunks': [{'x': x, 'y': y, 'rows': chunk} for (x, y), chunk in zip(coords, rows)]}
    output = output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=output.parent, prefix='.nav-', delete=False) as file:
        staged = Path(file.name)
        json.dump(data, file, ensure_ascii=False, sort_keys=True, separators=(',', ':'))
        file.write('\n')
    try:
        staged.replace(output)
    finally:
        staged.unlink(missing_ok=True)
    return data


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine', type=Path)
    parser.add_argument('built_map', type=Path, help='Directory containing a format-3 index.json')
    parser.add_argument('output', type=Path)
    parser.add_argument('--bounds', type=int, nargs=4, metavar=('LEFT', 'TOP', 'RIGHT', 'BOTTOM'))
    parser.add_argument('--cell-size', type=int)
    parser.add_argument('--radius', type=float, default=0)
    args = parser.parse_args()
    try:
        result = bake(args.engine, args.built_map, args.output, args.bounds, args.cell_size, args.radius)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f'error: {error}\n')
    print(f'Baked {len(result["chunks"])} chunks to {args.output}')


if __name__ == '__main__':
    main()
