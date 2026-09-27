"""Device API, deterministic replay and scene continuity through the actual host."""
import json
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY = Path(sys.argv[1]).resolve()
ROOT = Path(sys.argv[2]).resolve()
del sys.argv[1:3] # Preserve unittest selectors for focused verification.

def event(frame=0, keys=None, connected=False, buttons=None, axes=None, **edges):
    return dict(frame=frame, keys=keys or [], gamepad=dict(
        connected=connected, buttons=buttons or [], axes=axes or {}), **edges)

class InputTests(unittest.TestCase):
    def test_rebinding_mouse_pad_and_discard_preserve_live_controls(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('''local Input=require('shiny.input')
local Rebind=require('shiny.rebind')
local actions=Input.new({jump={{key='space'},{button='south'}},menu={{key='escape'}}},'player')
local panel,closed
return {init=function() panel=Rebind.new(actions,function() closed=true end);panel.ui.focus='bindings.key' end,
update=function(dt)
    Input.update(actions,'ui');Rebind.update(panel,dt);Input.update(actions)
    local t=sc.tick()
    if t==2 then assert(panel.draft.jump[2].mouse=='left' and not panel.capture) end
    if t==3 then panel.ui.focus='bindings.pad' end
    if t==6 then
        assert(panel.draft.jump[1].mouse=='left' and panel.draft.jump[2].button=='east' and panel.draft.jump[2].slot==1)
        assert(not panel.capture and not closed)
        panel.selected='menu';panel.ui.nodes['bindings.clear'].on_click()
        assert(panel.draft.menu[1].key=='escape')
        panel.draft.menu={};panel.ui.nodes['bindings.apply'].on_click()
        assert(sc.settings.get().bindings.player==nil and actions.bindings.menu[1].key=='escape')
    end
    if t==8 then
        assert(closed and actions.bindings.jump[1].key=='space' and actions.bindings.jump[2].button=='south')
    end
end}''')
        events=[event(0,keys=['enter']),event(1),event(2,mouse={'x':30,'y':30,'inside':True,'buttons':['left']}),
                event(3),event(4,keys=['enter'],connected=True),event(5,connected=True),
                event(6,connected=True,buttons=['east']),event(7,connected=True),event(8,keys=['escape'])]
        self.invoke('--frames',9,'--replay',self.replay(events))

    def test_rebinding_draft_capture_conflict_apply_and_restore(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('return {id="rebind-test"}','project.lua')
        self.source('''local Input=require('shiny.input')
local Rebind=require('shiny.rebind')
local defaults={jump={{key='space'},{button='south'},{axis='right_trigger'}},menu={{key='escape'}},move={{key='d'}}}
local actions=Input.new(defaults,'player');local panel,closed
local function has(sources,field,value) for _,source in ipairs(sources) do if source[field]==value then return true end end end
return {init=function() panel=Rebind.new(actions,function() closed=true end) end,update=function(dt)
    Input.update(actions,'ui');if not closed then Rebind.update(panel,dt) end;Input.update(actions)
    local t=sc.tick()
    if t==3 then assert(panel.capture and not panel.capture.armed) end
    if t==5 then assert(panel.capture and panel.ui.nodes['bindings.notice'].text:find('move')) end
    if t==7 then
        assert(not panel.capture and has(panel.draft.jump,'key','j') and has(actions.bindings.jump,'key','space'))
        assert(has(panel.draft.jump,'button','south') and has(panel.draft.jump,'axis','right_trigger'))
    end
    if t==17 then
        assert(has(actions.bindings.jump,'key','j') and has(actions.defaults.jump,'key','space'))
        assert(panel.ui.nodes['bindings.notice'].text=='Controls saved.')
        assert(not Input.pressed(actions,'jump'))
    end
    if t==19 then assert(closed and not Input.pressed(actions,'menu')) end
end}''')
        keys={0:['tab'],2:['enter'],3:['enter'],5:['d'],7:['j'],9:['tab'],11:['tab'],13:['tab'],15:['tab'],17:['enter'],19:['escape']}
        self.invoke('--frames',20,'--replay',self.replay([event(i,keys=keys.get(i,[])) for i in range(20)]),
                    '--save-dir',self.path/'data')
        self.source('''local Input=require('shiny.input')
local actions=Input.new({},'player')
return {update=function()
    Input.update(actions);assert(Input.pressed(actions,'jump') and Input.down(actions,'jump'))
end}''')
        self.invoke('--frames',1,'--replay',self.replay([event(keys=['j'])]),'--save-dir',self.path/'data')

    def test_rebinding_axis_neutral_gate_cancel_defaults_and_failed_commit(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('''local Input=require('shiny.input')
local Rebind=require('shiny.rebind')
local actions=Input.new({jump={{key='space'},{axis='right_trigger'}},menu={{key='escape'}},
    move={{axis='left_x'}}},'player');local panel
return {init=function()
    panel=Rebind.new(actions,function() end);panel.ui.focus='bindings.axis'
end,update=function(dt)
    Input.update(actions,'ui');Rebind.update(panel,dt);Input.update(actions)
    local t=sc.tick()
    if t==1 then assert(panel.capture and not panel.capture.armed) end
    if t==3 then assert(panel.capture and not panel.capture.armed) end
    if t==5 then
        assert(not panel.capture and panel.draft.jump[2].axis=='left_x' and panel.draft.jump[2].direction==-1)
        assert(panel.draft.jump[2].slot==1 and panel.draft.jump[2].deadzone==.2)
        local apply=sc.settings.apply;sc.settings.apply=function() return nil,'injected write failure' end
        panel.ui.nodes['bindings.apply'].on_click();sc.settings.apply=apply
        assert(actions.bindings.jump[2].axis=='right_trigger' and panel.draft.jump[2].axis=='left_x')
        assert(panel.ui.nodes['bindings.notice'].text=='injected write failure')
        panel.ui.nodes['bindings.defaults'].on_click()
        assert(panel.draft.jump[2].axis=='right_trigger')
    end
    if t==6 then panel.ui.nodes['bindings.pad'].on_click() end
    if t==8 then assert(not panel.capture and panel.draft.jump[2].axis=='right_trigger') end
end}''')
        events=[event(0,keys=['enter'],connected=True,axes={'left_x':.8}),
                event(1,connected=True,axes={'left_x':.8}),event(2,connected=True),
                event(3,connected=True,axes={'left_x':.7}),event(4,connected=True),
                event(5,connected=True,axes={'left_x':-.7}),event(6,connected=True),
                event(7,connected=True),event(8,connected=True,buttons=['back'])]
        self.invoke('--frames',9,'--replay',self.replay(events))

    def test_analog_actions_edges_ui_ownership_and_persistence(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('return {id="axis-profile"}', 'project.lua')
        self.source('''local Input=require('shiny.input')
local actions=Input.new({left={{axis='left_x',direction=-1}},
    right={{key='d'},{axis='left_x',slot=1}},trigger={{axis='right_trigger'}}},'axes')
return {update=function()
    local t=sc.tick()
    Input.update(actions,'ui')
    if t==5 then Input.consume_sources(actions,{axes={left_x=true}}) end
    Input.update(actions)
    local expected=({[0]=0,.5,1,-.5,0,0,0,0,.5})[t]
    assert(math.abs(Input.axis(actions,'left','right')-expected)<.00001,'axis at '..t)
    if t==1 then assert(Input.pressed(actions,'right') and math.abs(Input.value(actions,'trigger')-.5)<.00001) end
    if t==2 then assert(not Input.pressed(actions,'right')) end
    if t==3 then assert(Input.released(actions,'right') and Input.pressed(actions,'left')) end
    if t==4 then assert(Input.released(actions,'left')) end
    if t>=5 and t<=7 then assert(not Input.down(actions,'right') and not Input.released(actions,'right')) end
    if t==8 then
        assert(Input.pressed(actions,'right'))
        local original=actions.bindings.right
        for _,bad in ipairs({{axis='bogus'},{axis='left_x',direction=0},{axis='left_x',deadzone=1},
            {axis='left_x',deadzone=0/0},{axis='left_x',slot=5},{axis='left_x',key='d'},
            {key='unknown'},{mouse='unknown'},{button='unknown'},{key='a',mouse='left'},
            {key='a',slot=1},{axis='left_x',extra=true}}) do
            assert(not pcall(Input.bind,actions,'right',{bad}) and actions.bindings.right==original)
            assert(not pcall(Input.new,{right={bad}}))
        end
        assert(not pcall(Input.bind,actions,'',{{key='a'}}))
        local authored={{key='a'}}
        Input.bind(actions,'right',authored);authored[1].key='unknown'
        assert(actions.bindings.right[1].key=='a')
        Input.bind(actions,'right',original)
        assert(Input.save(actions))
    end
end}''')
        events=[event(0,connected=True,axes={'left_x':.1}),
                event(1,connected=True,axes={'left_x':.6,'right_trigger':.6}),
                event(2,keys=['d'],connected=True,axes={'left_x':.8}),
                event(3,connected=True,axes={'left_x':-.6}),event(4),
                event(5,connected=True,axes={'left_x':.6}),event(6,connected=True,axes={'left_x':.6}),
                event(7,connected=True),event(8,connected=True,axes={'left_x':.6})]
        path=self.replay(events)
        self.invoke('--replay',path,'--frames',9,'--save-dir',self.path/'saves')
        self.source('''local Input=require('shiny.input')
local actions=Input.new({},'axes')
return {init=function()
    assert(actions.bindings.left[1].direction==-1 and actions.bindings.right[2].slot==1)
end,update=function()
    Input.update(actions)
    assert(math.abs(Input.value(actions,'right')-.5)<.00001)
end}''')
        self.invoke('--replay',self.replay([event(0,connected=True,axes={'left_x':.6})]),
                    '--frames',1,'--save-dir',self.path/'saves')

    def test_shell_gamepad_pause_settings_and_held_action_isolation(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('''local Input=require('shiny.input')
local Shell=require('shiny.shell')
local actions=Input.new{right={{button='dpad_right'}},jump={{button='south'}},
    menu={{button='start'}},cancel={{button='east'}}}
local shell,t
return {init=function() shell=Shell.new('Title','Controls'); t=0 end,
update=function(dt)
    Input.update(actions,'ui')
    local playing=Shell.update(shell,dt,actions)
    Input.update(actions)
    if t==0 or t==4 or t==5 or (t>=10 and t<=14) then assert(not playing) end
    if t==1 or t==6 or t==7 then
        assert(playing and not Input.down(actions,'right') and not Input.pressed(actions,'jump'),
            'tick '..t..' mode '..shell.mode..' right '..tostring(Input.down(actions,'right'))..' jump '..tostring(Input.pressed(actions,'jump')))
    end
    if t==3 then assert(playing and Input.down(actions,'right')) end
    if t==9 then assert(playing and Input.pressed(actions,'jump')) end
    if t==13 then assert(shell.settings~=nil) end
    if t==14 then assert(shell.settings==nil and not Input.pressed(actions,'cancel')) end
    if t==15 then assert(playing and not Input.pressed(actions,'jump')) end
    if t==17 then assert(playing and Input.pressed(actions,'jump')) end
    t=t+1
end}''')
        buttons=[['south'],['south'],[],['dpad_right'],
                 ['start','south','dpad_right'],['south','dpad_right'],
                 ['start','south','dpad_right'],['south','dpad_right'],[],['south'],
                 ['start'],[],['dpad_down'],['south'],['east'],['start','south'],[],['south']]
        path=self.replay([event(i,connected=True,buttons=value) for i,value in enumerate(buttons)])
        self.invoke('--replay',path,'--frames',len(buttons))

    def test_ui_incremental_geometry_and_text_invalidation(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source("""local UI=require('shiny.ui')
local ui=UI.new{id='root',kind='row',padding=0,gap=0,children={
    {id='left',w=120,kind='column',padding=0,gap=4,children={
        {id='a',kind='label',text='A',h=20},{id='b',kind='button',text='B',h=20}}},
    {id='right',w=120,kind='overlay',padding=0,children={
        {id='c',kind='label',text='C',w=40,h=20,anchor='center'}}}}}
local function layout() UI.layout(ui,240,120) end
return {init=function()
    layout()
    local a,b,c=ui.nodes.a,ui.nodes.b,ui.nodes.c
    local ar,br,cr=a.rect,b.rect,c.rect
    local text=c.text_layout
    UI.set(ui,'a',{text='Changed',color='#FF0000'});layout()
    assert(a.rect==ar and b.rect==br and c.rect==cr and c.text_layout==text)
    assert(a.text_layout.text=='Changed')
    UI.set(ui,'a',{h=40});layout()
    assert(b.rect.y==44 and c.rect==cr and c.text_layout==text)
    ar,br=a.rect,b.rect
    UI.set(ui,'a',{h=40});layout()
    assert(a.rect==ar and b.rect==br and c.rect==cr)
    UI.set(ui,'a',{visible=false});layout()
    assert(b.rect.y==0 and #ui.order==5 and c.rect==cr)
    UI.set(ui,'a',{h=30});layout()
    UI.set(ui,'a',{visible=true});layout()
    assert(a.rect.h==30 and b.rect.y==34 and #ui.order==6 and c.rect==cr)
    UI.layout(ui,300,120)
    assert(c.rect==cr) -- Fixed panel receives the same slot when the viewport grows.
    ui.theme.padding=2;ui.dirty=true;UI.layout(ui,300,120)
    assert(c.rect~=cr) -- Explicit full invalidation supports direct theme changes.
end,draw=function() UI.draw(ui) end}
""")
        self.invoke('--frames',1)

    def test_ui_incremental_matches_full_layout_after_nested_changes(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source("""local UI=require('shiny.ui')
local ui=UI.new{id='root',kind='row',padding=0,gap=0,children={
    {id='scroll',kind='scroll',w=100,h=70,padding=2,gap=3,children={
        {id='grid',kind='grid',h=100,padding=1,gap=2,columns=2,children={
            {id='a',h=30},{id='b',h=50},{id='c',h=20},{id='d',h=15}}},
        {id='tail',h=80}}},
    {id='list',kind='list',w=100,h=70,row_height=20,items={'A','B','C','D','E'}}}}
local function equal_full()
    UI.layout(ui,240,120)
    local snapshot={}
    for i,node in ipairs(ui.order) do
        local r=node.rect;snapshot[i]={id=node.id,x=r.x,y=r.y,w=r.w,h=r.h,
            content=node.content_h,maximum=node.scroll_max,scroll=ui.scroll[node.id]}
    end
    ui.dirty=true;UI.layout(ui,240,120)
    assert(#snapshot==#ui.order)
    for i,node in ipairs(ui.order) do
        local old,r=snapshot[i],node.rect
        assert(old.id==node.id and old.x==r.x and old.y==r.y and old.w==r.w and old.h==r.h,node.id)
        assert(old.content==node.content_h and old.maximum==node.scroll_max and old.scroll==ui.scroll[node.id],node.id)
    end
end
return {init=function()
    equal_full();UI.scroll_to(ui,'scroll',999);equal_full()
    UI.set(ui,'tail',{visible=false});equal_full()
    UI.set(ui,'grid',{h=40,columns=3});equal_full()
    UI.set(ui,'b',{min_h=60});equal_full()
    UI.set(ui,'grid',{padding=3,gap=5,anchor='bottom_right',w=80});equal_full()
    UI.scroll_to(ui,'list',999);UI.set(ui,'list',{items={'Only'}});equal_full()
    assert(ui.scroll.list==0 and ui.nodes.list.scroll_max==0)
    UI.set(ui,'root',{visible=false});equal_full()
    UI.set(ui,'tail',{visible=true,h=50});UI.set(ui,'root',{visible=true});equal_full()
end,draw=function() UI.draw(ui) end}
""")
        self.invoke('--frames',1)

    def test_ui_nine_anchors_resize_and_atomic_layout_patch(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source("""local UI=require('shiny.ui')
local anchors={'top_left','top','top_right','left','center','right','bottom_left','bottom','bottom_right'}
local nodes={};for _,anchor in ipairs(anchors) do nodes[#nodes+1]={id=anchor,anchor=anchor,w=20,h=10} end
local ui=UI.new{id='root',kind='overlay',padding=4,children=nodes}
local function verify(w,h)
    UI.layout(ui,w,h)
    for i,node in ipairs(nodes) do
        local col,row=(i-1)%3,math.floor((i-1)/3)
        assert(node.rect.x==4+(w-28)*col/2 and node.rect.y==4+(h-18)*row/2,node.id)
    end
end
return {init=function()
    verify(108,58);verify(208,108)
    local center=ui.nodes.center
    for _,patch in ipairs({{anchor='typo'},{w=-1},{x=math.huge},{min_w=50,max_w=20},{columns=0},{gap=-1}}) do
        patch.text='must not commit'
        assert(not pcall(UI.set,ui,'center',patch))
        assert(center.text==nil and not ui.dirty)
    end
    UI.set(ui,'center',{x=3,y=-2,min_w=22,max_w=30})
    UI.layout(ui,208,108)
    assert(center.rect.w==22 and center.rect.x==96 and center.rect.y==47)
    local row=UI.new{id='row',kind='row',padding=0,children={{id='a',w=20,h=10,anchor='bottom'}}}
    UI.layout(row,100,50);assert(row.nodes.a.rect.y==40)
    sc.debug.watch('anchors',true)
end}
""")
        self.invoke('--frames',1)

    def test_ui_skin_copy_theme_fallback_and_atomic_patch(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        shutil.copyfile(ROOT/'examples/lantern/assets/keeper.png',self.path/'panel.png')
        self.source('return {resources={panel={type="image",path="panel.png"}}}','project.lua')
        self.source("""local UI=require('shiny.ui')
local skin={resource='panel',source_w=8,source_h=8,slice={left=2,right=2,top=2,bottom=2}}
local theme={};for key,value in pairs(UI.theme) do theme[key]=value end
theme.skins={button=skin}
local ui=UI.new({id='root',children={{id='a',kind='button',skin=skin,w=60,h=30},
    {id='b',kind='button',skin=false,w=60,h=30},{id='c',kind='button',w=60,h=30}}},theme)
return {init=function()
    UI.layout(ui,384,216)
    assert(ui.nodes.a.skin_resource=='panel' and ui.nodes.c.skin_resource=='panel')
    assert(ui.nodes.b.skin_resource==nil and ui.nodes.a.skin_options.screen)
    skin.slice.left=3
    assert(ui.nodes.a.skin_options.slice.left==2 and ui.nodes.c.skin_options.slice.left==2)
    for _,bad in ipairs({{resource='panel',slice={left=-1}},{resource='panel',screen=false},
        {resource='panel',source_w=8},{resource='panel',slice={other=1}},
        {resource='panel',color='bad'},setmetatable({resource='panel'},{})}) do
        assert(not pcall(UI.set,ui,'a',{skin=bad,text='bad'}))
        assert(ui.nodes.a.text==nil and ui.nodes.a.skin_options.slice.left==2 and not ui.dirty)
    end
    UI.set(ui,'a',{skin=false});assert(ui.nodes.a.skin_resource==nil)
    UI.set(ui,'b',{skin=skin});assert(ui.nodes.b.skin_options.slice.left==3)
    UI.layout(ui,384,216);sc.debug.watch('skins',true)
end,draw=function() UI.draw(ui) end}
""")
        self.invoke('--frames',1)

    def test_host_ui_actions_precede_gameplay(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('''local Input=require('shiny.input')
local UI=require('shiny.ui')
local actions=Input.new{jump={{key='enter'},{button='south'}},move={{key='a'}}}
local clicks,updates,jumps=0,0,0
local ui
local root={id='root',modal=true,children={{id='resume',kind='button',text='Resume'}}}
root.children[1].on_click=function()
    clicks=clicks+1;UI.set(ui,'root',{modal=false,visible=false});sc.app.pause(false)
end
ui=UI.new(root);ui.focus='resume'
return {init=function() UI.layout(ui,384,216);sc.app.pause(true) end,
ui_update=function(dt)
    Input.update(actions,'ui')
    UI.update(ui,dt,384,216,actions)
    updates=updates+1
end,update=function()
    local t=sc.tick()
    assert(updates==t+1,'UI must run before this fixed update')
    Input.update(actions)
    if t<3 or t==4 or t==5 then
        assert(not Input.down(actions,'jump') and not Input.pressed(actions,'jump') and not Input.released(actions,'jump'))
        assert(not Input.down(actions,'move') and not Input.released(actions,'move'))
    end
    if t==0 then assert(clicks==1 and not sc.app.paused() and sc.input.key_pressed('enter')) end
    if t==3 then
        assert(Input.pressed(actions,'jump') and Input.pressed(actions,'move'))
        UI.set(ui,'root',{visible=true,modal=true});ui.focus='resume';sc.app.pause(true)
    end
    if t==4 then assert(clicks==2 and not sc.app.paused()) end
    if t==6 then assert(Input.pressed(actions,'jump') and Input.released(actions,'jump')) end
    if Input.pressed(actions,'jump') then jumps=jumps+1 end
    sc.debug.watch('ui_actions',{clicks=clicks,jumps=jumps,ui_calls=updates})
end,draw=function() UI.draw(ui) end}''')
        rows=[event(keys=['enter','a']),event(1,keys=['enter','a']),event(2),
              event(3,keys=['enter','a']),event(4,connected=True,buttons=['south']),
              event(5,connected=True),event(6,key_pressed=['enter'],key_released=['enter'])]
        result=self.invoke('--replay',self.replay(rows),'--frames',7)
        self.assertEqual(result['watches']['ui_actions'],dict(clicks=2,jumps=2,ui_calls=7))

    def test_ui_action_bridge_across_host_and_fixed_rates(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('''local Input=require('shiny.input')
return {init=function()
    local native=sc.input
    local held,pressed,released={},{},{}
    sc.input={key_down=function(k) return held[k] end,key_pressed=function(k) return pressed[k] end,
        key_released=function(k) return released[k] end}
    local actions=Input.new{jump={{key='enter'}},move={{key='a'}}}
    local function invisible()
        assert(not Input.down(actions,'jump') and not Input.pressed(actions,'jump') and not Input.released(actions,'jump'))
    end
    -- A press/release handled between fixed updates must survive a later idle UI frame.
    pressed={enter=true};released={enter=true}
    Input.update(actions,'ui');Input.consume_sources(actions,{keys={enter=true}})
    pressed={};released={};Input.update(actions,'ui')
    pressed={enter=true};released={enter=true};Input.update(actions);invisible()
    pressed={};released={};Input.update(actions);invisible()
    -- The latest UI capture covers every catch-up tick, then remains latched until release.
    held={enter=true,a=true};pressed={enter=true,a=true}
    Input.update(actions,'ui');Input.consume(actions,'jump')
    assert(Input.pressed(actions,'move'))
    Input.update(actions);invisible();assert(Input.pressed(actions,'move'))
    pressed={};Input.update(actions);invisible();assert(not Input.pressed(actions,'move'))
    Input.update(actions,'ui');Input.update(actions);invisible()
    held={};released={enter=true};Input.update(actions,'ui');Input.update(actions);invisible()
    held={enter=true};pressed={enter=true};released={}
    Input.update(actions,'ui');Input.update(actions);assert(Input.pressed(actions,'jump'))
    assert(not pcall(Input.update,actions,'invalid'))
    assert(not pcall(Input.update,actions,false))
    assert(Input.pressed(actions,'jump'))
    sc.input=native
end}''')
        self.invoke('--frames',0)

    def test_text_selection_collapse_and_document_navigation(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('''local UI=require('shiny.ui')
local Edit=require('shiny.textedit')
local value='á中B\\n第二行'
local node={id='entry',kind='input',multiline=true,w=220,h=60,value=value}
local ui=UI.new{id='root',children={node}};ui.focus='entry'
return {init=function()
    local e=Edit.new(value)
    e.anchor=4;e.cursor=7; Edit.move(e,-1,false); assert(e.cursor==4 and e.anchor==4)
    e.anchor=7;e.cursor=4; Edit.move(e,1,false); assert(e.cursor==7 and e.anchor==7)
    e.anchor=4;e.cursor=7; Edit.move(e,-1,true); assert(e.cursor==4 and e.anchor==4)
    Edit.erase(e,-1); assert(e.value=='中B\\n第二行'); Edit.undo(e); assert(e.value==value)
    UI.layout(ui,384,216)
end,update=function(dt)
    UI.update(ui,dt,384,216)
    local e=node.editor;local t=sc.tick()
    if t==0 then assert(e.cursor==1 and e.anchor==1) end
    if t==1 then assert(e.cursor==#value+1 and e.anchor==1) end
    if t==2 then assert(e.cursor==1 and e.anchor==1) end
    if t==3 then assert(e.cursor==#value+1 and e.anchor==#value+1) end
    if t==4 then assert(e.cursor==1 and e.anchor==#value+1) end
    if t==5 then assert(e.cursor==#value+1 and e.anchor==#value+1) end
    if t==6 then assert(e.cursor==value:find('第',1,true)) end
    if t==7 then assert(e.cursor==#value+1) end
    assert(e.value==value and #e.undo==0)
end,draw=function() UI.draw(ui) end}''')
        rows=[event(keys=['left_control','home']),event(1,keys=['left_control','left_shift','end']),
              event(2,keys=['left']),event(3,keys=['left_control','end']),
              event(4,keys=['left_control','left_shift','home']),event(5,keys=['right']),
              event(6,keys=['home']),event(7,keys=['end'])]
        self.invoke('--replay',self.replay(rows),'--frames',8)

    def test_multiline_preferred_column_and_page_selection(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('''local UI=require('shiny.ui')
local Edit=require('shiny.textedit')
local value='AAAAAA\\nB\\nAAAAAA\\nAAAAAA\\nAAAAAA'
local node={id='entry',kind='input',multiline=true,w=220,h=36,value=value,font_size=14}
local ui=UI.new{id='root',padding=0,children={node}};ui.focus='entry'
node.editor=Edit.new(value);node.editor.cursor=6;node.editor.anchor=6
local expected={9,9,15,14,21,28,14,1,30,1,30,1,30,24,30}
return {init=function() UI.layout(ui,384,216) end,update=function(dt)
    UI.update(ui,dt,384,216)
    local e=node.editor;local t=sc.tick()
    assert(e.cursor==expected[t+1],t..': '..e.cursor)
    if t==5 then assert(e.anchor==21 and node.text_scroll_y>0) end
    if t==6 then assert(e.anchor==21) end
    assert(e.value==value and #e.undo==0)
end,draw=function() UI.draw(ui) end}''')
        rows=[event(keys=['down']),event(1),event(2,keys=['down']),event(3,keys=['left']),
              event(4,keys=['down']),event(5,keys=['left_shift','page_down']),event(6,keys=['left_shift','page_up']),
              event(7,keys=['left_control','home']),event(8,keys=['left_control','left_shift','end']),
              event(9,keys=['left']),event(10,keys=['left_control','end']),
              event(11,keys=['left_control','left_shift','home']),event(12,keys=['right']),
              event(13,keys=['home']),event(14,keys=['end'])]
        self.invoke('--replay',self.replay(rows),'--frames',15)

    def test_text_shift_redo_and_ime_document_key_isolation(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('''local UI=require('shiny.ui')
local node={id='entry',kind='input',multiline=true,w=220,h=60,value='月光'}
local ui=UI.new{id='root',children={node}};ui.focus='entry'
return {init=function() UI.layout(ui,384,216) end,update=function(dt)
    UI.update(ui,dt,384,216)
    local e=node.editor;local t=sc.tick()
    assert(node.value==((t==1 or t==2) and '月光' or '月光草'))
    if t>=3 then assert(e.cursor==#node.value+1 and e.anchor==e.cursor) end
end}''')
        rows=[event(text='草'),event(1,keys=['left_control','z']),event(2),
              event(3,keys=['left_control','left_shift','z']),
              event(4,keys=['left_control','home'],composition='明'),
              event(5,keys=['left_control','left_shift','home']),event(6)]
        self.invoke('--replay',self.replay(rows),'--frames',7)

    def test_ui_held_navigation_and_confirmation(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('''local UI=require('shiny.ui')
local Input=require('shiny.input')
local items={};for i=1,20 do items[i]=tostring(i) end
local hits=0
local ui=UI.new{id='root',kind='overlay',padding=0,children={
    {id='items',kind='list',w=100,h=80,items=items,value=1},
    {id='volume',kind='slider',y=90,w=100,h=24,value=0,step=.1},
    {id='button',kind='button',y=130,w=100,h=24,on_click=function() hits=hits+1 end}}}
local actions=Input.new{move={{key='down'},{button='dpad_down'}}}
ui.focus='items'
return {init=function() UI.layout(ui,384,216) end,update=function(dt)
    local tick=sc.tick()
    if tick==34 then ui.focus='volume' elseif tick==66 then ui.focus='button' end
    Input.update(actions); UI.update(ui,dt,384,216,actions)
    assert(not Input.down(actions,'move'))
    if tick<=30 then
        local expected=2+(tick<24 and 0 or 1+math.floor((tick-24)/3))
        assert(ui.nodes.items.value==expected,tick..': '..ui.nodes.items.value)
    end
    if tick==31 then assert(ui.nodes.items.value==5) end
    if tick==32 then assert(ui.nodes.items.value==6) end
    if tick>=34 and tick<=65 then
        local held=tick-34
        local expected=.1*(1+(held<24 and 0 or 1+math.floor((held-24)/3)))
        assert(math.abs(ui.nodes.volume.value-expected)<1e-6)
    end
    if tick>=66 then assert(hits==1) end
end,draw=function() UI.draw(ui) end}''')
        rows=[event(i,connected=True,buttons=['dpad_down']) for i in range(31)]
        rows.extend([event(31),event(32,keys=['down']),event(33)])
        rows.extend(event(i,keys=['right']) for i in range(34,66))
        rows.extend(event(i,keys=['enter']) for i in range(66,95))
        self.invoke('--replay',self.replay(rows),'--frames',95)

    def test_ui_held_grapheme_editing_and_ime_reset(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('''local UI=require('shiny.ui')
local node={id='entry',kind='input',w=250,value='甲乙丙丁戊áZ'}
local ui=UI.new{id='root',children={node}};ui.focus='entry'
local steps={.1,0,2,0,.05,.1,.1,2,.4,.1,.1,2,.1,.1}
local expected={'甲乙丙丁戊á','甲乙丙丁戊á','甲乙丙丁戊','甲乙丙丁戊','甲乙丙丁',
    '甲乙丙丁','甲乙丙丁','甲乙丙丁','甲乙丙','甲乙丙','甲乙丙丁','甲乙丙丁','甲乙丙丁','甲乙丙'}
return {init=function() UI.layout(ui,384,216) end,update=function()
    local tick=sc.tick(); UI.update(ui,steps[tick+1],384,216)
    assert(node.value==expected[tick+1],tick..': '..node.value)
    if tick==2 then assert(not sc.input.key_pressed('backspace')) end
    for _,bad in ipairs({-1,math.huge,0/0,'1'}) do assert(not pcall(UI.update,ui,bad,384,216)) end
end,draw=function() UI.draw(ui) end}''')
        rows=[event(i,keys=['backspace'],composition='输入' if i==5 else '') for i in range(9)]
        rows.extend([event(9),event(10,keys=['left_control','z']),event(11,keys=['left_control','z']),
                     event(12),event(13,keys=['left_control','y'])])
        self.invoke('--replay',self.replay(rows),'--frames',14)

    def test_ui_held_navigation_resets_at_modal_boundary(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('''local UI=require('shiny.ui')
local ui=UI.new{id='root',kind='overlay',padding=0,children={
    {id='items',kind='list',w=100,h=100,items={'one','two','three','four'},value=1},
    {id='dialog',kind='column',x=120,w=100,h=100,modal=true,visible=false,children={
        {id='first',kind='button',h=24},{id='second',kind='button',h=24}}}}}
ui.focus='items'
return {update=function(dt)
    local tick=sc.tick()
    if tick==24 then UI.set(ui,'dialog',{visible=true}) end
    if tick==49 then UI.set(ui,'dialog',{visible=false}) end
    UI.update(ui,dt,384,216)
    if tick<24 then assert(ui.focus=='items' and ui.nodes.items.value==2) end
    if tick>=24 and tick<48 then assert(ui.focus=='first' and ui.nodes.items.value==2) end
    if tick==48 then assert(ui.focus=='second') end
    if tick>=49 and tick<73 then assert(ui.focus=='items' and ui.nodes.items.value==2) end
    if tick==73 then assert(ui.nodes.items.value==3) end
end}''')
        self.invoke('--replay',self.replay([event(i,keys=['down']) for i in range(74)]),'--frames',74)
    def test_ime_cursor_target_and_snapshot_roundtrip(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('''local UI=require('shiny.ui')
local Edit=require('shiny.textedit')
local node={id='entry',kind='input',value='AB',w=180,h=32}
local ui=UI.new{id='root',children={node}};ui.focus='entry'
node.editor=Edit.new('AB');node.editor.cursor=2;node.editor.anchor=2
return {init=function() UI.layout(ui,384,216) end,update=function(dt)
    assert(select('#',sc.input.text())==7 and not pcall(sc.input.text,1))
    local _,preedit,cursor,first,last=sc.input.text()
    local snapshot=sc.input.snapshot()
    assert(snapshot.composition_edit.cursor==cursor and snapshot.composition_edit.start==first and snapshot.composition_edit.finish==last)
    sc.debug.watch('input',snapshot)
    UI.update(ui,dt,384,216)
    local cache=node.text_layout; local targets={}
    for _,p in ipairs(cache.positions) do if p.targeted then targets[#targets+1]=p.byte end end
    assert(node.value=='AB' and #node.editor.undo==0 and #targets==1)
    if sc.tick()==0 then
        assert(cursor==2 and cache.preedit_cursor==3 and targets[1]==2)
    elseif sc.tick()==1 then
        assert(cursor==1 and cache.preedit_cursor==2 and targets[1]==5)
    elseif sc.tick()==2 then
        assert(cursor==7 and cache.preedit_cursor==8 and targets[1]==5)
    else
        assert(cursor==6 and cache.text=='A中文B' and cache.preedit_cursor==5 and targets[1]==5)
    end
end,draw=function() UI.draw(ui) end}''')
        path=self.replay([event(composition='e\u0301中',composition_edit=dict(cursor=2,start=2,finish=4)),
            event(1,composition='e\u0301中',composition_edit=dict(cursor=1,start=4,finish=7)),
            event(2,composition='e\u0301中',composition_edit=dict(cursor=7,start=4,finish=7)),
            event(3,composition='中\r\n文',composition_edit=dict(cursor=6,start=6,finish=9))])
        trace=self.path/'trace.jsonl'
        first=self.invoke('--frames',4,'--replay',path,'--trace',trace)
        normalized=[]
        for line in trace.read_text(encoding='utf-8').splitlines():
            snapshot=json.loads(line);row=snapshot['input'];row['frame']=snapshot['frames']-1;normalized.append(row)
        repeated=self.invoke('--frames',4,'--replay',self.replay(normalized))
        self.assertEqual(first['hash'],repeated['hash'])

    def test_ime_replay_rejects_invalid_positions(self):
        self.source('return {}')
        for edit in [dict(cursor=2,start=1,finish=4),dict(cursor=0,start=1,finish=4),
                     dict(cursor=1,start=4,finish=1),dict(cursor=1,start=1,finish=5),
                     dict(cursor=1.5,start=1,finish=4),dict(cursor=1,start=1),
                     dict(cursor=1,start=1,finish=4,extra=True)]:
            path=self.replay([event(composition='中',composition_edit=edit)])
            result=subprocess.run([str(BINARY),str(self.path),'--headless','--frames','1','--replay',str(path)],capture_output=True,text=True,timeout=10)
            self.assertNotEqual(result.returncode,0,edit)
            self.assertIn('replay',result.stderr)

    def test_list_stable_selection_after_filter_and_sort(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('''local UI=require('shiny.ui')
local a,b,c={id='apple',label='Apple'},{id='boots',label='Boots'},{id='coin',label='Coin'}
local changes,activated=0,nil
local node={id='bag',kind='list',items={a,b,c},value=2,h=20,row_height=20,
    on_change=function() changes=changes+1 end}
node.on_activate=function(value) activated=node.items[value].id end
local ui=UI.new{id='root',padding=0,gap=0,children={node}}
return {init=function() UI.layout(ui,160,60);ui.focus='bag' end,update=function(dt)
    local tick=sc.tick()
    if tick==0 then UI.set(ui,'bag',{items={c,a,b}}) end
    if tick==2 then UI.set(ui,'bag',{items={a,b}}) end
    if tick==3 then UI.set(ui,'bag',{items={a}}) end
    if tick==4 then UI.set(ui,'bag',{items={c,b,a},value=2}) end
    if tick==5 then UI.set(ui,'bag',{value=false}) end
    UI.update(ui,dt,160,60)
    if tick==0 then assert(node.value==3 and ui.scroll.bag==40 and changes==1) end
    if tick==1 then assert(activated=='boots') end
    if tick==2 then assert(node.value==2 and ui.scroll.bag==20 and changes==2) end
    if tick==3 then assert(node.value==nil and ui.scroll.bag==0 and changes==3) end
    if tick==4 then
        assert(node.value==2 and ui.scroll.bag==20)
        local rows=UI.inspect(ui);assert(rows[2].selected_item_id=='boots')
    end
    if tick==5 then assert(node.value==nil and changes==3) end
    if tick==6 then assert(node.value==1) end
    if tick==7 then assert(node.value==1) end
    if tick==8 then assert(node.value==2) end
end,draw=function() UI.draw(ui) end}''')
        self.invoke('--frames',9,'--replay',self.replay([
            event(),event(1,keys=['enter']),event(2),event(3),event(4),event(5),
            event(6,connected=True,buttons=['dpad_down']),event(7,connected=True),
            event(8,connected=True,buttons=['dpad_down'])]))

    def test_list_rejects_invalid_batches_before_mutation(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('''local UI=require('shiny.ui')
local items={{id='one',label='One'},{id='two',label='Two'}}
local node={id='bag',kind='list',items=items,value=2,row_height=20}
local ui=UI.new{id='root',children={node}}
local function forbidden() error('must not invoke list metatable') end
local bad={{[2]='hole'},{'ok',extra='bad'},{{id='x'},{id='x'}},{{id='x'},'text'},
    {{id=1}},{true},{{label=1}},{{label=false}},{string.rep('a',4096)},
    {'a'..string.char(0)..'b'},setmetatable({}, {__len=forbidden,__pairs=forbidden}),
    {setmetatable({}, {__index=forbidden})}}
return {init=function()
    UI.layout(ui,160,80)
    for _,value in ipairs(bad) do
        assert(not pcall(UI.set,ui,'bag',{items=value,color='#FF0000',row_height=30}))
        assert(node.items==items and node.value==2 and node.row_height==20 and node.color==nil and not ui.dirty)
        assert(not pcall(UI.new,{id='bad',kind='list',items=value}))
    end
    for _,patch in ipairs({{row_height=0},{row_height=math.huge},{value=0},{value=1.5},{value=math.huge}}) do
        assert(not pcall(UI.set,ui,'bag',patch));assert(node.value==2 and node.row_height==20)
    end
    UI.set(ui,'bag',{items={{id='two',label='Two'}},value=false})
    assert(node.value==nil)
end}''')
        self.invoke('--frames',1)

    def test_space_confirmation_controls_and_callback_focus_clear(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('''local UI=require('shiny.ui')
local Input=require('shiny.input')
local clicks,tabs,activations=0,0,0
local ui
ui=UI.new{id='root',padding=0,gap=0,children={
    {id='button',kind='button',h=20,on_click=function() clicks=clicks+1; ui.focus=nil end},
    {id='check',kind='checkbox',h=20},
    {id='list',kind='list',items={'One'},value=1,h=20,on_activate=function() activations=activations+1 end},
    {id='input',kind='input',value='',h=20},
    {id='tab',kind='tab',h=20,on_click=function() tabs=tabs+1 end}}}
local actions=Input.new{jump={{key='space'}}}
return {init=function()
    UI.layout(ui,200,140); ui.focus='button'
    assert(ui.nodes.check.value==false)
    assert(not pcall(UI.set,ui,'check',{value=0}))
end,update=function(dt)
    local tick=sc.tick()
    if tick==1 then ui.focus='check' end
    if tick==4 then ui.focus='list' end
    if tick==6 then ui.focus='input' end
    if tick==8 then ui.focus='tab' end
    if tick==10 then UI.set(ui,'check',{disabled=true}); ui.focus='check' end
    Input.update(actions); UI.update(ui,dt,200,140,actions)
    if tick==0 then assert(clicks==1 and ui.focus==nil and not Input.pressed(actions,'jump')) end
    if tick>=2 then assert(ui.nodes.check.value==true) end
    if tick>=5 then assert(activations==1) end
    if tick>=7 then assert(ui.nodes.input.value==' ') end
    if tick>=9 then assert(tabs==1) end
    if tick==2 or tick==3 or tick==5 or tick==7 or tick==9 then assert(not Input.down(actions,'jump')) end
    if tick==11 then assert(Input.pressed(actions,'jump') and clicks==1) end
end,draw=function() UI.draw(ui) end}''')
        self.invoke('--replay',self.replay([event(i,keys=['space'] if i in (0,2,3,5,7,9,11) else [],
                                                 text=' ' if i==7 else '') for i in range(12)]),'--frames',12)

    def test_slider_keyboard_endpoints_and_atomic_validation(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('''local UI=require('shiny.ui')
local Input=require('shiny.input')
local changes=0
local ui=UI.new{id='root',children={{id='s',kind='slider',value=.5,step=.1,page_step=.25,
    on_change=function() changes=changes+1 end}}}
local actions=Input.new{move={{key='left'},{key='right'},{key='home'},{key='end'},
    {key='page_up'},{key='page_down'},{button='dpad_left'}}}
local expected={.6,.6,.85,.85,.6,.6,0,0,1,1,.9,.9,.9,.9,1,1,1}
return {init=function()
    UI.layout(ui,200,100); ui.focus='s'
    for _,patch in ipairs({{value=-1},{value=2},{value=0/0},{value='1'},
                          {step=0},{step=2},{page_step=math.huge},{page_step=-1}}) do
        assert(not pcall(UI.set,ui,'s',patch))
        assert(ui.nodes.s.value==.5 and ui.nodes.s.step==.1 and ui.nodes.s.page_step==.25)
    end
end,update=function(dt)
    Input.update(actions); UI.update(ui,dt,200,100,actions)
    assert(math.abs(ui.nodes.s.value-expected[sc.tick()+1])<1e-9)
    assert(not Input.down(actions,'move'))
    if sc.tick()==16 then assert(changes==7) end
end,draw=function() UI.draw(ui) end}''')
        keys={0:['right'],1:['right'],2:['page_up'],4:['page_down'],6:['home'],8:['end'],
              12:['left','right'],14:['right'],16:['right']}
        path=self.replay([event(i,keys=keys.get(i,[]),connected=True,buttons=['dpad_left'] if i==10 else []) for i in range(17)])
        self.invoke('--replay',path,'--frames',17)

    def test_slider_fast_tap_release_position_and_zero_width(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('''local UI=require('shiny.ui')
local ui=UI.new{id='root',padding=0,children={{id='s',kind='slider',w=100,h=20}}}
local expected={.75,.5,1,.25,.25,.25}
return {init=function() UI.layout(ui,200,100); assert(ui.nodes.s.value==0) end,update=function(dt)
    if sc.tick()==4 then UI.set(ui,'s',{disabled=true}) end
    if sc.tick()==5 then UI.set(ui,'s',{disabled=false,w=0}); ui.focus='s' end
    UI.update(ui,dt,200,100)
    assert(ui.nodes.s.value==expected[sc.tick()+1])
end,draw=function() UI.draw(ui) end}''')
        path=self.replay([
            event(mouse=dict(x=75,y=10,inside=True,buttons=[],pressed=['left'],released=['left'])),
            event(1,mouse=dict(x=50,y=10,inside=True,buttons=['left'])),
            event(2,mouse=dict(x=200,y=10,inside=False,buttons=['left'])),
            event(3,mouse=dict(x=25,y=10,inside=True,buttons=[])),
            event(4,mouse=dict(x=75,y=10,inside=True,buttons=['left'])),
            event(5,mouse=dict(x=50,y=10,inside=True,buttons=['left']))])
        self.invoke('--replay',path,'--frames',6)

    def test_tooltip_delay_bounds_escape_and_draw_only(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('return {limits={draws=64}}','project.lua')
        self.source('''local UI=require('shiny.ui')
local Input=require('shiny.input')
local ui=UI.new{id='root',kind='overlay',padding=0,children={
    {id='help',kind='button',x=165,y=72,w=30,h=24,tooltip=string.rep('中文说明 ',40),tooltip_delay=.04,tooltip_width=180}}}
local actions=Input.new{quit={{key='escape'}}}
return {init=function() UI.layout(ui,200,100) end,update=function(dt)
    if sc.tick()==3 then UI.set(ui,'help',{text='?'}) end -- Relayout must not forget a visible tip's Escape.
    Input.update(actions); UI.update(ui,dt,200,100,actions)
    local expected=sc.tick()==2 or sc.tick()==8
    assert((ui.tooltip~=nil)==expected)
    local inspected=UI.inspect(ui)
    assert(inspected[2].tooltip_visible==expected)
    if ui.tooltip then
        local r=inspected[2].tooltip_rect
        assert(inspected[2].tooltip_truncated)
        assert(ui.tooltip.owner=='help' and r.x>=4 and r.y>=4 and r.x+r.w<=196 and r.y+r.h<=96)
        assert(ui.nodes.help.rect.x==165 and ui.nodes.help.rect.y==72)
    end
    if sc.tick()==3 then assert(not Input.pressed(actions,'quit')) end
    assert(#ui.events==0) -- A passive tip never activates its owner.
end,draw=function()
    local popup,elapsed=ui.tooltip,ui.hint.elapsed
    UI.draw(ui)
    assert(ui.tooltip==popup and ui.hint.elapsed==elapsed)
end}''')
        path=self.replay([event(i,keys=['escape'] if i==3 else [],
                               mouse=dict(x=20 if i==5 else 185,y=85,inside=True,buttons=[])) for i in range(9)])
        self.invoke('--replay',path,'--frames',9)

    def test_tooltip_input_source_and_modal_isolation(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('''local UI=require('shiny.ui')
local ui
ui=UI.new{id='root',kind='overlay',padding=0,children={
    {id='one',kind='button',w=60,h=30,tooltip='One',tooltip_delay=0,
        on_click=function() UI.set(ui,'dialog',{visible=true}) end},
    {id='two',kind='button',x=70,w=60,h=30,tooltip='Two',tooltip_delay=0},
    {id='dialog',kind='overlay',x=50,y=40,w=100,h=60,padding=0,visible=false,modal=true,children={
        {id='inside',kind='button',h=20,tooltip='Modal hint',tooltip_delay=0}}}}}
local expected={'one','one','two','one','one','inside',false,'one',false}
return {init=function() UI.layout(ui,200,120) end,update=function(dt)
    local tick=sc.tick()
    if tick==6 then UI.set(ui,'inside',{disabled=true}) end
    if tick==7 then UI.set(ui,'dialog',{visible=false}) end
    if tick==8 then UI.set(ui,'one',{tooltip=false}) end
    UI.update(ui,dt,200,120)
    assert((ui.tooltip and ui.tooltip.owner or false)==expected[tick+1],tostring(tick))
end,draw=function() UI.draw(ui) end}''')
        events=[event(mouse=dict(x=30,y=10,inside=True,buttons=[]))]
        for i in range(1,9):
            events.append(event(i,keys=['tab'] if i==1 else ['enter'] if i==5 else [],connected=True,
                                buttons=['right_shoulder'] if i==2 else ['left_shoulder'] if i==4 else [],
                                mouse=dict(x=31 if i>=3 else 30,y=10,inside=True,buttons=[])))
        self.invoke('--replay',self.replay(events),'--frames',9)

    def test_tooltip_clipping_overlap_and_invalid_options(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('''local UI=require('shiny.ui')
local ui=UI.new{id='root',kind='overlay',padding=0,children={
    {id='panel',kind='scroll',w=100,h=60,padding=0,gap=0,children={
        {id='upper',kind='label',h=20,tooltip='Upper',tooltip_delay=0},
        {id='lower',kind='label',y=100,h=20,tooltip='Lower outside parent clip',tooltip_delay=0}}},
    {id='cover',kind='button',w=100,h=20}}}
return {init=function()
    UI.layout(ui,200,120)
    for _,patch in ipairs({{tooltip={}},{tooltip=string.rep('x',4097)},{tooltip_delay=-1},
                          {tooltip_width=0/0},{tooltip_width=23},{tooltip_delay=math.huge}}) do
        assert(not pcall(UI.set,ui,'upper',patch))
        assert(ui.nodes.upper.tooltip=='Upper' and not ui.dirty)
    end
end,update=function(dt)
    local tick=sc.tick()
    if tick==1 then UI.set(ui,'cover',{visible=false}) end
    if tick==2 then UI.set(ui,'upper',{disabled=true}) end
    if tick==3 then UI.set(ui,'panel',{visible=false}) end
    if tick==4 then UI.set(ui,'panel',{visible=true}); UI.scroll_to(ui,'panel',999) end
    UI.update(ui,dt,200,120)
    local expected=tick==1 and 'upper' or tick==4 and 'lower' or false
    assert((ui.tooltip and ui.tooltip.owner or false)==expected)
    if tick==4 then assert(ui.tooltip.rect.w>100) end -- Popup is outside the narrow ancestor clip.
end,draw=function() UI.draw(ui) end}''')
        path=self.replay([event(i,mouse=dict(x=10,y=50 if i==4 else 10,inside=True,buttons=[])) for i in range(5)])
        self.invoke('--replay',path,'--frames',5)

    def test_scroll_layout_extent_clamping_and_grid_rows(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('''local UI=require('shiny.ui')
local ui=UI.new{id='root',kind='scroll',padding=4,gap=3,children={
    {id='a',kind='button',h=20},{id='b',kind='button',h=30,min_h=40},{id='c',kind='button',h=20}}}
return {init=function()
    UI.layout(ui,120,80)
    assert(ui.root.content_h==94 and ui.root.scroll_max==14)
    assert(UI.scroll_to(ui,'root',999)==14 and ui.nodes.c.rect.y+ui.nodes.c.rect.h==76)
    for i=1,4 do
        UI.set(ui,'a',{text=tostring(i)}); UI.layout(ui,120,80)
        assert(ui.root.content_h==94 and ui.root.scroll_max==14 and ui.scroll.root==14)
    end
    for _,bad in ipairs({0/0,math.huge,'1'}) do assert(not pcall(UI.scroll_to,ui,'root',bad)) end
    assert(not pcall(UI.scroll_to,ui,'a',0) and ui.scroll.root==14)
    UI.set(ui,'b',{visible=false}); UI.layout(ui,120,80)
    assert(ui.root.content_h==51 and ui.root.scroll_max==0 and ui.scroll.root==0)
    assert(ui.nodes.a.rect.y==4 and ui.nodes.c.rect.y==27)
    local grid=UI.new{id='grid',kind='grid',columns=2,padding=4,gap=2,children={
        {id='one',h=20},{id='two',h=40},{id='three',h=30},{id='four',h=10}}}
    UI.layout(grid,200,200)
    assert(grid.nodes.three.rect.y==46 and grid.nodes.four.rect.y==46 and grid.root.content_h==80)
end,draw=function() UI.draw(ui) end}''')
        self.invoke('--frames',1)

    def test_scroll_focus_reveal_and_nested_wheel(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('''local UI=require('shiny.ui')
local items={}; for i=1,5 do items[i]={id='b'..i,kind='button',h=20} end
local ui=UI.new{id='root',kind='scroll',padding=0,gap=0,children=items}
return {init=function() UI.layout(ui,100,60) end,update=function(dt)
    UI.update(ui,dt,100,60)
    local tick=sc.tick()
    if tick%2==0 then
        local index=tick//2+1
        assert(ui.focus=='b'..index and ui.scroll.root==math.max(0,index*20-60))
        local r=ui.nodes[ui.focus].rect; assert(r.y>=0 and r.y+r.h<=60)
    end
end,draw=function() UI.draw(ui) end}''')
        path=self.replay([event(i,keys=['tab'] if i%2==0 else []) for i in range(9)])
        self.invoke('--replay',path,'--frames',9)
        self.source('''local UI=require('shiny.ui')
local ui=UI.new{id='outer',kind='scroll',padding=0,gap=0,children={
    {id='inner',kind='scroll',h=60,padding=0,gap=0,children={{id='body',h=120}}},
    {id='tail',h=100}}}
return {init=function() UI.layout(ui,100,100) end,update=function(dt)
    UI.update(ui,dt,100,100)
    if sc.tick()==0 then assert(ui.scroll.inner==60 and ui.scroll.outer==0) end
    if sc.tick()==1 then assert(ui.scroll.inner==60 and ui.scroll.outer==60) end
end,draw=function() UI.draw(ui) end}''')
        path=self.replay([event(i,mouse=dict(x=10,y=10,inside=True,wheel_y=-10,buttons=[])) for i in range(2)])
        self.invoke('--replay',path,'--frames',2)
        self.source('''local UI=require('shiny.ui')
local items={}; for i=1,4 do items[i]={id='b'..i,kind='button',h=30} end
local ui=UI.new{id='outer',kind='scroll',padding=0,gap=0,children={
    {id='space',h=50},{id='inner',kind='scroll',h=60,padding=0,gap=0,children=items},{id='tail',h=70}}}
return {init=function() UI.layout(ui,100,100); ui.focus='b4' end,update=function(dt)
    UI.update(ui,dt,100,100)
    assert(ui.scroll.inner==60 and ui.scroll.outer==10)
    assert(ui.nodes.b4.rect.y==70 and ui.nodes.b4.rect.y+ui.nodes.b4.rect.h==100)
end,draw=function() UI.draw(ui) end}''')
        self.invoke('--frames',1)

    def test_scrollbar_capture_page_click_and_disabled_release(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('''local UI=require('shiny.ui')
local ui=UI.new{id='root',kind='scroll',padding=0,gap=0,children={
    {id='body',kind='button',h=200,on_click=function() error('scrollbar click leaked into content') end}}}
return {init=function() UI.layout(ui,100,80) end,update=function(dt)
    if sc.tick()==6 then UI.set(ui,'root',{disabled=true}) end
    local consumed=UI.update(ui,dt,100,80)
    if sc.tick()==0 or sc.tick()==5 then assert(ui.scroll_drag and consumed.pointer) end
    if sc.tick()==1 then assert(ui.scroll.root==120 and consumed.pointer) end
    if sc.tick()==2 then assert(ui.scroll.root==105 and consumed.pointer) end
    if sc.tick()==2 or sc.tick()==6 then assert(ui.scroll_drag==nil) end
    if sc.tick()==3 then assert(ui.scroll.root==25 and ui.scroll_drag==nil) end
end,draw=function() UI.draw(ui) end}''')
        events=[(96,8,True,['left']),(150,400,False,['left']),(50,50,True,[]),
                (96,0,True,['left']),(96,0,True,[]),(96,24,True,['left']),(96,70,True,['left'])]
        path=self.replay([event(i,mouse=dict(x=x,y=y,inside=inside,buttons=buttons))
                          for i,(x,y,inside,buttons) in enumerate(events)])
        self.invoke('--replay',path,'--frames',7)
        self.source('''local UI=require('shiny.ui')
local ui
ui=UI.new{id='root',kind='scroll',padding=0,gap=0,children={
    {id='space',h=100},{id='top',kind='button',h=20,on_click=function() UI.scroll_to(ui,'root',0) end}}}
return {init=function() UI.layout(ui,100,60) end,update=function(dt)
    UI.update(ui,dt,100,60)
    assert(ui.focus=='top' and ui.scroll.root==0) -- Explicit callback scrolling wins over focus reveal.
end}''')
        path=self.replay([event(keys=['tab','enter'])])
        self.invoke('--replay',path,'--frames',1)

    def test_ime_preview_selection_commit_cancel_and_undo(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('return {limits={draws=64}}','project.lua')
        self.source('''local UI=require('shiny.ui')
local Edit=require('shiny.textedit')
local changes=0
local node={id='entry',kind='input',value='甲旧乙',w=120,h=32,on_change=function() changes=changes+1 end}
local ui=UI.new{id='root',children={node,{id='other',kind='button'}}}
ui.focus='entry'; node.editor=Edit.new(node.value); node.editor.cursor=4; node.editor.anchor=7
return {init=function() UI.layout(ui,384,216) end,update=function(dt)
    UI.update(ui,dt,384,216)
    local tick=sc.tick(); local cache=node.text_layout; local e=node.editor
    assert(ui.focus=='entry')
    if tick==0 or tick==1 or tick==4 then
        assert(node.value=='甲旧乙' and e.cursor==4 and e.anchor==7 and #e.undo==0)
        assert(cache.text=='甲'..node.composition..'乙' and cache.preedit_first==4)
        local plain,preview='', ''
        for _,run in ipairs(cache.runs) do
            assert(#run.text<=511)
            if run.composition then preview=preview..run.text else plain=plain..run.text end
        end
        assert(plain=='甲乙' and preview==node.composition)
        if tick==1 then assert(node.text_scroll_x>0 and #cache.runs>=4) end
    elseif tick==2 then
        assert(node.value=='甲新乙' and #e.undo==1 and changes==1)
        assert(cache.preedit_first==nil and cache.text==node.value)
    elseif tick==3 or tick==5 then
        assert(node.value=='甲旧乙' and e.cursor==4 and e.anchor==7 and #e.undo==0 and changes==2)
        assert(cache.preedit_first==nil)
    end
end,draw=function() UI.draw(ui) end}''')
        path=self.replay([
            event(composition='拼音',keys=['left','backspace','tab']),
            event(1,composition='中'*180,keys=['right','delete']),
            event(2,text='新',keys=['enter']),event(3,keys=['left_control','z']),
            event(4,composition='候选'),event(5,keys=['backspace'])])
        self.invoke('--replay',path,'--frames',6)

    def test_ime_wrapping_enter_and_focus_isolation(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source(r'''local UI=require('shiny.ui')
local Edit=require('shiny.textedit')
local first={id='first',kind='input',multiline=true,value='A\nB',w=70,h=48}
local second={id='second',kind='input',value='',w=90,h=32}
local ui=UI.new{id='root',children={first,second}}; ui.focus='first'
first.editor=Edit.new(first.value); first.editor.cursor=1; first.editor.anchor=1
return {init=function() UI.layout(ui,384,216) end,update=function(dt)
    local tick=sc.tick()
    if tick==3 then ui.focus='second' end
    if tick==6 then ui.focus='first' end
    UI.update(ui,dt,384,216)
    if tick==0 then
        assert(first.value=='A\nB' and first.text_layout.height>48 and first.text_scroll_y>0)
    elseif tick==1 then assert(first.value=='中A\nB' and #first.editor.undo==1)
    elseif tick>=2 and tick<6 then assert(first.value=='中A\nB') end
    if tick==3 or tick==4 then
        assert(second.value=='' and second.text_layout.preedit_first==nil)
        assert(first.text_layout.text==first.value and first.text_layout.preedit_first==nil)
    end
    if tick==5 then assert(second.value=='XY') end
    if tick==6 or tick==7 then assert(first.value=='中文A\nB') end
    if tick==8 then assert(first.value=='中文\nA\nB') end
    if tick==9 then assert(first.value=='中文\nX\nYA\nB') end
end,draw=function() UI.draw(ui) end}''')
        path=self.replay([
            event(composition='中文'*20),event(1,text='中',keys=['enter']),
            event(2,composition='不转移'),event(3,composition='不转移'),
            event(4,text='不转移'),event(5,keys=['left_control','v'],clipboard='X\r\nY'),
            event(6,text='文',keys=['enter']),event(7),event(8,keys=['enter']),
            event(9,keys=['left_control','v'],clipboard='X\r\nY')])
        self.invoke('--replay',path,'--frames',10)

    def test_ui_inspection_is_bounded_and_detached(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('''local UI=require('shiny.ui')
local long=string.rep('中',100)
local root={id='root',children={
    {id='hidden',visible=false,children={{id='entry',kind='input',value=long}}},
    {id='disabled',kind='scroll',disabled=true,children={{id='button',kind='button',on_click=function() error('disabled ancestor received input') end}}},
    {id='items',kind='list',items={'one','two'},value=2}}}
local ui=UI.new(root); ui.focus='button'
return {init=function()
    local pending=UI.inspect(ui)
    assert(pending[1].layout_pending and pending[1].rect==nil)
    UI.layout(ui,384,216)
    local first,next,total=UI.inspect(ui,0,2)
    assert(#first==2 and next==2 and total==6)
    local second,after=UI.inspect(ui,next,2)
    assert(second[1].id=='entry' and second[1].parent=='hidden' and not second[1].visible)
    assert(second[1].rect==nil and second[1].value_truncated and second[1].value_bytes==300)
    assert(#second[1].value==255 and #sc.input.boundaries(second[1].value)==86)
    local last,done=UI.inspect(ui,after,2)
    assert(not last[1].enabled and last[2].item_count==2 and last[2].value==2 and done==nil)
    first[1].rect.x=12345; second[1].value='changed'
    assert(ui.root.rect.x==0 and ui.nodes.entry.value==long)
    local empty,tail=UI.inspect(ui,999,2); assert(#empty==0 and tail==nil)
    assert(not pcall(UI.inspect,ui,-1,2) and not pcall(UI.inspect,ui,0,257))
    assert(not pcall(UI.inspect,ui,0,.5))
    assert(not pcall(UI.inspect,ui,false,2) and not pcall(UI.inspect,ui,0,false))
    ui.root.parent=ui.root
    assert(UI.inspect(ui,0,1)[1].invalid_parent)
    ui.root.parent=nil
    local calls=0
    local trap={__index=function() calls=calls+1; error('inspection invoked metamethod') end,
        __len=function() calls=calls+1; return 100 end}
    setmetatable(ui.nodes.items,trap); setmetatable(ui.nodes.items.items,trap)
    local copy=UI.inspect(ui,5,1); assert(copy[1].item_count==2 and calls==0)
    setmetatable(ui.nodes.items,nil); setmetatable(ui.nodes.items.items,nil)
    local tree={id='large',children={}}
    for i=1,600 do tree.children[i]={id='node'..i,kind='label'} end
    local large=UI.new(tree); local offset,count=0,0
    repeat
        local page,next,total=UI.inspect(large,offset)
        assert(#page<=128 and total==601)
        for _,entry in ipairs(page) do
            assert(entry.id==(count==0 and 'large' or 'node'..count)); count=count+1
        end
        offset=next
    until offset==nil
    assert(count==601)
end,update=function(dt)
    UI.update(ui,dt,384,216)
    assert(ui.focus~='button')
    assert((ui.scroll.disabled or 0)==0)
end}''')
        self.invoke('--replay',self.replay([event(keys=['enter'],mouse=dict(x=20,y=20,inside=True,wheel_y=-1))]),'--frames',1)

    def test_closing_background_modal_keeps_front_focus(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('''local UI=require('shiny.ui')
local ui=UI.new{id='root',children={{id='outside',kind='button'},
    {id='back',modal=true,children={{id='back_button',kind='button'}}},
    {id='front',modal=true,children={{id='first',kind='button'},{id='second',kind='button'}}}}}
ui.focus='outside'
return {update=function(dt)
    local tick=sc.tick()
    if tick==2 then UI.set(ui,'back',{visible=false}) end
    if tick==3 then UI.set(ui,'front',{visible=false}) end
    UI.update(ui,dt,384,216)
    assert(ui.focus==({'first','second','second','outside'})[tick+1])
end}''')
        self.invoke('--replay',self.replay([event(),event(1,keys=['tab']),event(2),event(3)]),'--frames',4)

    def test_nested_modal_focus_and_no_activation_fallthrough(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('''local UI=require('shiny.ui')
local Input=require('shiny.input')
local actions=Input.new{confirm={{key='enter'}}}
local ui; local opens,nested,closes=0,0,0
ui=UI.new{id='root',children={
    {id='open',kind='button',on_click=function() opens=opens+1; UI.set(ui,'dialog',{visible=true}) end},
    {id='dialog',modal=true,visible=false,children={
        {id='inside',kind='button',on_click=function() nested=nested+1; UI.set(ui,'nested',{visible=true}) end},
        {id='close',kind='button',on_click=function() closes=closes+1; UI.set(ui,'dialog',{visible=false}) end}}},
    {id='nested',modal=true,visible=false,children={
        {id='finish',kind='button',on_click=function() UI.set(ui,'nested',{visible=false}) end}}}}}
ui.focus='open'
local expected={'inside','inside','finish','finish','inside','close','open','open','inside'}
return {init=function() UI.layout(ui,384,216) end,update=function(dt)
    local tick=sc.tick()
    if tick==8 then UI.set(ui,'dialog',{visible=true}) end
    if tick==9 then UI.set(ui,'open',{disabled=true}); UI.set(ui,'dialog',{visible=false}) end
    Input.update(actions); UI.update(ui,dt,384,216,actions)
    if tick<9 then assert(ui.focus==expected[tick+1]) else assert(ui.focus==nil) end
    assert(opens==1 and nested==(tick>=2 and 1 or 0) and closes==(tick>=6 and 1 or 0))
    assert(not Input.pressed(actions,'confirm'))
end,draw=function() UI.draw(ui) end}''')
        path=self.replay([event(keys=['enter']),event(1),event(2,keys=['enter']),event(3),
                          event(4,keys=['enter']),event(5,keys=['tab']),event(6,keys=['enter']),
                          event(7),event(8),event(9)])
        self.invoke('--replay',path,'--frames',10)

    def test_spatial_focus_grid_modal_and_action_consumption(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('''local UI=require('shiny.ui')
local Input=require('shiny.input')
local function button(id,x,y) return {id=id,kind='button',x=x,y=y,w=60,h=28,text=id} end
local ghost=button('disabled',65,0);ghost.disabled=true
local hidden=button('hidden',70,0);hidden.visible=false
local ui=UI.new{id='root',kind='overlay',padding=0,children={
    button('a',0,0),button('d',100,50),button('b',100,0),button('c',0,50),ghost,hidden,
    {id='modal',kind='overlay',modal=true,visible=false,padding=0,x=100,y=20,w=140,h=80,
     children={button('m1',0,0),button('m2',70,0)}}}}
ui.focus='a'
local actions=Input.new{move={{key='left'},{key='right'},{key='up'},{key='down'},
    {button='dpad_left'},{button='dpad_right'},{button='dpad_up'},{button='dpad_down'}}}
local expected={'b','d','c','a','a','a','d','d','m2','d'}
return {init=function() UI.layout(ui,384,216) end,update=function(dt)
    local tick=sc.tick()
    if tick==8 then UI.set(ui,'modal',{visible=true}) end
    if tick==9 then UI.set(ui,'modal',{visible=false}) end
    Input.update(actions); UI.update(ui,dt,384,216,actions)
    assert(ui.focus==expected[tick+1],tick..': '..tostring(ui.focus))
    assert(not Input.down(actions,'move') and not Input.pressed(actions,'move') and not Input.released(actions,'move'))
end,draw=function() UI.draw(ui) end}''')
        path=self.replay([event(keys=['right']),event(1,keys=['down']),event(2,keys=['left']),
            event(3,keys=['up']),event(4),event(5,keys=['up']),
            event(6,connected=True,buttons=['right_shoulder']),event(7),
            event(8,connected=True,buttons=['dpad_right']),event(9)])
        self.invoke('--replay',path,'--frames',10)

    def test_spatial_focus_preserves_control_direction_ownership(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('''local UI=require('shiny.ui')
local ui=UI.new{id='root',kind='overlay',padding=0,children={
    {id='button',kind='button',x=0,y=0,w=80,h=28},
    {id='slider',kind='slider',x=100,y=0,w=100,h=28,value=.5,step=.1},
    {id='list',kind='list',x=100,y=50,w=100,h=60,items={'one','two','three'},value=1},
    {id='input',kind='input',x=0,y=50,w=80,h=32,value='ABCD'}}}
ui.focus='button'
local expected={'slider','slider','slider','list','list','list','input','input','input','button'}
return {init=function() UI.layout(ui,384,216) end,update=function(dt)
    UI.update(ui,dt,384,216)
    local tick=sc.tick()
    assert(ui.focus==expected[tick+1],tick..': '..tostring(ui.focus))
    assert(math.abs(ui.nodes.slider.value-(tick<2 and .5 or .6))<1e-9)
    assert(ui.nodes.list.value==(tick<5 and 1 or 2))
    if tick==6 or tick==7 then assert(ui.nodes.input.editor.cursor==5) end
    if tick==8 then assert(ui.nodes.input.editor.cursor==4 and ui.nodes.input.value=='ABCD') end
end,draw=function() UI.draw(ui) end}''')
        path=self.replay([event(keys=['right']),event(1),event(2,keys=['right']),
            event(3,keys=['down']),event(4),event(5,keys=['down']),event(6,keys=['left']),
            event(7),event(8,keys=['left']),event(9,connected=True,buttons=['dpad_up'])])
        self.invoke('--replay',path,'--frames',10)

    def test_tabs_switch_panels_and_refresh_layout(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('''local UI=require('shiny.ui')
local changes={}
local function tab(id,page,disabled)
    return {id=id,kind='tab',text=id,group='book',page=page,w=80,disabled=disabled,
        on_change=function(page) changes[#changes+1]=page end}
end
local ui=UI.new{id='root',children={
    {id='bar',kind='row',h=40,padding=0,gap=0,children={tab('a','pa'),tab('b','pb',true),tab('c','pc')}},
    {id='pa',h=70,children={{id='content_a',kind='label',text='Page A'}}},
    {id='pb',h=70},{id='pc',h=70,children={{id='content_c',kind='label',text='Page C'}}}}}
ui.focus='a'
local expected={'pc','pa','pa','pc','pa','pa'}
return {init=function()
    UI.layout(ui,384,216)
    assert(ui.nodes.a.value and not ui.nodes.pc.visible and ui.nodes.pc.rect==nil)
end,update=function(dt)
    local tick=sc.tick()
    if tick==4 then UI.select_tab(ui,'a') end
    if tick==5 then
        assert(not pcall(UI.select_tab,ui,'b'))
        assert(not pcall(UI.set,ui,'a',{text='changed',page='missing'}))
        assert(ui.nodes.a.text=='a' and ui.nodes.a.page=='pa')
    end
    UI.update(ui,dt,384,216)
    local page=expected[tick+1]
    assert(ui.nodes[page].visible and ui.nodes[page].rect)
    assert(ui.nodes.pa.visible~=ui.nodes.pc.visible and not ui.nodes.pb.visible)
    assert(ui.nodes.a.value==ui.nodes.pa.visible and ui.nodes.c.value==ui.nodes.pc.visible)
    assert(not ui.dirty)
    if tick>=4 then assert(#changes==4) end
end,draw=function() UI.draw(ui) end}''')
        path=self.replay([event(keys=['right']),event(1,connected=True,buttons=['dpad_left']),
                          event(2,mouse=dict(x=180,y=20,inside=True,buttons=['left'])),
                          event(3,mouse=dict(x=180,y=20,inside=True,buttons=[])),event(4),event(5)])
        self.invoke('--replay',path,'--frames',6)

    def test_invalid_tab_relationships(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('''local UI=require('shiny.ui')
return {init=function()
    assert(not pcall(UI.new,{id='a',kind='tab',group='g',page='missing'}))
    assert(not pcall(UI.new,{id='root',children={{id='a',kind='tab',group='g',page='root'}}}))
    assert(not pcall(UI.new,{id='root',children={
        {id='a',kind='tab',group='g',page='page'}, {id='b',kind='tab',group='g',page='page'}, {id='page'}}}))
    assert(not pcall(UI.new,{id='root',children={
        {id='a',kind='tab',group='g',page='p1',value=true},
        {id='b',kind='tab',group='g',page='p2',value=true},{id='p1'},{id='p2'}}}))
    assert(not pcall(UI.new,{id='root',children={
        {id='a',kind='tab',group='g',page='p1'},
        {id='p1',children={{id='b',kind='tab',group='g',page='p2'}}},{id='p2'}}}))
end}''')
        self.invoke('--frames',1)

    def test_reverse_focus_from_no_selection(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('''local UI=require('shiny.ui')
local ui=UI.new{id='root',children={{id='a',kind='button'},
    {id='b',kind='button'},{id='c',kind='button'}}}
return {update=function(dt)
    UI.update(ui,dt,384,216)
    assert(ui.focus==({'c','c','a','c'})[sc.tick()+1])
end}''')
        path=self.replay([event(keys=['right_shift','tab']),event(1),
                          event(2,keys=['tab']),event(3,connected=True,buttons=['left_shoulder'])])
        self.invoke('--replay',path,'--frames',4)

    def test_list_selection_navigation_and_data_changes(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('return {limits={draws=32}}','project.lua')
        self.source('''local UI=require('shiny.ui')
local Input=require('shiny.input')
local items={}; for i=1,10000 do items[i]={id='item-'..i,label='Item '..i} end
local activated={}
local node={id='items',kind='list',items=items,h=60,row_height=20,
    on_activate=function(value) activated[#activated+1]=value end}
local ui=UI.new{id='root',children={node,{id='close',kind='button',text='Close'}}}
local actions=Input.new{move={{key='down'},{button='dpad_down'}}}
local expected={2,3,6,10000,1,2,2,2,2}
return {init=function() UI.layout(ui,384,216) end,update=function(dt)
    local tick=sc.tick()
    if tick==9 then UI.set(ui,'items',{items={}}) end
    if tick==10 then UI.set(ui,'items',{items={'one','two'}}) end
    Input.update(actions); UI.update(ui,dt,384,216,actions)
    if tick<9 then assert(node.value==expected[tick+1]) end
    if tick==2 then assert(ui.scroll.items==60) end
    if tick==3 then assert(ui.scroll.items==199940) end
    if tick==4 then assert(ui.scroll.items==0) end
    if tick==5 then assert(ui.focus=='items' and not Input.pressed(actions,'move')) end
    if tick==6 then assert(#activated==1 and activated[1]==2) end
    if tick==7 then assert(ui.focus=='close') end
    if tick==8 then assert(ui.focus=='items') end
    if tick==9 then assert(node.value==nil and ui.scroll.items==0) end
    if tick>=10 then assert(node.value==2) end
    if tick>=11 then assert(#activated==2 and activated[2]==2) end
end,draw=function() UI.draw(ui) end}''')
        path=self.replay([
            event(mouse=dict(x=20,y=38,inside=True,buttons=['left'])),
            event(1,keys=['down']),event(2,keys=['page_down']),event(3,keys=['end']),
            event(4,keys=['home']),event(5,connected=True,buttons=['dpad_down']),
            event(6,connected=True,buttons=['south']),
            event(7,connected=True,buttons=['right_shoulder']),
            event(8,connected=True,buttons=['left_shoulder']),event(9),
            event(10,keys=['end']),event(11,keys=['enter']),
            event(12,mouse=dict(x=20,y=63,inside=True,buttons=['left'])),
            event(13,mouse=dict(x=20,y=63,inside=True,buttons=[]))])
        self.invoke('--replay',path,'--frames',14)

    def test_multiline_drag_and_hidden_capture(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source(r'''local UI=require('shiny.ui')
local Edit=require('shiny.textedit')
local value='a\n中B'
local node={id='entry',kind='input',value=value,w=180,h=80,multiline=true}
local ui=UI.new{id='root',children={node}}
return {update=function(dt)
    local tick=sc.tick()
    if tick==4 then UI.set(ui,'entry',{visible=false}) end
    if tick==5 then UI.set(ui,'entry',{visible=true}); ui.focus='entry' end
    UI.update(ui,dt,384,216)
    local e=node.editor
    if tick==0 then assert(e.cursor==1) end -- Lower half still belongs to the first line.
    if tick==1 then assert(Edit.selected(e)==value) end
    if tick==2 then assert(Edit.selected(e)=='a') end -- Release position updates the endpoint.
    if tick>=3 then assert(e.cursor==1 and e.anchor==1) end
    if tick>=4 then assert(ui.active==nil) end
end}''')
        path=self.replay([
            event(mouse=dict(x=14,y=25,inside=True,buttons=['left'])),
            event(1,mouse=dict(x=500,y=40,inside=False,buttons=['left'])),
            event(2,mouse=dict(x=500,y=12,inside=False,buttons=[])),
            event(3,mouse=dict(x=14,y=12,inside=True,buttons=['left'])),
            event(4,mouse=dict(x=500,y=40,inside=False,buttons=['left'])),
            event(5,mouse=dict(x=500,y=40,inside=False,buttons=['left']))])
        self.invoke('--replay',path,'--frames',6)

    def test_text_drag_selection_graphemes_and_release(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('''local UI=require('shiny.ui')
local Edit=require('shiny.textedit')
local value='á中B'
local node={id='entry',kind='input',value=value,w=180}
local ui=UI.new{id='root',children={node}}
return {update=function(dt)
    UI.update(ui,dt,384,216)
    local e=node.editor; local tick=sc.tick()
    local valid={}; for _,p in ipairs(sc.input.boundaries(e.value)) do valid[p]=true end
    assert(valid[e.cursor] and valid[e.anchor])
    if tick==0 or tick==2 then assert(e.cursor==1 and e.anchor==1) end
    if tick==1 or tick==3 or tick==4 or tick==6 then assert(Edit.selected(e)==value) end
    if tick==5 then assert(e.value=='Z') end
    if tick==7 then assert(e.cursor==1 and e.anchor==1 and #e.undo==0) end
end}''')
        path=self.replay([
            event(mouse=dict(x=14,y=12,inside=True,buttons=['left'])),
            event(1,mouse=dict(x=500,y=12,inside=False,buttons=['left'])),
            event(2,mouse=dict(x=14,y=12,inside=True,buttons=[])),
            event(3,keys=['left_shift'],mouse=dict(x=170,y=12,inside=True,buttons=['left'])),
            event(4,keys=['left_shift'],mouse=dict(x=170,y=12,inside=True,buttons=[])),
            event(5,text='Z'),event(6,keys=['left_control','z']),
            event(7,mouse=dict(x=14,y=12,inside=True,buttons=['left']))])
        self.invoke('--replay',path,'--frames',8)

    def test_ui_pointer_capture_outside_release(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('''local Input=require('shiny.input')
local UI=require('shiny.ui')
local actions=Input.new{fire={{mouse='left'}},alternate={{mouse='right'}}}
local clicks=0
local ui=UI.new{id='root',children={{id='button',kind='button',text='OK',
    on_click=function() clicks=clicks+1 end}}}
return {update=function(dt)
    Input.update(actions); UI.update(ui,dt,384,216,actions)
    assert(not Input.pressed(actions,'fire') and not Input.released(actions,'fire'))
    if sc.tick()==2 then assert(Input.pressed(actions,'alternate')) end
    if sc.tick()<4 then assert(clicks==0) else assert(clicks==1) end
end}''')
        path=self.replay([
            event(mouse=dict(x=20,y=20,inside=True,buttons=['left'])),
            event(1,mouse=dict(x=380,y=200,inside=True,buttons=[])),
            event(2,mouse=dict(x=20,y=20,inside=True,buttons=['right'])),
            event(3,mouse=dict(x=20,y=20,inside=True,buttons=['left'])),
            event(4,mouse=dict(x=20,y=20,inside=True,buttons=[]))])
        self.invoke('--replay',path,'--frames',5)

    def test_ui_consumes_bound_actions_before_gameplay(self):
        shutil.copytree(ROOT/'lua/shiny',self.path/'lib/shiny')
        self.source('''local Input=require('shiny.input')
local UI=require('shiny.ui')
local actions=Input.new{jump={{key='enter'},{key='x'},{button='south'}},move={{key='a'}}}
local clicks=0
local root={id='root',children={{id='ok',kind='button',text='OK'}}}
root.children[1].on_click=function() clicks=clicks+1; root.modal=false end
local ui=UI.new(root); ui.focus='ok'
return {update=function(dt)
    local tick=sc.tick()
    if tick==3 then root.modal=true end
    Input.update(actions)
    Input.dispatch(actions,{
        {update=function() UI.update(ui,dt,384,216,actions) end},
        {update=function()
            if tick==0 then assert(clicks==1 and not Input.pressed(actions,'jump')) end
            if tick==1 then assert(not Input.released(actions,'jump')) end
            if tick==2 then assert(Input.pressed(actions,'jump')) end
            if tick==3 then
                assert(clicks==2 and not root.modal)
                assert(not Input.down(actions,'move') and not Input.down(actions,'jump'))
            elseif tick<3 or tick==9 then assert(Input.down(actions,'move'))
            else assert(not Input.down(actions,'move')) end
            if tick==4 then assert(not Input.down(actions,'jump')) end -- Enter release is still consumed.
            if tick==5 then assert(not Input.down(actions,'jump') and not Input.pressed(actions,'jump')) end
            if tick==6 then assert(clicks==3 and not Input.down(actions,'jump')) end
            if tick==7 then assert(not Input.released(actions,'jump')) end
            if tick==9 then assert(Input.pressed(actions,'jump')) end
        end}})
end}''')
        path=self.replay([event(keys=['enter','a']),event(1,keys=['a']),
                          event(2,keys=['x','a']),event(3,keys=['enter','x','a']),
                          event(4,keys=['x','a']),event(5,keys=['x','a']),
                          event(6,keys=['a'],connected=True,buttons=['south']),
                          event(7,keys=['a'],connected=True),event(8),event(9,keys=['a','x'])])
        self.invoke('--replay',path,'--frames',10)

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="shiny-input-")
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name)
        self.source("return {}")

    def source(self, text, name="main.lua"):
        (self.path / name).write_text(text, encoding="utf-8")

    def invoke(self, *args, ok=True):
        result = subprocess.run([str(BINARY), "--headless", str(self.path), *map(str,args)],
                                capture_output=True, text=True, encoding="utf-8", timeout=20)
        self.assertEqual(result.returncode == 0, ok, result.stderr or result.stdout)
        return json.loads(result.stdout) if ok else result.stderr

    def replay(self, events):
        self.source("\n".join(json.dumps(x) for x in [{"version":2}, *events])+"\n", "input.jsonl")
        return self.path / "input.jsonl"

    def test_full_name_catalog_and_edges(self):
        meta = json.loads(subprocess.check_output([str(BINARY),"--api"], text=True, encoding="utf-8"))
        keys, buttons = meta["keys"], meta["gamepad_buttons"]
        self.assertEqual(len(keys), len(set(keys)))
        self.assertEqual(len(buttons),17)
        self.assertTrue({"a","0","f12","escape","left_shift","kp_enter","kb_menu"} <= set(keys))
        for name in keys + buttons + meta["gamepad_axes"]:
            self.assertIn("'"+name+"'", (ROOT / "docs/api.lua").read_text(encoding="utf-8"))
        self.source("local keys={"+",".join(map(json.dumps,keys))+"}; local buttons={"+
                    ",".join(map(json.dumps,buttons))+"}"+'''
return {update=function()
  local t=sc.tick()
  for _,k in ipairs(keys) do
    assert(sc.key_down(k)==(t<2)); assert(sc.key_pressed(k)==(t==0)); assert(sc.key_released(k)==(t==2))
  end
  for _,b in ipairs(buttons) do
    assert(sc.gamepad_down(b)==(t<2)); assert(sc.gamepad_pressed(b)==(t==0)); assert(sc.gamepad_released(b)==(t==2))
  end
  assert(sc.gamepad_connected()==(t<2))
end}''')
        path=self.replay([event(keys=keys,connected=True,buttons=buttons),event(2)])
        self.invoke("--frames",4,"--replay",path)

    def test_axes_short_edges_and_determinism(self):
        self.source('''return {update=function()
 local t=sc.tick()
 if t==0 then
   assert(sc.gamepad_axis('left_x')==0)
   assert(math.abs(sc.gamepad_axis('right_y')+.5)<.000001)
   assert(sc.gamepad_axis('right_trigger')==1)
   assert(math.abs(sc.gamepad_axis('left_trigger',0)-.6)<.000001)
 end
 if t==1 then
   assert(sc.key_pressed('escape') and sc.key_released('escape') and not sc.key_down('escape'))
   assert(sc.gamepad_pressed('south') and sc.gamepad_released('south') and not sc.gamepad_down('south'))
   assert(sc.pressed('jump') and sc.released('jump'))
 end
 if t==2 then assert(not sc.key_pressed('escape') and not sc.gamepad_pressed('south')) end
 if t==3 then assert(not sc.gamepad_connected() and sc.gamepad_axis('right_trigger')==0) end
end}''')
        path=self.replay([event(connected=True,axes=dict(left_x=.2,right_y=-.6,left_trigger=.6,right_trigger=1)),
                          event(1,connected=True,key_pressed=["escape"],key_released=["escape"],
                                button_pressed=["south"],button_released=["south"]),event(3)])
        first=self.invoke("--frames",4,"--replay",path)
        self.assertEqual(first,self.invoke("--frames",4,"--replay",path))
        self.assertFalse(first["input"]["gamepad"]["connected"])

    def test_same_tick_release_repress(self):
        self.source('''return {update=function()
 if sc.tick()==1 then assert(sc.key_down('space') and sc.key_pressed('space') and sc.key_released('space')) end
end}''')
        path=self.replay([event(keys=["space"]),event(1,keys=["space"],key_pressed=["space"],key_released=["space"])])
        self.invoke("--frames",2,"--replay",path)

    def test_source_handover_does_not_repeat_actions(self):
        self.source('''return {update=function()
 local t=sc.tick()
 assert(sc.down('jump'))
 assert(sc.pressed('jump')==(t==0 or t==3))
 assert(sc.released('jump')==(t==3))
end}''')
        path=self.replay([event(keys=["space"]),event(1,connected=True,buttons=["south"]),
                          event(2,keys=["space"]),event(3,keys=["space"],
                          key_pressed=["space"],key_released=["space"])])
        self.invoke("--frames",4,"--replay",path)

    def test_underflow_axes_have_canonical_zero(self):
        path=self.replay([event(connected=True,axes=dict(left_x=0))])
        neutral=self.invoke("--frames",1,"--replay",path)
        path=self.replay([event(connected=True,axes=dict(left_x=-1e-100))])
        self.assertEqual(neutral,self.invoke("--frames",1,"--replay",path))
        path=self.replay([event(axes=dict(left_x=-1e-100))])
        self.assertIn("disconnected gamepad",self.invoke("--frames",1,"--replay",path,ok=False))

    def test_scene_inherits_held_without_press(self):
        self.source("return {update=function() assert(sc.key_pressed('a')); sc.scene('next.lua') end}")
        self.source('''return {init=function()
 assert(sc.key_down('a') and not sc.key_pressed('a'))
 assert(sc.gamepad_down('south') and not sc.gamepad_pressed('south'))
end,update=function()
 assert(sc.key_down('a') and not sc.key_pressed('a'))
 assert(sc.down('jump') and not sc.pressed('jump'))
end}''', "next.lua")
        path=self.replay([event(keys=["a"],connected=True,buttons=["south"])])
        result=self.invoke("--frames",3,"--replay",path)
        self.assertEqual(result["scene"],"next.lua")

    def test_legacy_actions_do_not_invent_devices(self):
        self.source('''return {update=function()
 assert(sc.down('jump') and not sc.key_down('space') and not sc.gamepad_connected())
 assert(sc.pressed('jump')==(sc.tick()==0))
end}''')
        self.source("0 16\n", "legacy.txt")
        self.invoke("--frames",3,"--replay",self.path/"legacy.txt")

    def test_invalid_lua_arguments(self):
        for expression in ["sc.key_down('A')","sc.key_down('a',true)","sc.gamepad_connected(1)","sc.gamepad_axis('left_x',0,1)","sc.key_pressed(1)","sc.key_released({})",
                           "sc.gamepad_down('a')","sc.gamepad_axis('bogus')",
                           "sc.gamepad_axis('left_x',1)","sc.gamepad_axis('left_x',-1)",
                           "sc.gamepad_axis('left_x','0.2')","sc.gamepad_axis('left_x',0/0)",
                           "sc.gamepad_axis('left_x',math.huge)","sc.key_down('a'..string.char(0)..'b')"]:
            with self.subTest(expression=expression):
                self.source("return {init=function() "+expression+" end}")
                self.invoke("--frames",0,ok=False)

    def test_malformed_device_replays(self):
        bad=[]
        for key,value in [("extra",1),("frame",.5),("frame",-1),("keys",["a","a"]),
                          ("keys",["bogus"]),("keys",[1]),("keys","a"),("key_pressed",["a","a"])]:
            sample=event(); sample[key]=value; bad.append(sample)
        for axes in [dict(left_x=1.1),dict(left_trigger=-.1),dict(left_y="0"),dict(foo=0)]:
            bad.append(event(connected=True,axes=axes))
        bad.extend([event(buttons=["south"]),event(axes=dict(left_x=.1)),event(button_pressed=["south"]),
                    event(connected=True,buttons=["south","south"]),event(connected="yes")])
        for sample in bad:
            with self.subTest(sample=sample):
                path=self.replay([sample]); error=self.invoke("--frames",1,"--replay",path,ok=False)
                self.assertIn("input.jsonl:2:",error)
        for raw in ['{"version":4}\n','{"version":2,"extra":0}\n',
                    '{"version":2}\n{"frame":0,"frame":1}\n',
                    '{"version":2}\n{"frame":0,"keys":[],"gamepad":{"connected":true,"axes":{"left_x":NaN}}}\n']:
            self.source(raw,"input.jsonl")
            self.invoke("--frames",1,"--replay",self.path/"input.jsonl",ok=False)
        path=self.replay([event(1),event(1)])
        self.assertIn("input.jsonl:3:",self.invoke("--frames",2,"--replay",path,ok=False))

    def test_showcase_and_debug_option(self):
        self.assertIn("--debug-keys",subprocess.check_output([str(BINARY),"--help"],text=True))
        result=subprocess.run([str(BINARY),"--headless",str(ROOT/"examples/input"),"--frames","125",
                               "--replay",str(ROOT/"examples/input/demo.jsonl")],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)

if __name__ == "__main__":
    unittest.main()
