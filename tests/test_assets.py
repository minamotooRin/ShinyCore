"""Exercise deterministic resource output, encoding, references and invalid geometry."""
import base64
import gzip
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

source=Path(__file__).resolve().parents[1]/'tools/assets.py'
spec=importlib.util.spec_from_file_location('assets',source)
assets=importlib.util.module_from_spec(spec)
spec.loader.exec_module(assets)


class Assets(unittest.TestCase):
    def test_supported_encodings(self):
        values=[0,1,0x80000002,3]
        raw=struct.pack('<4I',*values)
        for compression,encoded in [('',raw),('zlib',zlib.compress(raw)),('gzip',gzip.compress(raw,mtime=0))]:
            layer={'width':2,'height':2,'encoding':'base64','compression':compression,'data':base64.b64encode(encoded).decode()}
            self.assertEqual(assets.decode_tiles(layer),values)
        layer['width']=1
        with self.assertRaises(ValueError): assets.decode_tiles(layer)
        layer['compression']='zstd'
        with self.assertRaises(ValueError): assets.decode_tiles(layer)

    def test_concave_geometry(self):
        polygon=[{'x':x,'y':y} for x,y in [(0,0),(8,0),(8,3),(3,3),(3,8),(0,8)]]
        triangles=assets.triangulate(polygon)
        self.assertEqual(len(triangles),4)
        area=sum(abs((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]))/2 for a,b,c in triangles)
        self.assertEqual(area,39)
        with self.assertRaises(ValueError): assets.triangulate([{'x':0,'y':0}]*3)

    def test_negative_chunks_and_rebuild(self):
        with tempfile.TemporaryDirectory(prefix='shiny-assets-') as temp:
            root=Path(temp)
            def write(name,value): (root/name).write_text(json.dumps(value),encoding='utf-8')
            write('map.json',{'orientation':'orthogonal','tilewidth':8,'tileheight':8,'infinite':True,
                'layers':[{'id':1,'name':'land','type':'tilelayer','chunks':[
                    {'x':-33,'y':-1,'width':3,'height':1,'data':[1,2,3]}]}]})
            write('assets.json',{'maps':{'world':'map.json'}})
            first=assets.build(root/'assets.json',root/'a')
            second=assets.build(root/'assets.json',root/'b')
            self.assertEqual(first.name,second.name)
            files={p.relative_to(first):p.read_bytes() for p in first.rglob('*') if p.is_file()}
            self.assertEqual(files,{p.relative_to(second):p.read_bytes() for p in second.rglob('*') if p.is_file()})
            index=json.loads((first/'map-world/index.json').read_text())
            self.assertEqual([(c['x'],c['y']) for c in index['chunks']],[(-2,-1),(-1,-1)])
            block=json.loads((first/'map-world/-2_-1.json').read_text())
            self.assertEqual(block['layers']['0'][1023],1)
            write('assets.json',{'maps':{'../escape':'map.json'}})
            with self.assertRaises(ValueError): assets.build(root/'assets.json',root/'a')

    def test_atlas_and_image_dependency(self):
        from PIL import Image
        with tempfile.TemporaryDirectory(prefix='shiny-atlas-') as temp:
            root=Path(temp)
            Image.new('RGBA',(4,4),(255,0,0,255)).save(root/'sprite.png')
            manifest=root/'assets.json'
            manifest.write_text(json.dumps({'atlases':{'sprites':{'width':16,'images':{'hero':'sprite.png'}}}}))
            first=assets.build(manifest,root/'cache')
            index=json.loads((first/'index.json').read_text())
            rect=index['atlases']['sprites']['regions']['hero']
            with Image.open(first/'atlas-sprites.png') as atlas:
                self.assertEqual(atlas.getpixel((rect['x'],rect['y'])),(255,0,0,255))
            Image.new('RGBA',(4,4),(0,255,0,255)).save(root/'sprite.png')
            self.assertNotEqual(first.name,assets.build(manifest,root/'cache').name)


if __name__=='__main__': unittest.main()
