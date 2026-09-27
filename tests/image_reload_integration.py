"""Reload actual PNGs while paused over stdio; native captures are opt-in."""
import argparse
import hashlib
import json
from pathlib import Path
import queue
import shutil
import subprocess
import tempfile
import threading
from images_integration import png

ROOT=Path(__file__).resolve().parents[1]
RED=png((230,75,82,255)); GREEN=png((68,195,144,255))
CASES=('commit','cancel','corrupt','resize','retry','world')
SCENE='''local request,visible,failed
return {width=192,height=108,gravity=0,ambient=1,
init=function() assert(not pcall(sc.images.reload)) end,
update=function()
    local tick=sc.tick()
    if tick==0 then
        assert(not pcall(sc.images.reload))
        request=sc.images.prepare({"atlas","alias"})
    elseif tick==1 then sc.images.commit(request); request=nil; visible=true
    elseif tick==2 then
        request=sc.images.reload(); assert(sc.images.status(request).status=="pending")
        assert(sc.images.stats().pinned==2 and not pcall(sc.images.reload))
    elseif tick==3 then
        if request then FINISH; assert(not pcall(sc.images.status,request)); request=nil end
        assert(sc.images.stats().pinned==1)
    elseif tick==4 then request=sc.images.prepare({"atlas"})
    elseif tick==5 then sc.images.commit(request); request=nil end
    sc.debug.watch("reload",{failed=failed==true,pinned=sc.images.stats().pinned})
end,
ui_update=function()
    if request and sc.images.status(request).status=="failed" then
        assert(sc.tick()==3); failed=true
        RECOVER
    end
end,
draw=function()
    if visible then sc.image("alias",16,24,160,64,{screen=true}) end
    sc.text(failed and "RELOAD FAILED: OLD PIXELS" or "EXPLICIT IMAGE RELOAD",8,6,8,"#FFFFFF",true)
end}
'''
WORLD='''local World=require("shiny.stream_world")
local world,before,last
return {width=192,height=108,gravity=0,ambient=1,
init=function()
    world=World.new{index="index.json",name="world",slot="slot",margin=0,residency=true,
        images={["atlas.png"]="atlas"},prepare=function() error("unexpected object") end,export=function() return {} end}
    World.request(world,{{x=8,y=8}},0)
end,
update=function(dt)
    local changed,err,event=World.update(world,dt); assert(not err,err)
    if changed then before=world.chunks end
    if event then last=event end
    if sc.tick()==3 then
        assert(before and World.reload_images(world)); assert(not World.reload_images(world))
    elseif sc.tick()==4 then
        assert(last=="reloaded" and before==world.chunks and not World.status(world))
    end
    sc.debug.watch("reload",{failed=false,pinned=sc.images.stats().pinned,event=last})
end,
ui_update=function() World.ui_update(world) end,
draw=function()
    World.draw(world)
    if before then sc.image("atlas",16,24,160,64,{screen=true}) end
end}
'''


