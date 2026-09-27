"""Native UI inspection must not run author callbacks or retain abandoned trees."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

binary=Path(sys.argv[1]).resolve()
api=json.loads(subprocess.check_output([str(binary),'--api'],encoding='utf-8'))
if not api['modules']['devtools']:
    assert not any(item['name']=='sc.debug.ui' for item in api['functions'])
    with tempfile.TemporaryDirectory(prefix='shiny-ui-no-debug-') as directory:
        project=Path(directory)
        shutil.copytree(Path(__file__).resolve().parents[1]/'lua'/'shiny',project/'lib'/'shiny')
        (project/'main.lua').write_text('''local UI=require("shiny.ui")
return {init=function()
assert(sc.debug.ui==nil)
local ui=UI.new{id="menu",kind="column",children={{id="play",kind="button",text="Play"}}}
UI.layout(ui,320,180)
end}''',encoding='utf-8')
        result=subprocess.run([str(binary),str(project),'--headless','--frames','1'],capture_output=True,text=True,timeout=10)
        assert result.returncode==0,result.stderr
    print('UI debugger: disabled build omits registry API; ordinary UI still works');raise SystemExit(0)
contract=next(item['contract'] for item in api['functions'] if item['name']=='sc.debug.ui')
assert contract['module']=='devtools' and contract['phases']==['load','init','update','ui_update']
assert not contract['parameters'][1]['required']
with tempfile.TemporaryDirectory(prefix='shiny-debug-ui-') as directory:
    project=Path(directory)
    shutil.copytree(Path(__file__).resolve().parents[1]/'lua'/'shiny',project/'lib'/'shiny')
    shutil.copyfile(Path(__file__).resolve().parents[1]/'examples'/'particle_materials'/'assets'/'keeper.png',project/'keeper.png')
    (project/'project.lua').write_text('return {resources={keeper={type="image",path="keeper.png"}}}',encoding='utf-8')
    (project/'main.lua').write_text('''local UI=require("shiny.ui")
local ui,button,n= nil,nil,0
local function forbidden() error("inspection executed a metamethod") end
local mt={__index=forbidden,__pairs=forbidden,__len=forbidden,__tostring=forbidden}
return {init=function()
    button={id="confirm",kind="button",text="Confirm",value="ready",tooltip="Confirm inventory",tooltip_delay=0}
    ui=UI.new{id="inventory",kind="column",children={button,
        {id="hidden",kind="column",visible=false,disabled=true,children={{id="nested",kind="label"}}},
        {id="items",kind="list",h=24,row_height=24,
         items={{id="herb",label="Herb"},{id="key",label="Key"},{id="map",label="Map"}},value=1}}}
    UI.layout(ui,320,180); ui.focus="confirm"
    UI.update(ui,1/60,320,180)
    assert(ui.tooltip and ui.tooltip.owner=="confirm")
    sc.debug.watch("tooltip",ui.tooltip.rect)
    -- Capture can be observed between callbacks; inspection must read it raw.
    ui.scroll_drag={id="items",grab=0}
    setmetatable(ui.scroll_drag,mt);setmetatable(ui.tooltip.rect,mt);setmetatable(ui.tooltip,mt)
    for _,node in ipairs(ui.authored_nodes) do setmetatable(node,mt) end
    setmetatable(ui.nodes.items.items[1],mt);setmetatable(ui.nodes.items.items,mt)
    setmetatable(ui,mt)
end,update=function()
    n=n+1
    if n==1 then rawset(button,"visible",false); rawset(ui,"focus",nil); rawset(ui,"layout_pending",true)
    elseif n==2 then sc.debug.ui("inventory")
    elseif n==3 then
        sc.debug.ui("inventory",ui)
        ui=nil;button=nil;collectgarbage("collect")
    end
end}
''',encoding='utf-8')
    process=subprocess.Popen([str(binary),str(project),'--headless','--debug-stdio','--frames','10'],
        stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8')
    sequence=0
    def receive():
        line=process.stdout.readline()
        assert line,process.stderr.read()
        return json.loads(line)
    def command(name,**fields):
        global sequence
        sequence+=1
        process.stdin.write(json.dumps({'id':sequence,'command':name,**fields})+'\n');process.stdin.flush()
        result=receive();assert result.get('id')==sequence,result
        return result
    def step():
        assert command('step_out')['ok']
        assert receive()['event']=='breakpoint'
    try:
        assert 'ui' in receive()['commands']
        update_line=next(i for i,line in enumerate((project/'main.lua').read_text(encoding='utf-8').splitlines(),1) if 'n=n+1' in line)
        assert command('breakpoints',file='main.lua',lines=[update_line])['ok']
        assert command('continue')['ok'];assert receive()['event']=='breakpoint'
        assert command('breakpoints',file='main.lua',lines=[])['ok']
        metrics=command('metrics')['result']
        assert metrics['entities']==0 and metrics['lua_bytes']>0 and metrics['entity_capacity']==4096
        resources=command('resources')['result'];assert resources['total']==1
        resource=resources['items'][0]
        assert resource['name']=='keeper' and resource['type']=='image' and resource['path']=='keeper.png'
        assert resource['width']>0 and resource['height']>0
        trees=command('ui')['result'];assert trees['items']==[{'name':'inventory','focus':'confirm','total':5}]
        page=command('ui',tree='inventory',offset=1,limit=1)['result']
        row=page['items'][0]
        assert page['total']==5 and row['id']=='confirm' and row['focused'] and row['value']=='ready'
        assert row['rect']['w']>0 and row['visible'] and row['enabled'] and not row['layout_pending']
        assert row['tooltip_visible'] and not row['tooltip_truncated'] and not row['scroll_capture']
        tooltip=command('watches',path=['tooltip'])['result']['items']
        assert row['tooltip_rect']=={item['key']:item['value'] for item in tooltip}
        hidden=command('ui',tree='inventory',offset=3)['result']['items'][0]
        assert hidden['id']=='nested' and not hidden['visible'] and not hidden['enabled'] and 'rect' not in hidden
        item=command('ui',tree='inventory',offset=4,limit=1)['result']['items'][0]
        assert item['id']=='items' and item['selected_item_id']=='herb' and item['item_count']==3
        assert item['scroll_max']==48 and item['scroll_capture'] and not item['tooltip_visible']
        assert not command('ui',tree='missing')['ok']
        assert not command('ui',limit=129)['ok']
        step()
        row=command('ui',tree='inventory',offset=1,limit=1)['result']['items'][0]
        assert not row['visible'] and not row['focused'] and not row['tooltip_visible'] and 'tooltip_rect' not in row
        dirty=command('ui',tree='inventory',offset=4,limit=1)['result']['items'][0]
        assert dirty['layout_pending'] and 'rect' not in dirty
        step();assert command('ui')['result']['total']==0
        step();assert command('ui')['result']['total']==0
        assert not command('ui',tree='inventory')['ok']
        assert command('quit')['ok'];assert receive()['event']=='terminated'
        assert process.wait(timeout=10)==0,process.stderr.read()
    finally:
        if process.poll() is None: process.kill();process.wait(timeout=10)
        process.stdin.close();process.stdout.close();process.stderr.close()
print('UI debugger: native paging, focus, inherited visibility, raw reads and weak lifetime passed')
