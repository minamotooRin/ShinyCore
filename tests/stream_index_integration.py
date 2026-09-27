"""Declared map indexes finish on the content worker before room init."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile


ACTIVE = '''return {width=320,height=180,gravity=0,
init=function() sc.state.set("room","old") end,
update=function() if sc.tick()==0 then sc.scene("next.lua") end end,
draw=function() sc.rect(0,0,320,180,"#DC4050FF",true) end}
'''
NEXT = '''return {width=320,height=180,gravity=0,
init=function()
    sc.stream.open("index.json")
    assert(sc.stream.metadata().tilewidth==8)
    sc.state.set("room","new")
end,
draw=function() sc.rect(0,0,320,180,"#40C090FF",true) end}
'''


def project(root, valid):
    root.mkdir(parents=True)
    (root/'project.lua').write_text('return {id="index-test",rooms={"main.lua","next.lua"},'
        'modules={"streaming"},stream_indexes={["next.lua"]="index.json"}}',encoding='utf-8')
    (root/'main.lua').write_text(ACTIVE,encoding='utf-8')
    (root/'next.lua').write_text(NEXT,encoding='utf-8')
    (root/'index.json').write_text(json.dumps({'format':3 if valid else 2,'chunk_size':32,
        'tilewidth':8,'tileheight':8,'layers':[],'chunks':[]}),encoding='utf-8')


def run(binary, root, native=False):
    command=[str(binary),str(root),'--frames','2','--mute']
    if native: command+=['--capture-hidden','--capture',str(root/'frame.png')]
    else: command+=['--headless']
    return subprocess.run(command,capture_output=True,text=True,encoding='utf-8',timeout=15)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary',type=Path)
    parser.add_argument('--native-output',type=Path)
    args=parser.parse_args()
    if args.native_output:
        root=args.native_output.resolve()
        root.mkdir(parents=True,exist_ok=False)
        context=None
    else:
        context=tempfile.TemporaryDirectory(prefix='shiny-index-')
        root=Path(context.name)
    try:
        for valid,expected in [(True,'new'),(False,'old')]:
            case=root/expected
            project(case,valid)
            result=run(args.binary.resolve(),case,native=bool(args.native_output))
            if valid or args.native_output:
                assert result.returncode==0,result.stderr
                snapshot=json.loads(result.stdout)
                assert snapshot['state']['room']==expected,snapshot
                assert snapshot['scene']==('next.lua' if valid else 'main.lua'),snapshot
                if args.native_output:
                    from PIL import Image
                    with Image.open(case/'frame.png') as image:
                        pixel=image.convert('RGB').getpixel((image.width//10,image.height//2))
                    assert pixel==((64,192,144) if valid else (220,64,80)),pixel
            else:
                assert result.returncode!=0 and 'unsupported stream index format' in result.stderr,result.stderr
            if not valid and args.native_output:
                assert 'unsupported stream index format' in result.stderr,result.stderr
        print('stream index: worker preparation, room commit and failure retention passed')
    finally:
        if context: context.cleanup()


if __name__=='__main__': main()
