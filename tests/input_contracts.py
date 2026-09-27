"""Input metadata matches real snapshot fields, argument validation and UI phases."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

binary=Path(sys.argv[1]).resolve()
api=json.loads(subprocess.check_output([str(binary),'--api'],encoding='utf-8'))
contracts={row['name'].rsplit('.',1)[1]:row['contract'] for row in api['functions'] if row['name'].startswith('sc.input.')}
assert len(contracts)==18 and all(contracts.values())
for name,contract in contracts.items():
    assert contract['phases']==(['load','init','update','ui_update'] if name=='focus_text' else ['load','init','update','draw','ui_update'])
axis=contracts['gamepad_axis']['parameters']
assert axis[1]['default']==.2 and axis[1]['minimum']==0 and axis[1]['maximum']==1 and axis[1]['exclusive_maximum']
assert axis[2]['default'] is None and axis[2]['minimum']==1 and axis[2]['maximum']==4
assert contracts['clipboard']['mutation']==dict(parameter='text',when='non_nil',phases=['load','init','update','ui_update'])
assert [r['name'] for r in contracts['mouse']['returns']]==['x','y','inside']
assert [r['name'] for r in contracts['wheel']['returns']]==['x','y']
assert [r['name'] for r in contracts['text']['returns']]==['committed','composition','cursor','selection_start','selection_end','segments','segments_truncated']
assert api['mouse_buttons']==['left','right','middle','side','extra']

with tempfile.TemporaryDirectory(prefix='shiny-input-contract-') as directory:
    root=Path(directory)
    (root/'main.lua').write_text('''local input=sc.input
local function invalid_calls()
    for _,call in ipairs({
        function() input.key_down(1) end,function() input.key_pressed('a',1) end,
        function() input.key_released('a'..string.char(0)) end,
        function() input.mouse_down('left'..string.char(0)..'suffix') end,
        function() input.mouse_pressed('left',1) end,function() input.mouse_released(1) end,
        function() input.mouse(1) end,function() input.wheel(1) end,function() input.snapshot(1) end,
        function() input.text(1) end,function() input.gamepad_connected('1') end,
        function() input.gamepad_connected(1,2) end,function() input.gamepad_down('south',0) end,
        function() input.gamepad_pressed('south',5) end,function() input.gamepad_released('south',1.5) end,
        function() input.gamepad_axis('left_x','0.2') end,function() input.gamepad_axis('left_x',1) end,
        function() input.gamepad_axis('left_x',0/0) end,function() input.gamepad_axis('left_x',0,1,2) end,
        function() input.focus_text(false,0) end,function() input.focus_text(true) end,
        function() input.focus_text(0) end,function() input.focus_text('0',0) end,
        function() input.focus_text(0,math.huge) end,function() input.focus_text(0,0,0) end,
        function() input.clipboard(3) end,function() input.clipboard('x','y') end,
        function() input.clipboard(string.char(0)) end,function() input.clipboard(string.char(255)) end,
        function() input.clipboard(string.rep('x',4096)) end,
        function() input.boundaries('x','y') end,function() input.boundaries(3) end,
        function() input.boundaries(string.char(255)) end,function() input.boundaries(string.rep('x',65537)) end
    }) do assert(not pcall(call)) end
end
invalid_calls()
return {init=function()
    invalid_calls()
    assert(#input.boundaries('')==1 and input.boundaries('')[1]==1)
    local b=input.boundaries('é中');assert(#b==3 and b[1]==1 and b[2]==4 and b[3]==7)
    assert(input.gamepad_axis('left_x',.999)==0)
    input.focus_text(-1000000,1000000);input.focus_text(false)
end,update=function()
    local data=input.snapshot()
    if sc.tick()==0 then
        assert(input.key_down('a') and input.key_pressed('a'))
        assert(input.mouse_down('left') and input.mouse_pressed('left'))
        assert(input.gamepad_down('south') and not input.gamepad_connected(1))
        assert(input.gamepad_connected(3) and input.gamepad_pressed('west',3))
        assert(math.abs(input.gamepad_axis('left_x')-.5)<1e-6)
        assert(math.abs(input.gamepad_axis('left_y',0,3)+.8)<1e-6)
        local x,y,inside=input.mouse();assert(x==12 and y==24 and not inside)
        local wx,wy=input.wheel();assert(wx==1 and wy==-2)
        local text,preedit,cursor,first,finish=input.text()
        assert(text=='中' and preedit=='月光' and cursor==4 and first==1 and finish==4)
        assert(input.clipboard()=='粘贴' and input.clipboard(nil)=='粘贴')
        sc.debug.watch('snapshot',data)
        data.pads[3].axes.left_y=99;assert(input.snapshot().pads[3].axes.left_y<0)
    else assert(input.key_released('a') and input.gamepad_released('west',3) and input.mouse_released('left')) end
end,ui_update=function()
    input.focus_text(12,24);input.focus_text(false)
    assert(select('#',input.clipboard('复制'))==0)
end,draw=function()
    assert(type(input.snapshot())=='table' and type(input.clipboard(nil))=='string')
    assert(not pcall(input.clipboard,'write') and not pcall(input.focus_text,false))
end}
''',encoding='utf-8')
    idle=dict(connected=False)
    rows=[dict(version=3),dict(frame=0,keys=['a'],gamepad=dict(connected=True,buttons=['south'],axes=dict(left_x=.6)),
        pads=[idle,idle,dict(connected=True,buttons=['west'],axes=dict(left_y=-.8)),idle],
        mouse=dict(x=12,y=24,inside=False,buttons=['left'],wheel_x=1,wheel_y=-2),
        text='中',composition='月光',composition_edit=dict(cursor=4,start=1,finish=4),
        composition_segments=[dict(start=1,finish=4,kind='target_converted'),dict(start=4,finish=7,kind='converted')],clipboard='粘贴'),
        dict(frame=1,keys=[],gamepad=idle,pads=[idle]*4)]
    replay=root/'input.jsonl';replay.write_text('\n'.join(json.dumps(row,ensure_ascii=False) for row in rows)+'\n',encoding='utf-8')
    result=subprocess.run([str(binary),str(root),'--headless','--frames','2','--replay',str(replay)],capture_output=True,text=True,encoding='utf-8',timeout=20)
    assert result.returncode==0,result.stderr
    snapshot=json.loads(result.stdout)['watches']['snapshot']

def fields(name,value):
    expected={item['name']:item for item in api['types'][name]['fields']}
    assert set(expected)==set(value),(name,set(expected)^set(value))
    assert all(item['readonly'] and item['required'] for item in expected.values())
fields('ScInputSnapshot',snapshot)
for segment in snapshot['composition_segments']:fields('ScInputCompositionSegment',segment)
fields('ScInputMouse',snapshot['mouse']);fields('ScInputCompositionEdit',snapshot['composition_edit'])
fields('ScInputSelectedGamepad',snapshot['gamepad']);fields('ScInputAxes',snapshot['gamepad']['axes'])
assert len(snapshot['pads'])==4
for pad in snapshot['pads']:fields('ScInputPad',pad);fields('ScInputAxes',pad['axes'])
print('Input contracts: all 18 functions, nested snapshots, strict arguments, phases and recorded devices passed')
