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
    def test_xml_object_template_with_tsx_and_property_origins(self):
        from PIL import Image
        with tempfile.TemporaryDirectory(prefix='shiny-tx-') as temp:
            root=Path(temp);(root/'tiles').mkdir();(root/'templates').mkdir();(root/'images').mkdir()
            Image.new('RGBA',(8,8),(150,80,40,255)).save(root/'images/icon.png')
            (root/'tiles/set.tsx').write_text('''<tileset name="icons" tilewidth="8" tileheight="8" tilecount="1" columns="0">
                <tile id="3"><image source="../images/icon.png"/></tile></tileset>''',encoding='utf-8')
            (root/'templates/default.txt').write_text('template note',encoding='utf-8')
            (root/'map-note.txt').write_text('map note',encoding='utf-8')
            template='''<?xml version="1.0" encoding="UTF-8"?>
                <template><tileset firstgid="5" source="../tiles/set.tsx"/>
                <object name="sign" class="marker" gid="8" width="8" height="8" visible="1">
                 <properties><property name="note" type="file" value="default.txt"/>
                   <property name="title" value="from template"/>
                   <property name="count" type="int" value="7"/></properties>
                </object></template>'''
            (root/'templates/sign.tx').write_text(template,encoding='utf-8')
            data={'orientation':'orthogonal','tilewidth':8,'tileheight':8,
                  'tilesets':[{'firstgid':6,'source':'tiles/set.tsx'}],
                  'layers':[{'type':'objectgroup','name':'markers','objects':[
                      {'id':9,'template':'templates/sign.tx','x':16,'y':8,
                       'properties':[{'name':'title','type':'string','value':'override'}]},
                      {'id':10,'template':'templates/sign.tx','x':24,'y':8,
                       'properties':[{'name':'note','type':'file','value':'map-note.txt'}]}]}]}
            (root/'map.json').write_text(json.dumps(data),encoding='utf-8')
            manifest=root/'assets.json';manifest.write_text('{"maps":{"world":"map.json"}}',encoding='utf-8')
            built=assets.build(manifest,root/'cache')
            chunk=json.loads((built/'map-world/0_0.json').read_text(encoding='utf-8'))
            first,second=chunk['objects']
            self.assertEqual((first['id'],first['persistent_id'],first['gid'],first['type']),
                             (9,'markers:9',9,'marker'))
            self.assertEqual({p['name']:p['value'] for p in first['properties']},
                             {'count':7,'note':'templates/default.txt','title':'override'})
            self.assertEqual({p['name']:p['value'] for p in second['properties']}['note'],'map-note.txt')
            inputs=json.loads((built/'index.json').read_text(encoding='utf-8'))['inputs']
            self.assertTrue({'templates/sign.tx','tiles/set.tsx','templates/default.txt','map-note.txt'}<=inputs.keys())
            self.assertEqual((built/'map-world/index.json').read_bytes(),
                             (assets.build(manifest,root/'clean')/'map-world/index.json').read_bytes())
            (root/'templates/sign.tx').write_text(template.replace('count" type="int" value="7"','count" type="int" value="8"'),encoding='utf-8')
            self.assertNotEqual(built.name,assets.build(manifest,root/'cache').name)
            data['tilesets']=[]
            (root/'map.json').write_text(json.dumps(data),encoding='utf-8')
            imported=assets.build(manifest,root/'template-only')
            self.assertEqual(json.loads((imported/'map-world/index.json').read_text(encoding='utf-8'))['tilesets'][0]['firstgid'],1)
            self.assertEqual(json.loads((imported/'map-world/0_0.json').read_text(encoding='utf-8'))['objects'][0]['gid'],4)

    def test_xml_object_template_rejects_bad_structure(self):
        with tempfile.TemporaryDirectory(prefix='shiny-tx-invalid-') as temp:
            root=Path(temp)
            data={'orientation':'orthogonal','tilewidth':8,'tileheight':8,'tilesets':[],
                  'layers':[{'type':'objectgroup','name':'markers','objects':[
                      {'id':1,'template':'sign.tx','x':0,'y':0}]}]}
            (root/'map.json').write_text(json.dumps(data),encoding='utf-8')
            manifest=root/'assets.json';manifest.write_text('{"maps":{"world":"map.json"}}',encoding='utf-8')
            for content,field in [('<template><object/><object/></template>','one object'),
                                  ('<template><object template="other.tx"/></template>','nested templates'),
                                  ('<template><object><properties><property name="x" type="int" value="bad"/></properties></object></template>','invalid number'),
                                  ('<!DOCTYPE template [<!ENTITY x "y">]><template><object/></template>','DTD/entity')]:
                (root/'sign.tx').write_text(content,encoding='utf-8')
                with self.subTest(field=field),self.assertRaisesRegex(ValueError,field):
                    assets.build(manifest,root/'cache')
                self.assertFalse((root/'cache').exists())

    def test_external_tsx_atlas_collection_and_cache(self):
        from PIL import Image
        with tempfile.TemporaryDirectory(prefix='shiny-tsx-') as temp:
            root=Path(temp);(root/'tiles').mkdir();(root/'images').mkdir()
            (root/'map-note.txt').write_text('map',encoding='utf-8')
            Image.new('RGBA',(16,8),(255,0,0,255)).save(root/'images/atlas.png')
            Image.new('RGBA',(6,10),(0,255,0,255)).save(root/'images/leaf.png')
            (root/'tiles/note.txt').write_text('item',encoding='utf-8')
            atlas='''<?xml version="1.0" encoding="UTF-8"?>
<tileset name="ground" tilewidth="8" tileheight="8" tilecount="2" columns="2">
 <properties><property name="level" type="int" value="2147483647"/>
  <property name="target" type="object" value="4294967295"/></properties>
 <tileoffset x="2" y="-1"/><image source="../images/atlas.png" width="16" height="8"/>
 <tile id="0"><properties><property name="collision" value="solid"/></properties>
  <objectgroup><object id="3" x="1" y="1"><polygon points="0,0 6,0 6,3 3,3 3,6 0,6"/></object></objectgroup>
  <animation><frame tileid="0" duration="100"/><frame tileid="1" duration="150"/></animation>
 </tile>
</tileset>'''
            collection='''<tileset name="leaves" tilewidth="6" tileheight="10" tilecount="1" columns="0">
 <tile id="3"><image source="../images/leaf.png"/>
  <properties><property name="source_note" type="file" value="note.txt"/></properties>
 </tile>
</tileset>'''
            (root/'tiles/ground.tsx').write_text(atlas,encoding='utf-8')
            (root/'tiles/leaves.tsx').write_text(collection,encoding='utf-8')
            (root/'icon.json').write_text(json.dumps({'type':'template',
                'tileset':{'firstgid':1,'source':'tiles/leaves.tsx'},
                'object':{'gid':4,'width':6,'height':10}}),encoding='utf-8')
            map_data={'orientation':'orthogonal','tilewidth':8,'tileheight':8,
                      'tilesets':[{'firstgid':1,'source':'tiles/ground.tsx'},
                                  {'firstgid':3,'source':'tiles/leaves.tsx'}],
                      'layers':[{'type':'tilelayer','name':'floor','width':2,'height':1,'data':[1,6],
                                 'properties':[{'name':'hint','type':'file','value':'map-note.txt'}]},
                                {'type':'objectgroup','name':'markers','objects':[
                                    {'id':5,'template':'icon.json','x':8,'y':8}]}]}
            (root/'map.json').write_text(json.dumps(map_data),encoding='utf-8')
            manifest=root/'assets.json';manifest.write_text('{"maps":{"world":"map.json"}}',encoding='utf-8')
            first=assets.build(manifest,root/'cache');index=json.loads((first/'map-world/index.json').read_text(encoding='utf-8'))
            ground,leaves=index['tilesets']
            self.assertEqual([p['value'] for p in ground['properties']],[2147483647,4294967295])
            self.assertEqual(index['layers'][0]['properties'][0]['value'],'map-note.txt')
            self.assertEqual((ground['image'],ground['tileoffset']),('images/atlas.png',{'x':2.0,'y':-1.0}))
            self.assertEqual([frame['duration'] for frame in ground['tiles'][0]['animation']],[100,150])
            self.assertEqual(len(ground['tiles'][0]['collision_shapes']),4)
            self.assertEqual((leaves['tiles'][0]['id'],leaves['tiles'][0]['image'],leaves['tiles'][0]['imagewidth']),
                             (3,'images/leaf.png',6))
            self.assertEqual(leaves['tiles'][0]['properties'][0]['value'],'tiles/note.txt')
            self.assertEqual(json.loads((first/'map-world/0_0.json').read_text(encoding='utf-8'))['layers']['0'][:2],[1,6])
            self.assertEqual(json.loads((first/'map-world/0_0.json').read_text(encoding='utf-8'))['objects'][0]['gid'],6)
            self.assertTrue({'tiles/ground.tsx','tiles/leaves.tsx','images/atlas.png','images/leaf.png','tiles/note.txt','map-note.txt'}
                            <=json.loads((first/'index.json').read_text(encoding='utf-8'))['inputs'].keys())
            self.assertEqual((first/'map-world/index.json').read_bytes(),
                             (assets.build(manifest,root/'clean')/'map-world/index.json').read_bytes())
            (root/'tiles/ground.tsx').write_text(atlas.replace('duration="150"','duration="200"'),encoding='utf-8')
            self.assertNotEqual(first.name,assets.build(manifest,root/'cache').name)

    def test_external_tsx_rejects_unsupported_or_missing_data(self):
        from PIL import Image
        with tempfile.TemporaryDirectory(prefix='shiny-tsx-invalid-') as temp:
            root=Path(temp);Image.new('RGBA',(8,8)).save(root/'image.png')
            map_data={'orientation':'orthogonal','tilewidth':8,'tileheight':8,
                      'tilesets':[{'firstgid':1,'source':'set.tsx'}],
                      'layers':[{'type':'tilelayer','name':'floor','width':1,'height':1,'data':[1]}]}
            (root/'map.json').write_text(json.dumps(map_data),encoding='utf-8')
            manifest=root/'assets.json';manifest.write_text('{"maps":{"world":"map.json"}}',encoding='utf-8')
            base='<tileset name="set" tilewidth="8" tileheight="8" tilecount="1" columns="1"><image source="image.png"/></tileset>'
            for content,field in [(base.replace('image.png','missing.png'),'missing.png'),
                                  (base.replace('source="image.png"','source="image.png" width="7"'),'image size'),
                                  (base.replace('<image','<image trans="FF00FF"'),'tileset.image'),
                                  (base.replace('tilewidth="8"','tilewidth="0"'),'tileset.tilewidth'),
                                  (base.replace('</tileset>','<tile id="0"/><tile id="0"/></tileset>'),'duplicate tile ID'),
                                  ('<!DOCTYPE tileset [<!ENTITY bomb "x">]>'+base,'xml')]:
                (root/'set.tsx').write_text(content,encoding='utf-8')
                with self.subTest(field=field),self.assertRaisesRegex(ValueError,field):
                    assets.build(manifest,root/'cache')
                self.assertFalse((root/'cache').exists())

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
