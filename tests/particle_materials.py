"""Actual native pixel checks; requires Pillow (development only) and a display."""
import argparse
import json
from pathlib import Path
import subprocess
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary',type=Path)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
args.output.mkdir(parents=True,exist_ok=True)
capture=args.output.resolve()/'particle-materials.png'
project=ROOT/'examples/particle_materials'
command=[str(args.binary.resolve()),str(project),'--frames','8','--replay',str(project/'smoke.txt')]
def run(extra):
    result=subprocess.run(command+extra,capture_output=True,text=True,encoding='utf-8',timeout=30)
    if result.returncode: raise AssertionError(result.stderr or result.stdout)
    return json.loads(result.stdout)
native=run(['--capture',str(capture)])
headless=run(['--headless'])
assert native==headless,'native/headless replay mismatch'
assert native['watches']['materials']['used']==9
image=Image.open(capture).convert('RGB')
scale=max(1,min(image.width//640,image.height//360))
left,top=(image.width-640*scale)//2,(image.height-360*scale)//2
background=(32,48,64)
checks=0
def pixel(x,y,expected):
    global checks
    actual=image.getpixel((left+int((x+.5)*scale),top+int((y+.5)*scale)))
    assert all(abs(a-b)<=3 for a,b in zip(actual,expected)),(x,y,actual,expected)
    checks+=1
def blend(destination,source,alpha,additive=False):
    return tuple(min(255,s*alpha+d*(1 if additive else 1-alpha)) for d,s in zip(destination,source))
alpha=128/255
red=(255,0,0); blue=(0,0,255)
pixel(100,155,blend(blend(background,red,alpha),blue,alpha))
pixel(280,155,blend(blend(background,red,alpha,True),blue,alpha,True))
pixel(460,155,blend(blend(background,red,alpha,True),blue,alpha))
pixel(465,260,(64,208,128)) # Restore white texture and alpha after additive atlas.
pixel(550,260,blend(background,red,alpha)) # Scene target must also be copied, not blended twice.
pixel(10,10,background) # Expired first slot must not change remaining order.
source=Image.open(project/'assets/keeper.png').convert('RGBA')
for y in range(18):
    for x in range(12):
        rgba=source.getpixel((x,y)); intensity=rgba[3]/255*192/255
        for origin,additive in [(95,False),(275,True)]:
            pixel(origin+x*3+1,245+y*3+1,blend(background,rgba[:3],intensity,additive))
print(json.dumps({'ok':True,'pixel_checks':checks,'active_particles':9,'capture':str(capture)}))
