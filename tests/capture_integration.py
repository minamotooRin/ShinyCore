"""Opt-in native capture: exact final frame, hidden-window visibility and CLI constraints."""
import ctypes
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from PIL import Image

binary=Path(sys.argv[1]).resolve()
for args in (['--capture-hidden'],['--capture-hidden','--capture','out.png','--frames','0'],
             ['--capture-hidden','--capture','out.png','--frames','2','--headless']):
    result=subprocess.run([str(binary),*args],capture_output=True,text=True,encoding='utf-8',timeout=10)
    assert result.returncode and 'arguments' in result.stderr
with tempfile.TemporaryDirectory(prefix='shiny-capture-') as directory:
    root=Path(directory)
    (root/'main.lua').write_text('''return {width=64,height=64,gravity=0,draw=function()
    sc.rect(0,0,64,64,sc.tick()==1 and "#FF0000FF" or "#00FF00FF",true)
end}''',encoding='utf-8')
    with (root/'out.json').open('w',encoding='utf-8') as stdout,(root/'err.log').open('w',encoding='utf-8') as stderr:
        process=subprocess.Popen([str(binary),str(root),'--frames','2','--capture-hidden','--mute',
            '--capture',str(root/'final.png'),'--profile',str(root/'profile.jsonl')],stdout=stdout,stderr=stderr)
        seen=set();visible=[]
        if os.name=='nt':
            user32=ctypes.windll.user32
            def visit(hwnd,unused):
                pid=ctypes.c_ulong()
                user32.GetWindowThreadProcessId(ctypes.c_void_p(hwnd),ctypes.byref(pid))
                if pid.value==process.pid:
                    seen.add(hwnd)
                    if user32.IsWindowVisible(ctypes.c_void_p(hwnd)):visible.append(hwnd)
                return True
            callback=ctypes.WINFUNCTYPE(ctypes.c_bool,ctypes.c_void_p,ctypes.c_void_p)(visit)
        deadline=time.monotonic()+20
        try:
            while process.poll() is None:
                if os.name=='nt':user32.EnumWindows(callback,0)
                if time.monotonic()>deadline:raise TimeoutError('native capture timed out')
                time.sleep(.01)
        finally:
            if process.poll() is None:process.kill()
            process.wait()
    assert process.returncode==0,(root/'err.log').read_text(encoding='utf-8')
    assert not visible,visible
    if os.name=='nt':assert seen,'no native window observed; visibility check inconclusive'
    snapshot=json.loads((root/'out.json').read_text(encoding='utf-8'));assert snapshot['frames']==2
    with Image.open(root/'final.png') as image:
        assert image.getpixel((image.width//2,image.height//2))[:3]==(0,255,0),'capture contains the previous frame'
    frames=[row for row in map(json.loads,(root/'profile.jsonl').read_text(encoding='utf-8').splitlines()) if row['type']=='frame']
    assert frames[-1]['diagnostic_ms']>0,'capture must be attributed to diagnostic cost'
print('Native final-frame capture, hidden visibility and argument constraints passed')
