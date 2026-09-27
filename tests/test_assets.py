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
    def test_group_layer_inheritance(self):
        with tempfile.TemporaryDirectory(prefix='shiny-layers-') as temp:
            root=Path(temp)
            child={'type':'tilelayer','name':'ground','width':1,'height':1,'data':[0],
                   'tintcolor':'#8080FF80','opacity':.5,'offsetx':2,'parallaxx':.5}
            group={'type':'group','name':'world','tintcolor':'#80FF8040','opacity':.5,
                   'offsetx':-8,'parallaxx':.5,'visible':False,'layers':[child]}
            data={'orientation':'orthogonal','tilewidth':8,'tileheight':8,'layers':[group]}
            def compile():
                (root/'map.json').write_text(json.dumps(data))
                return assets.tiled(root,'map.json',lambda p:p.read_bytes())[0]['layers'][0]
            layer=compile()
            self.assertEqual(layer['tintcolor'],'#40808020')
            self.assertEqual((layer['opacity'],layer['offsetx'],layer['parallaxx'],layer['visible']),(.25,-6,.25,False))
            self.assertNotIn('_tint',layer)
            for field,bad in [('tintcolor','#GGFFFF'),('opacity',True),('opacity',1.01),
                              ('visible','false'),('offsetx',float('inf')),('parallaxx',float('nan')),
                              ('mode','multiply'),('transparentcolor','#FF00FF')]:
                with self.subTest(field=field,bad=bad):
                    old=dict(child);child[field]=bad
                    with self.assertRaisesRegex(ValueError,'map.json:world/ground'):compile()
                    child.clear();child.update(old)
            group['parallaxx']=100;child['parallaxx']=2
            with self.assertRaisesRegex(ValueError,'inherited parallaxx'):compile()

    def test_template_properties_and_dependencies(self):
        with tempfile.TemporaryDirectory(prefix='shiny-template-') as temp:
            root=Path(temp);(root/'templates').mkdir();(root/'maps').mkdir()
            def write(name,value): (root/name).write_text(json.dumps(value),encoding='utf-8')
            (root/'templates/dialogue.txt').write_text('hello',encoding='utf-8')
            (root/'maps/override.txt').write_text('override',encoding='utf-8')
            write('templates/npc.json',{'type':'template','object':{'name':'healer','width':12,'properties':[
                {'name':'hp','type':'int','value':10}, {'name':'role','value':'healer'},
                {'name':'dialogue','type':'file','value':'dialogue.txt'},
                {'name':'replaced','type':'file','value':'unused-missing.txt'}]}})
            write('maps/map.json',{'orientation':'orthogonal','tilewidth':8,'tileheight':8,'layers':[
                {'name':'world','type':'group','offsetx':-256,'layers':[
                    {'name':'actors','type':'objectgroup','offsety':256,'objects':[
                        {'id':7,'template':'../templates/npc.json','x':8,'y':0,'properties':[
                            {'name':'hp','type':'int','value':20},
                            {'name':'replaced','type':'file','value':'override.txt'}]}]}]}]})
            write('assets.json',{'maps':{'world':'maps/map.json'}})
            built=assets.build(root/'assets.json',root/'cache')
            index=json.loads((built/'index.json').read_text())
            chunk=json.loads((built/'map-world/-1_1.json').read_text())
            obj=chunk['objects'][0];props={p['name']:p['value'] for p in obj['properties']}
            self.assertEqual((obj['id'],obj['name'],obj['width'],obj['persistent_id']),(7,'healer',12,'world/actors:7'))
            self.assertEqual((obj['x'],obj['y']),(-248,256))
            self.assertEqual(props,{'hp':20,'role':'healer','dialogue':'templates/dialogue.txt','replaced':'maps/override.txt'})
            self.assertNotIn('template',obj)
            self.assertTrue({'templates/npc.json','templates/dialogue.txt','maps/override.txt'}<=index['inputs'].keys())
            self.assertEqual(built.name,assets.build(root/'assets.json',root/'second').name)
            (root/'templates/dialogue.txt').write_text('changed',encoding='utf-8')
            self.assertNotEqual(built.name,assets.build(root/'assets.json',root/'cache').name)
            (root/'templates/dialogue.txt').unlink()
            with self.assertRaisesRegex(ValueError,'maps/map.json:world/actors: object 7'):
                assets.build(root/'assets.json',root/'cache')

    def test_template_tile_gid_mapping(self):
        from PIL import Image
        with tempfile.TemporaryDirectory(prefix='shiny-template-gid-') as temp:
            root=Path(temp);(root/'templates').mkdir();(root/'tiles').mkdir()
            def write(name,value): (root/name).write_text(json.dumps(value),encoding='utf-8')
            Image.new('RGBA',(16,8),(255,128,0,255)).save(root/'tiles/image.png')
            write('tiles/set.json',{'name':'symbols','tilewidth':8,'tileheight':8,'tilecount':2,'columns':2,
                'image':'image.png','imagewidth':16,'imageheight':8})
            template={'type':'template','tileset':{'firstgid':1,'source':'../tiles/set.json'},
                      'object':{'gid':0xa0000002,'width':8,'height':8}}
            write('templates/icon.json',template)
            data={'orientation':'orthogonal','tilewidth':8,'tileheight':8,
                'tilesets':[{'firstgid':1,'tilecount':16},{'firstgid':17,'source':'tiles/set.json'}],
                'layers':[{'name':'icons','type':'objectgroup','objects':[
                    {'id':1,'template':'templates/icon.json'},
                    {'id':2,'template':'templates/icon.json','gid':18}]}]}
            def compile():
                write('map.json',data)
                return assets.tiled(root,'map.json',lambda p:p.read_bytes())
            metadata,blocks=compile()
            self.assertEqual([o['gid'] for o in blocks[0,0]['objects']],[0xa0000012,18])
            self.assertEqual(len(metadata['tilesets']),2)
            data['tilesets'].pop() # A template-only tileset is assigned the next unused range.
            metadata,blocks=compile()
            self.assertEqual(metadata['tilesets'][-1]['firstgid'],17)
            self.assertEqual(blocks[0,0]['objects'][0]['gid'],0xa0000012)
            self.assertEqual(metadata['tilesets'][-1]['image'],'tiles/image.png')
            template['object']['gid']=3;write('templates/icon.json',template)
            with self.assertRaisesRegex(ValueError,'object 1: template object gid'):
                compile()
            # Sparse image collections reserve through their largest local ID, not tilecount.
            write('tiles/set.json',{'name':'sparse','tilewidth':8,'tileheight':8,'tilecount':1,'columns':0,
                'tiles':[{'id':7,'image':'image.png'}]})
            template['object']['gid']=8;write('templates/icon.json',template)
            write('tiles/second.json',{'name':'second','tilewidth':8,'tileheight':8,'tilecount':2,'columns':2,
                'image':'image.png','imagewidth':16,'imageheight':8})
            write('templates/second.json',{'type':'template','tileset':{'firstgid':1,'source':'../tiles/second.json'},
                'object':{'gid':1}})
            data['layers'][0]['objects'][1]={'id':2,'template':'templates/second.json'}
            metadata,blocks=compile()
            self.assertEqual([entry['firstgid'] for entry in metadata['tilesets']],[1,17,25])
            self.assertEqual([obj['gid'] for obj in blocks[0,0]['objects']],[24,25])

    def test_template_diagnostics(self):
        with tempfile.TemporaryDirectory(prefix='shiny-template-errors-') as temp:
            root=Path(temp)
            data={'orientation':'orthogonal','tilewidth':8,'tileheight':8,
                'layers':[{'name':'actors','type':'objectgroup','objects':[{'id':2,'template':'npc.json'}]}]}
            (root/'map.json').write_text(json.dumps(data))
            for invalid in [[],{'object':{}},{'type':'template','object':{'template':'npc.json'}},
                    {'type':'template','object':{'properties':[{'name':'x'},{'name':'x'}]}}]:
                (root/'npc.json').write_text(json.dumps(invalid))
                with self.assertRaisesRegex(ValueError,'map.json:actors: object 2'):
                    assets.tiled(root,'map.json',lambda p:p.read_bytes())

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
                    {'x':-33,'y':-1,'width':3,'height':1,'data':[1,2,3]}]},
                    {'id':2,'name':'actors','type':'objectgroup','objects':[{'id':7,'x':8,'y':16}]}]})
            write('assets.json',{'maps':{'world':'map.json'}})
            first=assets.build(root/'assets.json',root/'a')
            second=assets.build(root/'assets.json',root/'b')
            self.assertEqual(first.name,second.name)
            files={p.relative_to(first):p.read_bytes() for p in first.rglob('*') if p.is_file()}
            self.assertEqual(files,{p.relative_to(second):p.read_bytes() for p in second.rglob('*') if p.is_file()})
            index=json.loads((first/'map-world/index.json').read_text())
            self.assertEqual([(c['x'],c['y']) for c in index['chunks']],[(-2,-1),(-1,-1),(0,0)])
            self.assertNotIn('objects',index)
            self.assertEqual(index['format'],3)
            objects=json.loads((first/'map-world/0_0.json').read_text())
            self.assertEqual(objects['objects'][0]['persistent_id'],'actors:7')
            self.assertEqual(objects['objects'][0]['layer'],1)
            self.assertEqual(objects['layers'],{})
            block=json.loads((first/'map-world/-2_-1.json').read_text())
            self.assertEqual(block['layers']['0'][1023],1)
            write('assets.json',{'maps':{'../escape':'map.json'}})
            with self.assertRaises(ValueError): assets.build(root/'assets.json',root/'a')

    def test_object_ownership_and_validation(self):
        with tempfile.TemporaryDirectory(prefix='shiny-objects-') as temp:
            root=Path(temp)
            layer={'name':'actors','type':'objectgroup','offsetx':-256,'objects':[
                {'id':1,'x':0,'y':0,'width':1000}, {'id':2,'x':255.5,'y':-0.5},
                {'id':3,'x':256,'y':0}]}
            data={'orientation':'orthogonal','tilewidth':8,'tileheight':8,
                  'layers':[{'name':'world','type':'group','offsety':256,'layers':[layer]}]}
            def compile():
                (root/'map.json').write_text(json.dumps(data),encoding='utf-8')
                return assets.tiled(root,'map.json',lambda p:p.read_bytes())
            metadata,blocks=compile()
            self.assertEqual(set(blocks),{(-1,1),(-1,0),(0,1)})
            obj=blocks[-1,1]['objects'][0]
            self.assertEqual((obj['x'],obj['y'],obj['persistent_id']),(-256,256,'world/actors:1'))
            self.assertEqual(sum(len(b['objects']) for b in blocks.values()),3)
            for invalid in [float('nan'),float('inf'),True,8000257]:
                layer['objects'][0]['x']=invalid
                with self.assertRaisesRegex(ValueError,'map.json:world/actors: object 1'):
                    compile()
            layer['objects'][0]['x']=0
            layer['objects'][1]['id']=1
            with self.assertRaisesRegex(ValueError,'duplicate persistent_id'): compile()
            layer['objects'][1]['id']=2
            layer['name']='invalid name'
            with self.assertRaisesRegex(ValueError,'ASCII layer path'): compile()

    def test_aseprite_restores_canvas_tags_and_cache(self):
        from PIL import Image
        with tempfile.TemporaryDirectory(prefix='shiny-aseprite-') as temp:
            root=Path(temp);(root/'source').mkdir()
            image=Image.new('RGBA',(4,2));image.putpixel((0,0),(255,0,0,128));image.putpixel((2,1),(0,255,0,255))
            image.save(root/'source/sheet.png')
            frames=[{'filename':str(i),'frame':{'x':i*2,'y':0,'w':2,'h':2},'trimmed':True,
                     'spriteSourceSize':{'x':1,'y':1,'w':2,'h':2},'sourceSize':{'w':4,'h':4},'duration':100+i*50}
                    for i in range(2)]
            tags=[{'name':'reverse','from':0,'to':1,'direction':'reverse','repeat':'2'},
                  {'name':'quote"\\\n','from':0,'to':1,'direction':'pingpong'}]
            source={'frames':frames,'meta':{'image':'sheet.png','size':{'w':4,'h':2},'frameTags':tags}}
            (root/'source/hero.json').write_text(json.dumps(source))
            manifest=root/'assets.json';manifest.write_text(json.dumps({'animations':{'hero':'source/hero.json'}}))
            first=assets.build(manifest,root/'cache');index=json.loads((first/'index.json').read_text())
            a=index['animations']['hero'];self.assertEqual((a['frame_w'],a['frame_h']),(4,4))
            self.assertEqual([f['frame'] for f in a['clips']['reverse']['frames']],[1,0,1,0])
            self.assertFalse(a['clips']['reverse']['loop'])
            with Image.open(first/a['image']) as atlas:
                self.assertEqual(atlas.getpixel((1,1)),(255,0,0,128))
                self.assertEqual(atlas.getpixel((5,2)),(0,255,0,255))
                self.assertEqual(atlas.getpixel((0,0)),(0,0,0,0))
            self.assertIn('source/sheet.png',index['inputs'])
            self.assertEqual(assets.build(manifest,root/'cache'),first)
            second=assets.build(manifest,root/'clean')
            for name in ['index.json','animations.lua',a['image']]:self.assertEqual((first/name).read_bytes(),(second/name).read_bytes())
            image.putpixel((0,0),(0,0,255,128));image.save(root/'source/sheet.png')
            self.assertNotEqual(assets.build(manifest,root/'cache').name,first.name)

    def test_aseprite_rejects_bad_fields_before_publication(self):
        from PIL import Image
        import copy
        with tempfile.TemporaryDirectory(prefix='shiny-aseprite-invalid-') as temp:
            root=Path(temp);Image.new('RGBA',(2,2)).save(root/'sheet.png')
            frame={'frame':{'x':0,'y':0,'w':2,'h':2},'duration':100}
            base={'frames':{'first':frame},'meta':{'image':'sheet.png'}}
            manifest=root/'assets.json';manifest.write_text('{"animations":{"hero":"hero.json"}}')
            cases=[('duration',0),('duration',True),('rotated',True),('trimmed',True),('frame',{'x':1,'y':0,'w':2,'h':2})]
            for field,value in cases:
                data=copy.deepcopy(base);data['frames']['first'][field]=value
                (root/'hero.json').write_text(json.dumps(data))
                with self.subTest(field=field,value=value),self.assertRaisesRegex(ValueError,'hero.json:frames\\[0\\]'):
                    assets.build(manifest,root/'cache')
                self.assertFalse((root/'cache').exists())
            for tag in [{'name':'bad','from':0,'to':1},{'name':'bad','from':0,'to':0,'direction':'unknown'},
                        {'name':'bad','from':0,'to':0,'repeat':-1}]:
                data=copy.deepcopy(base);data['meta']['frameTags']=[tag]
                (root/'hero.json').write_text(json.dumps(data))
                with self.assertRaisesRegex(ValueError,'hero.json:meta.frameTags'):assets.build(manifest,root/'cache')
            (root/'hero.json').write_text(json.dumps(base));good=assets.build(manifest,root/'cache')
            a=json.loads((good/'index.json').read_text())['animations']['hero']
            self.assertEqual(a['frames'][0]['name'],'first');self.assertTrue(a['clips']['all']['loop'])

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
