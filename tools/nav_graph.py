"""Compile native-baked chunk masks into a compact Lua connectivity graph."""

from __future__ import annotations

import argparse
from array import array
from collections import deque
import json
import math
from pathlib import Path
import tempfile


def compile_graph(source: Path) -> dict:
    data = json.loads(source.read_text(encoding='utf-8'))
    if data.get('format') != 1 or data.get('chunk_size') != 32:
        raise ValueError('expected navigation bake format 1 / 32 tiles')
    cell, tw, th = (data[name] for name in ('cell_size', 'tilewidth', 'tileheight'))
    if any(type(n) is not int or n < 1 or n > 256 for n in (cell, tw, th)) or (32*tw)%cell or (32*th)%cell:
        raise ValueError('invalid navigation cell or tile dimensions')
    width, height = 32*tw//cell, 32*th//cell
    bounds = data['bounds']
    if not isinstance(bounds, list) or len(bounds) != 4:
        raise ValueError('invalid navigation bounds')
    left, top, right, bottom = bounds
    if any(type(n) is not int for n in bounds) or left > right or top > bottom:
        raise ValueError('invalid navigation bounds')
    if max(abs(left*32*tw), abs((right+1)*32*tw), abs(top*32*th), abs((bottom+1)*32*th)) > 1000000:
        raise ValueError('navigation graph exceeds supported world coordinates')
    if type(data.get('radius')) not in (int, float) or not math.isfinite(data['radius']) or not 0 <= data['radius'] <= 4096:
        raise ValueError('invalid baked navigation radius')
    for name in ('source_sha256', 'bake_sha256'):
        value = data.get(name)
        if not isinstance(value, str) or len(value) != 64 or set(value)-set('0123456789abcdef'):
            raise ValueError(f'invalid {name}')
    count = (right-left+1)*(bottom-top+1)
    if count > 16384 or not isinstance(data.get('chunks'), list) or len(data['chunks']) != count:
        raise ValueError('navigation chunks must fill a rectangle of at most 16384 chunks')
    expected = [(x, y) for y in range(top, bottom+1) for x in range(left, right+1)]
    chunks = {}
    node_count = 0
    for (x, y), chunk in zip(expected, data['chunks']):
        if not isinstance(chunk, dict):
            raise ValueError(f'invalid navigation chunk at ({x},{y})')
        if chunk.get('x') != x or chunk.get('y') != y:
            raise ValueError(f'navigation chunk order/coordinates disagree at ({x},{y})')
        rows = chunk.get('rows')
        if not isinstance(rows, list) or len(rows) != height or any(
                not isinstance(row, str) or len(row) != width or set(row)-{'.', '#'} for row in rows):
            raise ValueError(f'invalid passability rows at ({x},{y})')
        labels = array('I', [0]) * (width*height)
        local = []
        for index in range(width*height):
            cx, cy = index%width, index//width
            if rows[cy][cx] != '.' or labels[index]:
                continue
            node_count += 1
            if node_count > 1048576:
                raise ValueError('connectivity graph exceeds 1048576 components')
            local.append(node_count)
            labels[index] = node_count
            queue = deque([index])
            while queue:
                current = queue.popleft()
                px, py = current%width, current//width
                for nx, ny in ((px, py-1), (px-1, py), (px+1, py), (px, py+1)):
                    if 0 <= nx < width and 0 <= ny < height:
                        neighbor = ny*width+nx
                        if rows[ny][nx] == '.' and not labels[neighbor]:
                            labels[neighbor] = node_count
                            queue.append(neighbor)
        chunks[(x, y)] = {'rows': rows, 'labels': labels, 'components': local}
    edges = []
    for (x, y), chunk in chunks.items():
        own = chunk['labels']
        right_chunk = chunks.get((x+1, y))
        below_chunk = chunks.get((x, y+1))
        if right_chunk:
            portals = {}
            other = right_chunk['labels']
            for row in range(height):
                a, b = own[row*width+width-1], other[row*width]
                if a and b:
                    portals.setdefault((a, b), []).append(row)
            for (a, b), rows in portals.items():
                row = rows[len(rows)//2]
                edges.append((a, b, x*width+width-1, y*height+row, (x+1)*width, y*height+row))
        if below_chunk:
            portals = {}
            other = below_chunk['labels']
            for col in range(width):
                a, b = own[(height-1)*width+col], other[col]
                if a and b:
                    portals.setdefault((a, b), []).append(col)
            for (a, b), columns in portals.items():
                col = columns[len(columns)//2]
                edges.append((a, b, x*width+col, y*height+height-1, x*width+col, (y+1)*height))
    return {'source': data, 'chunks': chunks, 'edges': edges, 'node_count': node_count,
            'cells_x': width, 'cells_y': height}


def lua_source(graph: dict) -> str:
    data = graph['source']
    cell = data['cell_size']
    lines = ['-- Generated by tools/nav_graph.py from a native navigation bake.', 'return {',
             'format=1,cell_size='+str(cell)+',chunk_width='+str(data['tilewidth']*32)+
             ',chunk_height='+str(data['tileheight']*32)+',cells_x='+str(graph['cells_x'])+
             ',cells_y='+str(graph['cells_y'])+',radius='+str(data['radius'])+',',
             'source_sha256='+json.dumps(data['source_sha256'])+
             ',bake_sha256='+json.dumps(data['bake_sha256'])+',',
             'bounds={' + ','.join(map(str, data['bounds'])) + '},node_count='+str(graph['node_count'])+',',
             'chunks={']
    width = graph['cells_x']
    for (x, y), chunk in graph['chunks'].items():
        nodes = chunk['components']
        key = json.dumps(f'{x}:{y}')
        if not nodes:
            value = '{}'
        elif len(nodes) == 1:
            if all(row == '.'*width for row in chunk['rows']):
                value = '{component='+str(nodes[0])+',full=true}'
            else:
                value = '{component='+str(nodes[0])+',rows={' + \
                    ','.join(json.dumps(row) for row in chunk['rows']) + '}}'
        else:
            labels = chunk['labels']
            rows = ['{' + ','.join(str(n) for n in labels[start:start+width]) + '}'
                    for start in range(0, len(labels), width)]
            value = '{labels={' + ','.join(rows) + '}}'
        lines.append('['+key+']='+value+',')
    lines += ['},edges={']
    for a, b, ax, ay, bx, by in graph['edges']:
        lines.append(f'{{a={a},b={b},ax={ax},ay={ay},bx={bx},by={by}}},')
    lines += ['}}', '']
    return '\n'.join(lines)


def build(source: Path, output: Path) -> dict:
    graph = compile_graph(source.resolve())
    output = output.resolve()
    if output.suffix != '.lua':
        raise ValueError('navigation graph output must be a .lua module')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=output.parent, prefix='.route-', delete=False) as file:
        staged = Path(file.name)
        file.write(lua_source(graph))
    try:
        staged.replace(output)
    finally:
        staged.unlink(missing_ok=True)
    return graph


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baked_mask', type=Path)
    parser.add_argument('output_lua', type=Path)
    args = parser.parse_args()
    try:
        graph = build(args.baked_mask, args.output_lua)
    except (OSError, ValueError, TypeError, KeyError, OverflowError) as error:
        parser.exit(1, f'error: {error}\n')
    print(f'Compiled {graph["node_count"]} components and {len(graph["edges"])} portals to {args.output_lua}')


if __name__ == '__main__':
    main()