def run(binary,root,name,native):
    root.mkdir(parents=True)
    (root/'atlas.png').write_bytes(RED)
    (root/'project.lua').write_text('''return {id="reload",modules={"streaming","devtools"},resources={
atlas={type="image",path="atlas.png",stream=true},alias={type="image",path="atlas.png",stream=true}}}''')
    source=SCENE.replace('FINISH','sc.images.cancel(request)' if name=='cancel' else 'sc.images.commit(request)')
    source=source.replace('RECOVER','sc.log("REPAIR_PNG"); sc.images.retry(request)' if name=='retry' else 'sc.images.cancel(request); request=nil')
    initial,frames=2,6
    if name=='world':
        source=WORLD;initial,frames=3,5
        shutil.copytree(ROOT/'lua/shiny',root/'lib/shiny')
        chunk=json.dumps({'x':0,'y':0,'layers':{'0':[1]+[0]*1023},'objects':[]})
        (root/'chunk.json').write_text(chunk)
        (root/'index.json').write_text(json.dumps({'format':3,'chunk_size':32,'tilewidth':8,'tileheight':8,
            'layers':[{'type':'tilelayer'}],
            'tilesets':[{'firstgid':1,'tilecount':1,'columns':1,'tilewidth':8,'tileheight':8,'image':'atlas.png'}],
            'chunks':[{'x':0,'y':0,'path':'chunk.json','bytes':len(chunk)}]}))
    (root/'main.lua').write_text(source)
    (root/'idle.jsonl').write_text('{"version":3}\n{"frame":0,"keys":[],"gamepad":{"connected":false}}\n')
    command=[str(binary),str(root),'--debug-stdio','--frames',str(frames),'--mute',
        '--snapshot',str(root/'final.json'),'--replay',str(root/'idle.jsonl'),'--save-dir',str(root/'saves')]
    command+=['--capture-hidden','--capture',str(root/'final.png')] if native else ['--headless']
    process=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',bufsize=1)
    messages=queue.Queue();errors=[];transcript=[]
    def read_output():
        for line in process.stdout:
            try:messages.put(json.loads(line))
            except ValueError as error:messages.put(error)
        messages.put(EOFError('debug stream ended'))
    def read_errors():
        for line in process.stderr:
            errors.append(line)
            if name=='retry' and 'REPAIR_PNG' in line:
                temporary=root/'repair.tmp';temporary.write_bytes(GREEN);temporary.replace(root/'atlas.png')
    output_reader=threading.Thread(target=read_output,daemon=True);output_reader.start()
    error_reader=threading.Thread(target=read_errors,daemon=True);error_reader.start()
    identifier=0
    def message():
        item=messages.get(timeout=10)
        if isinstance(item,Exception):raise item
        transcript.append(item);return item
    def step(count):
        nonlocal identifier
        identifier+=1
        process.stdin.write(json.dumps({'id':identifier,'command':'step','count':count})+'\n');process.stdin.flush()
        reply=message();assert reply.get('id')==identifier and reply['ok'],reply
        stopped=message();assert stopped.get('event')=='stopped',stopped
    try:
        assert message()['event']=='ready'
        step(initial) # The old pixels are committed; the process is now paused.
        replacement=RED[:45] if name in ('corrupt','retry') else png((68,195,144,255),16,8) if name=='resize' else GREEN
        temporary=root/'replacement.tmp';temporary.write_bytes(replacement);temporary.replace(root/'atlas.png')
        step(frames-initial)
        assert message()['event']=='terminated'
        assert process.wait(timeout=10)==0,''.join(errors)
    finally:
        if process.poll() is None:process.kill();process.wait(timeout=10)
        process.stdin.close();output_reader.join(timeout=2);error_reader.join(timeout=2)
        process.stdout.close();process.stderr.close()
        (root/'stderr.log').write_text(''.join(errors),encoding='utf-8')
        (root/'protocol.json').write_text(json.dumps(transcript,indent=2))
    result=json.loads((root/'final.json').read_text())
    watch=result['watches']['reload']
    assert watch['pinned']==1 and watch['failed']==(name in ('corrupt','resize','retry')),watch
    if name=='world':assert watch['event']=='reloaded'
    if native:
        from PIL import Image
        with Image.open(root/'final.png') as picture:
            actual=picture.convert('RGB').getpixel((picture.width//2,picture.height//2))
        expected=(68,195,144) if name in ('commit','retry','world') else (230,75,82)
        assert actual==expected,(name,actual,expected)
    return {'case':name,'frames':frames,'watch':watch,'capture':'final.png' if native else None,
        'visual_review':'pending' if native else 'not-run'}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary',type=Path);parser.add_argument('--native-output',type=Path)
    parser.add_argument('--case',action='append',choices=CASES)
    args=parser.parse_args();binary=args.binary.resolve()
    api=json.loads(subprocess.check_output([str(binary),'--api'],encoding='utf-8'))
    if not (api['modules']['streaming'] and api['modules']['devtools']):
        print('reload file mutation checks require streaming and stdio debugging');return
    def cases(root):
        records=[]
        for name in args.case or CASES:
            records.append(run(binary,root/name,name,args.native_output is not None));print(name+': passed',flush=True)
        return records
    if args.native_output:
        output=args.native_output.resolve();output.mkdir(parents=True,exist_ok=False)
        records=cases(output)
        (output/'manifest.json').write_text(json.dumps({'engine_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'cases':records},indent=2)+'\n')
    else:
        with tempfile.TemporaryDirectory(prefix='shiny-image-reload-') as directory:cases(Path(directory))


if __name__=='__main__':main()
