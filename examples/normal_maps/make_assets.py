"""Offline deterministic sample assets; the packaged game does not need Python."""
from pathlib import Path
from math import sqrt
import json
from PIL import Image

root=Path(__file__).resolve().parent
color=Image.new('RGBA',(128,64));normal=Image.new('RGB',(128,64),(128,128,255))
for y in range(64):
    for x in range(128):
        u=((x%64)+.5-32)/29;v=(y+.5-32)/29
        inside=u*u+v*v<1 if x<64 else abs(u)<1 and abs(v)<1
        if not inside: continue
        if x<64:
            nx,ny=u*.85,v*.85;nz=sqrt(max(0,1-nx*nx-ny*ny));rgb=(175,184,199)
        else:
            nx=(1 if u>0 else -1)*max(0,abs(u)-.65)*2
            ny=(1 if v>0 else -1)*max(0,abs(v)-.65)*2
            nz=1;length=sqrt(nx*nx+ny*ny+nz*nz);nx/=length;ny/=length;nz/=length
            rgb=(203,133,79)
        color.putpixel((x,y),(*rgb,255))
        normal.putpixel((x,y),tuple(round((n*.5+.5)*255) for n in (nx,ny,nz)))
color.save(root/'atlas.png');normal.save(root/'normals.png')
data=[0]*28
for i in range(7): data[14+i]=2|((i&1)<<31)|(((i>>1)&1)<<30)|(((i>>2)&1)<<29)
level={'orientation':'orthogonal','renderorder':'right-down','infinite':False,'width':7,'height':4,'tilewidth':64,'tileheight':64,
       'tilesets':[{'firstgid':1,'image':'atlas.png','tilewidth':64,'tileheight':64,'columns':2,'tilecount':2,'objectalignment':'unspecified'}],
       'layers':[{'name':'normal tiles','type':'tilelayer','data':data}]}
(root/'room.tmj').write_text(json.dumps(level,indent=2)+'\n',encoding='utf-8')
