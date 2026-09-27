"""Hidden, bounded native checks for nine-slice geometry and the anchored UI example."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

from PIL import Image

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary',type=Path)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
binary=args.binary.resolve(); output=args.output.resolve(); output.mkdir(parents=True,exist_ok=False)
root=Path(__file__).resolve().parents[1]
project=output/'geometry';project.mkdir()
shutil.copyfile(root/'examples/ui_panels/assets/panel.png',project/'panel.png')
(project/'project.lua').write_text('return {display={width=320,height=180},resources={panel={type="image",path="panel.png"}}}')
(project/'main.lua').write_text('''local slice={left=4,right=4,top=4,bottom=4}
return {width=320,height=180,ambient=1,draw=function()
    sc.rect(0,0,320,180,'#111C2E',true)
    sc.image('panel',8,8,80,48,{slice=slice,screen=true})
    sc.image('panel',104,8,80,48,{slice=slice,screen=true,flip_x=true})
    sc.image('panel',200,8,80,48,{slice=slice,screen=true,diagonal=true})
    sc.image('panel',8,80,5,5,{slice=slice,screen=true})
    sc.clip(46,86,68,36)
    sc.image('panel',40,80,80,48,{slice=slice,screen=true})
    sc.clip()
    sc.image('panel',136,80,80,48,{source_w=16,source_h=16,slice=slice,screen=true,color='#FFFFFF80'})
    sc.image('panel',230,80,0,20,{slice=slice,screen=true})
end}
''')
idle=output/'idle.jsonl';idle.write_text('{"version":3}\n{"frame":0,"keys":[],"gamepad":{"connected":false}}\n')
report={'engine_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'visual_review':'pending','cases':[]}
for name,source in [('geometry',project),('anchors',root/'examples/ui_panels')]:
    command=[str(binary),str(source),'--frames','1','--replay',str(idle),'--mute','--capture-hidden',
             '--capture',str(output/(name+'.png')),'--save-dir',str(output/'saves')]
    result=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',timeout=15)
    (output/(name+'.log')).write_text(result.stderr,encoding='utf-8')
    assert result.returncode==0,result.stderr
    (output/(name+'.json')).write_text(result.stdout,encoding='utf-8')
    if name=='geometry':
        teal,gold,blue,purple=(102,217,176),(255,203,119),(94,164,230),(196,128,220)
        expected={(8,8):teal,(87,8):gold,(8,55):blue,(87,55):purple,
                  (104,8):gold,(183,8):teal,(104,55):purple,(183,55):blue,
                  (200,8):teal,(279,8):blue,(200,55):gold,(279,55):purple,
                  (8,80):teal,(12,80):gold,(8,84):blue,(12,84):purple,
                  (40,80):(17,28,46),(46,86):(29,44,67),(230,80):(17,28,46)}
        with Image.open(output/(name+'.png')) as image:
            rgb=image.convert('RGB')
            for point,color in expected.items(): assert rgb.getpixel(point)==color,(point,rgb.getpixel(point),color)
            assert all(abs(a-b)<=1 for a,b in zip(rgb.getpixel((150,95)),(23,36,57)))
    else:
        final=json.loads(result.stdout)
        assert final['watches']=={'focus':'center','selected':'center'},final['watches']
    report['cases'].append({'case':name,'command':command,'checks':'passed'})
(output/'manifest.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('nine-slice geometry and anchor captures passed; visual inspection pending')
