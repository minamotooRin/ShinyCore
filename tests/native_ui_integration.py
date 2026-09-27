"""Opt-in hidden native UI checks: glyph detail, nested clips and resize variants."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
from PIL import Image

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary',type=Path)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
binary=args.binary.resolve();output=args.output.resolve();output.mkdir(parents=True,exist_ok=False)
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='shiny-native-ui-') as directory:
    project=Path(directory)
    shutil.copy2(root/'examples/wayfarer/assets/SourceHanSansSC-Regular.otf',project/'font.otf')
    (project/'project.lua').write_text('return {resources={ui={type="font",path="font.otf",size=16}}}',encoding='utf-8')
    for name,width,height,smooth in [('integer',1152,648,False),('letterbox',1280,720,False),('smooth',1000,700,True)]:
        (project/'main.lua').write_text('''return {width=384,height=216,gravity=0,ambient=1,
update=function()
    if sc.tick()==1 then assert(sc.settings.apply({width=%d,height=%d,scale="%s",vsync=false})) end
end,
draw=function()
    sc.rect(0,0,384,216,"#000000",true)
    sc.text("中文笔画清晰",8,8,16,"#FFFFFF",true,{font="ui"})
    sc.clip(10,60,20,20)
    sc.rect(0,50,60,40,"#00FF00",true)
    sc.clip(20,70,30,30)
    sc.rect(0,50,60,50,"#FF0000",true)
    sc.clip()
    sc.rect(12,62,2,2,"#0000FF",true)
    sc.clip()
    sc.rect(45,65,2,2,"#FFFF00",true)
end}'''%(width,height,'smooth' if smooth else 'integer'),encoding='utf-8')
        capture=output/(name+'.png')
        result=subprocess.run([str(binary),str(project),'--frames','3','--capture-hidden','--mute',
            '--capture',str(capture),'--save-dir',str(project/name)],capture_output=True,text=True,encoding='utf-8',timeout=20)
        (output/(name+'.log')).write_text(result.stderr,encoding='utf-8')
        assert result.returncode==0,result.stderr
        assert json.loads(result.stdout)['frames']==3
        with Image.open(capture) as image:
            assert image.size==(width,height)
            fit=min(width/384,height/216);scale=fit if smooth else int(fit)
            ox=(width-384*scale)/2;oy=(height-216*scale)/2
            def pixel(x,y):return image.getpixel((round(ox+x*scale),round(oy+y*scale)))[:3]
            for x,y,color in [(5,65,(0,0,0)),(15,65,(0,255,0)),(25,75,(255,0,0)),
                              (35,75,(0,0,0)),(13,63,(0,0,255)),(46,66,(255,255,0))]:
                assert pixel(x,y)==color,(name,x,y,pixel(x,y))
            if name=='integer':
                # Upscaled logical glyphs repeat every 3x3 block. Native glyphs must contain finer detail.
                detail=0
                for y in range(24,72,3):
                    for x in range(24,300,3):
                        if len({image.getpixel((x+dx,y+dy)) for dx in range(3) for dy in range(3)})>1:detail+=1
                assert detail>100,('text still rasterized at logical resolution',detail)
print('Native UI glyph detail, nested clips, clip restoration and integer/smooth resize passed')
