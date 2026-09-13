"""Explicit desktop smoke test; messages target only the window of our child PID.

Run manually on Windows with a graphical binary. No global keyboard injection,
foreground-window changes, or messages to other application windows.
"""
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time

if sys.platform != "win32":
    raise SystemExit("This explicit native smoke test requires Windows.")

user = ctypes.WinDLL("user32", use_last_error=True)
callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
user.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
user.IsWindowVisible.argtypes = [wintypes.HWND]
user.SendMessageW.argtypes = [wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM]
user.SendMessageW.restype = ctypes.c_ssize_t
user.PostMessageW.argtypes = [wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM]
user.MapVirtualKeyW.argtypes = [wintypes.UINT,wintypes.UINT]

def window(pid):
    found=[]
    @callback_type
    def visit(hwnd, _):
        owner=wintypes.DWORD()
        user.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
        if owner.value==pid and user.IsWindowVisible(hwnd): found.append(hwnd)
        return True
    user.EnumWindows(visit,0)
    return found[0] if found else None

def key(hwnd, code, down):
    scan=user.MapVirtualKeyW(code,0)
    flags=1 | (scan<<16) | (0 if down else (3<<30))
    if not user.PostMessageW(hwnd,0x100 if down else 0x101,code,flags):
        raise ctypes.WinError(ctypes.get_last_error())

def tap(hwnd,code):
    key(hwnd,code,True); time.sleep(.08); key(hwnd,code,False); time.sleep(.08)

source='''local keys={'escape','p','o','f1','f2','f3','f5','f12','a'}
return {title='ShinyCore isolated native input test', width=320,height=180,
 init=function() sc.state.set('inits',(sc.state.get('inits') or 0)+1) end,
 update=function()
   sc.state.set('updates',(sc.state.get('updates') or 0)+1)
   if sc.gamepad_connected() then sc.state.set('pad_seen',true) end
   for _,k in ipairs(keys) do
     if sc.key_pressed(k) then sc.state.set(k,(sc.state.get(k) or 0)+1) end
     if sc.key_released(k) then sc.state.set(k..'_released',(sc.state.get(k..'_released') or 0)+1) end
   end
 end}
'''

def run(binary,root,debug=False,replay=False):
    args=[str(binary),str(root),'--mute','--frames','90' if debug else '600']
    if debug: args.append('--debug-keys')
    if replay: args.extend(['--replay',str(root/'native-isolation.jsonl')])
    before_screens=set(binary.parent.glob('screenshot*.png'))
    process=subprocess.Popen(args,cwd=root,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8')
    try:
        deadline=time.monotonic()+5; hwnd=None
        while time.monotonic()<deadline and process.poll() is None:
            hwnd=window(process.pid)
            if hwnd: break
            time.sleep(.02)
        assert hwnd, 'test window unavailable'
        time.sleep(.15)
        # Exercise focus callbacks on this test window only, without stealing focus.
        user.SendMessageW(hwnd,0x7,0,0)  # WM_SETFOCUS
        if debug:
            tap(hwnd,0x74) # F5: transactional reload
            tap(hwnd,0x50) # P: pause
            time.sleep(1.7)
            assert process.poll() is None, 'P did not pause fixed updates'
            tap(hwnd,0x4F) # O: consume one tick
            time.sleep(1.7)
            assert process.poll() is None, 'O resumed continuously instead of single-stepping'
            tap(hwnd,0x50) # P: resume
            tap(hwnd,0x70); tap(hwnd,0x71); tap(hwnd,0x72)
            key(hwnd,0x1B,True) # Debug Escape closes before a key-up can be sent.
        else:
            for code in (0x1B,0x50,0x4F,0x70,0x71,0x72,0x74,0x7B):
                tap(hwnd,code)
                assert process.poll() is None, 'default host shortcut unexpectedly terminated game'
            key(hwnd,0x41,True); time.sleep(.08)
            for _ in range(5):
                key(hwnd,0x41,False); key(hwnd,0x41,True); time.sleep(.08)
            key(hwnd,0x41,True); time.sleep(.08) # Repeated down is not a fresh press.
            user.SendMessageW(hwnd,0x8,0,0) # WM_KILLFOCUS should synthesize release.
            time.sleep(.08)
            user.SendMessageW(hwnd,0x7,0,0)
            key(hwnd,0x41,False); time.sleep(.08)
            user.PostMessageW(hwnd,0x10,0,0) # WM_CLOSE to our own child window.
        stdout,stderr=process.communicate(timeout=5)
        assert process.returncode==0,stderr
        assert not list(root.glob('screenshot*.png')), 'raylib consumed F12 as a screenshot shortcut'
        assert set(binary.parent.glob('screenshot*.png'))==before_screens, 'unexpected native screenshot'
        result=json.loads(stdout); state=result['state']
        assert result['frames']<600, 'window did not close when requested'
        if debug:
            assert state['inits']==2, state
            assert state.get('o')==1, state
            for name in ('f1','f2','f3'): assert state.get(name)==1,state
        elif replay:
            assert result['input']['keys']==['j'],result['input']
            for name in ('escape','p','o','f1','f2','f3','f5','f12','a'):
                assert name not in state and name+'_released' not in state,state
        else:
            assert state['inits']==1,state
            for name in ('escape','p','o','f1','f2','f3','f5','f12','a'):
                count=6 if name=='a' else 1
                assert state.get(name)==count, state
                assert state.get(name+'_released')==count, state
        return dict(debug=debug,replay=replay,frames=result['frames'],state=state)
    finally:
        if process.poll() is None:
            process.terminate(); process.communicate(timeout=5)

binary=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='shiny-native-input-') as temporary:
    root=Path(temporary); (root/'main.lua').write_text(source,encoding='utf-8')
    (root/'native-isolation.jsonl').write_text('{"version":2}\n{"frame":0,"keys":["j"],"gamepad":{"connected":false}}\n',encoding='utf-8')
    print(json.dumps([run(binary,root),run(binary,root,True),run(binary,root,replay=True)],indent=2))
