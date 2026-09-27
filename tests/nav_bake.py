"""Focused native bake checks: deterministic project data and collision across a distant chunk."""

import importlib.util
import json
from pathlib import Path
import sys
import tempfile


root = Path(__file__).resolve().parents[1]
binary = Path(sys.argv[1]).resolve()
spec = importlib.util.spec_from_file_location('nav_bake', root / 'tools/nav_bake.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

with tempfile.TemporaryDirectory(prefix='shiny-nav-bake-check-') as temp:
    folder = Path(temp)
    source = root / 'examples/wayfarer/maps/world'
    first, second = folder / 'first.json', folder / 'second.json'
    result = module.bake(binary, source, first, None, None, 0)
    module.bake(binary, source, second, None, None, 0)
    assert first.read_bytes() == second.read_bytes()
    assert len(result['chunks']) == 16 and result['terrain_halo'] == [1, 1]
    assert result['chunks'][0]['rows'][0] == '#' * 32

    distant = folder / 'distant'
    distant.mkdir()
    chunk = {'x': 2, 'y': 0, 'layers': {'0': [1] + [0] * 1023}, 'objects': []}
    content = json.dumps(chunk, separators=(',', ':'))
    (distant / '2_0.json').write_text(content, encoding='utf-8')
    index = {'format': 3, 'chunk_size': 32, 'tilewidth': 8, 'tileheight': 8,
             'layers': [{'type': 'tilelayer'}], 'chunks': [
                 {'x': 2, 'y': 0, 'path': '2_0.json', 'bytes': len(content.encode())}],
             'tilesets': [{'firstgid': 1, 'tilecount': 1, 'columns': 1,
                           'tilewidth': 8, 'tileheight': 8, 'image': 'tile.png',
                           'tileoffset': {'x': -257, 'y': 0}, 'tiles': [
                               {'id': 0, 'properties': [{'name': 'collision', 'value': 'solid'}]}]}]}
    (distant / 'index.json').write_text(json.dumps(index), encoding='utf-8')
    routed = module.bake(binary, distant, folder / 'distant.json', (0, 0, 0, 0), 8, 0)
    assert routed['terrain_halo'][0] >= 2
    assert routed['chunks'][0]['rows'][0][-1] == '#'
    assert routed['chunks'][0]['rows'][0][:-1] == '.' * 31

    object_chunk = {'x': 2, 'y': 0, 'layers': {}, 'objects': [{
        'id': 1, 'persistent_id': 'marker:1', 'layer': 0, 'x': 512, 'y': 0, 'collision_shapes': [
            {'x': 255, 'y': 0, 'w': 8, 'h': 8}]}]}
    object_content = json.dumps(object_chunk, separators=(',', ':'))
    (distant / '2_0.json').write_text(object_content, encoding='utf-8')
    index['layers'] = [{'type': 'objectgroup'}]
    index['tilesets'] = []
    index['chunks'][0]['bytes'] = len(object_content.encode())
    index['object_coverage'] = [{'x': 0, 'y': 0, 'anchors': [{'x': 2, 'y': 0}]}]
    (distant / 'index.json').write_text(json.dumps(index), encoding='utf-8')
    covered = module.bake(binary, distant, folder / 'covered.json', (0, 0, 0, 0), 8, 0)
    assert covered['terrain_halo'] == [1, 1]
    assert covered['chunks'][0]['rows'][0][-1] == '#'

print('Native navigation bake: deterministic map, offset collision and object coverage passed')
