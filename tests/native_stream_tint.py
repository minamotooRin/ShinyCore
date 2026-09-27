"""Short hidden native comparison of compiled group tint on tiles and image layers."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
from PIL import Image

root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'tools'))
import assets

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary',type=Path)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
binary=args.binary.resolve();out=args.output.resolve();project=out/'project'
project.mkdir(parents=True,exist_ok=True)
shutil.copytree(root/'lua/shiny',project/'lib/shiny',dirs_exist_ok=True)
Image.new('RGBA',(8,8),'white').save(project/'tile.png')
Image.new('RGBA',(64,48),'white').save(project/'panel.png')
def write(name,data): (project/name).write_text(json.dumps(data),encoding='utf-8')
def tiles(name,x,**extra):
    return {'type':'tilelayer','name':name,'width':8,'height':6,'data':[1]*48,'offsetx':x,'offsety':80,**extra}
write('map.json',{'orientation':'orthogonal','tilewidth':8,'tileheight':8,'tilesets':[
    {'firstgid':1,'name':'white','tilecount':1,'columns':1,'tilewidth':8,'tileheight':8,
     'image':'tile.png','imagewidth':8,'imageheight':8}],
    'layers':[tiles('reference',24),{'type':'group','name':'tinted','tintcolor':'#FF80C0FF','opacity':.75,
        'layers':[{'type':'imagelayer','name':'image','image':'panel.png','offsetx':152,'offsety':80,'tintcolor':'#FF80FF80'},
                  {'type':'group','name':'nested','tintcolor':'#80FFFFFF','layers':[tiles('tiles',280)]},
                  {'type':'imagelayer','name':'hidden','image':'panel.png','visible':False}]}]})
write('assets.json',{'maps':{'world':'map.json'}})
built=assets.build(project/'assets.json',out/'cache')
shutil.copytree(built/'map-world',project/'world',dirs_exist_ok=True)
(project/'project.lua').write_text('''return {id='test.tint',modules={'streaming'},
    limits={entities=1,particles=1,draws=128},resources={tile={type='image',path='tile.png'},panel={type='image',path='panel.png'}}}''',encoding='utf-8')
(project/'main.lua').write_text('''local Tiles=require('shiny.stream_tiles')
local view,prepared,calls
local image=sc.image
sc.image=function(resource,x,y,w,h,options)
 calls[#calls+1]=options.color;return image(resource,x,y,w,h,options)
end
return {width=384,height=216,ambient=1,gravity=0,map={rows={'.'},background='#000000FF'},
 init=function()
  sc.stream.open('world/index.json');sc.stream.request(0,0,0)
  view=Tiles.new(sc.stream.metadata(),{['tile.png']='tile',['panel.png']='panel'})
  sc.camera.set{x=0,y=0,bounds=false}
 end,
 update=function()
  prepared=Tiles.prepare(view,{sc.stream.get(0,0)})
  sc.debug.watch('tint',{reference=view.layers[1].color,image=view.layers[2].color,tile=view.layers[3].color})
 end,
 draw=function()
  if prepared then
   calls={};Tiles.draw(view,prepared,{x=0,y=0,visible={x=0,y=0,w=384,h=216}})
   assert(#calls==97 and calls[1]=='#FFFFFFFF' and calls[49]=='#40C080BF' and calls[50]=='#80C0FF60')
  end
  sc.text('GROUP TINT / LAYER ALPHA',20,20,18,'#E6EDF7',true)
  sc.text('WHITE',24,60,12,'#A4B8D4',true)
  sc.text('IMAGE',152,60,12,'#A4B8D4',true)
  sc.text('TILE ALPHA',280,60,12,'#A4B8D4',true)
  sc.text('Group colors multiply; opacity stays separate.',20,160,10,'#A4B8D4',true)
 end}
''',encoding='utf-8')
write('replay.jsonl',{'version':3})
base=[str(binary),str(project),'--frames','2','--replay',str(project/'replay.jsonl'),'--save-dir',str(out/'saves')]
def run(name,options):
    command=base+options
    result=subprocess.run(command,capture_output=True,timeout=30)
    (out/(name+'.json')).write_bytes(result.stdout);(out/(name+'.log')).write_bytes(result.stderr)
    assert result.returncode==0,result.stderr
    return json.loads(result.stdout),command
headless,_=run('headless',['--headless'])
native,command=run('native',['--capture-hidden','--mute','--capture',str(out/'layers.png')])
assert headless['watches']==native['watches']
with Image.open(out/'layers.png') as picture:
    rgb=picture.convert('RGB');scale=picture.width/384
    pixels=[rgb.getpixel((round(x*scale),round(104*scale))) for x in (56,184,312)]
    for actual,expected in zip(pixels,((255,255,255),(48,144,96),(48,72,96))):
        assert max(abs(a-b) for a,b in zip(actual,expected))<=2,(actual,expected)
(out/'manifest.json').write_text(json.dumps({'command':command,'engine_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
    'tool_version':assets.VERSION,'pixels':pixels,'native_headless_watch_equal':True,'visual_review':'pending'},indent=2)+'\n',encoding='utf-8')
print('Tint: compiled groups, native commands and pixel samples passed; screenshot awaits visual review')
