-- Draft edits over ordinary named actions; native settings commit before live input.
local Input=require('shiny.input')
local UI=require('shiny.ui')
local Rebind={}
local axes={'left_x','left_y','right_x','right_y','left_trigger','right_trigger'}
local function kind(source) return source.axis and 'axis' or source.button and 'pad' or 'key' end
local function refresh(panel,message)
    local labels={}
    for _,source in ipairs(panel.draft[panel.selected] or {}) do labels[#labels+1]=Input.label(source) end
    UI.set(panel.ui,'bindings.current',{text=table.concat(labels,' / ')})
    UI.set(panel.ui,'bindings.notice',{text=message or 'Edits are drafts until APPLY succeeds.'})
end
local function capture(panel,category)
    panel.capture={kind=category,armed=false}
    refresh(panel,'Release controls. Esc / pad Back cancels.')
end
function Rebind.new(actions,on_back)
    assert(actions.profile,'rebinding requires a persistent input profile')
    local panel={actions=actions,draft=Input.copy_bindings(actions.bindings),on_back=on_back}
    local names,seen={},{}
    for _,bindings in ipairs({actions.defaults,actions.bindings}) do
        for name in pairs(bindings) do if not seen[name] then names[#names+1]=name;seen[name]=true end end
    end
    table.sort(names);assert(#names>0,'rebinding requires actions');panel.names=names;panel.selected=names[1]
    local items={};for _,name in ipairs(names) do items[#items+1]={id=name,label=name:upper()} end
    panel.ui=UI.new{id='bindings',kind='overlay',padding=0,children={
        {id='bindings.panel',kind='column',x=10,y=8,w=364,h=200,padding=8,gap=4,modal=true,background='#17263EFF',children={
            {id='bindings.title',kind='label',text='CONTROLS',h=18,font_size=16},
            {id='bindings.body',kind='row',h=112,padding=0,gap=8,children={
                {id='bindings.actions',kind='list',items=items,value=1,w=100,row_height=20,font_size=10,
                    on_change=function(index) panel.selected=names[index];refresh(panel) end},
                {id='bindings.editor',kind='column',padding=0,gap=4,children={
                    {id='bindings.current',kind='label',h=52,font_size=10},
                    {id='bindings.capture',kind='row',h=24,padding=0,gap=4,children={
                        {id='bindings.key',kind='button',text='KEY/MOUSE',w=84,font_size=9,on_click=function() capture(panel,'key') end},
                        {id='bindings.pad',kind='button',text='PAD',font_size=9,on_click=function() capture(panel,'pad') end},
                        {id='bindings.axis',kind='button',text='AXIS',font_size=9,on_click=function() capture(panel,'axis') end}}},
                    {id='bindings.clear',kind='button',text='CLEAR ACTION',h=22,font_size=10,on_click=function()
                        if panel.selected=='menu' then refresh(panel,'Keep a menu binding so you can pause again.')
                        else panel.draft[panel.selected]={};refresh(panel) end
                    end}}}}},
            {id='bindings.notice',kind='label',h=20,font_size=9},
            {id='bindings.buttons',kind='row',h=22,padding=0,gap=4,children={
                {id='bindings.apply',kind='button',text='APPLY',font_size=10,on_click=function()
                    if actions.bindings.menu and #(panel.draft.menu or {})==0 then
                        refresh(panel,'Keep a menu binding so you can pause again.');return
                    end
                    local next_bindings=Input.copy_bindings(panel.draft)
                    local all=sc.settings.get().bindings;all[actions.profile]=next_bindings
                    local ok,err=sc.settings.apply({bindings=all})
                    if ok then actions.bindings=next_bindings end
                    refresh(panel,ok and 'Controls saved.' or err)
                end},
                {id='bindings.defaults',kind='button',text='DEFAULTS',font_size=10,on_click=function()
                    panel.draft=Input.copy_bindings(actions.defaults);refresh(panel,'Defaults restored in draft. APPLY to save.')
                end},
                {id='bindings.back',kind='button',text='BACK',font_size=10,on_click=on_back}}}
        }}}}
    UI.layout(panel.ui,384,216);panel.ui.focus='bindings.actions';refresh(panel)
    return panel
end
local function neutral(snapshot,category)
    if category=='key' then return #snapshot.keys==0 and #snapshot.mouse.buttons==0 end
    for _,pad in ipairs(snapshot.pads) do
        if category=='pad' and #pad.buttons>0 then return false end
        if category=='axis' then for _,axis in ipairs(axes) do if math.abs(pad.axes[axis])>.25 then return false end end end
    end
    return true
end
local function source(snapshot,category)
    if category=='key' then
        if snapshot.key_pressed[1] then return {key=snapshot.key_pressed[1]} end
        if snapshot.mouse.pressed[1] then return {mouse=snapshot.mouse.pressed[1]} end
    else
        for slot,pad in ipairs(snapshot.pads) do
            if category=='pad' and pad.pressed[1] then return {button=pad.pressed[1],slot=slot} end
            if category=='axis' then for _,axis in ipairs(axes) do
                local value=pad.axes[axis]
                if math.abs(value)>=.65 then return {axis=axis,direction=value<0 and -1 or 1,slot=slot,deadzone=.2} end
            end end
        end
    end
end
local function conflict(panel,candidate)
    for _,name in ipairs(panel.names) do if name~=panel.selected then
        for _,other in ipairs(panel.draft[name] or {}) do
            if kind(other)==kind(candidate) and other.key==candidate.key and other.mouse==candidate.mouse and
                other.button==candidate.button and other.axis==candidate.axis and
                (other.direction or 1)==(candidate.direction or 1) and
                (not other.slot or not candidate.slot or other.slot==candidate.slot) then return name end
        end
    end end
end
function Rebind.update(panel,dt)
    Input.consume_sources(panel.actions,{all=true})
    local recording=panel.capture
    if not recording then
        if sc.input.key_pressed('escape') or sc.input.gamepad_pressed('east') then panel.on_back();return end
        UI.update(panel.ui,dt,384,216,panel.actions);return
    end
    local snapshot=sc.input.snapshot()
    local cancel=sc.input.key_pressed('escape')
    for _,pad in ipairs(snapshot.pads) do for _,button in ipairs(pad.pressed) do if button=='back' then cancel=true end end end
    if cancel then
        panel.capture=nil;refresh(panel,'Capture cancelled.');return
    end
    if not recording.armed then
        if neutral(snapshot,recording.kind) then recording.armed=true;refresh(panel,'Press a control / move an axis. Esc cancels.') end
        return
    end
    local candidate=source(snapshot,recording.kind)
    if not candidate then return end
    local duplicate=conflict(panel,candidate)
    if duplicate then recording.armed=false;refresh(panel,'Already used by '..duplicate..'. Release and retry.');return end
    local replacement={}
    for _,old in ipairs(panel.draft[panel.selected] or {}) do if kind(old)~=recording.kind then replacement[#replacement+1]=old end end
    replacement[#replacement+1]=candidate;panel.draft[panel.selected]=replacement
    panel.capture=nil;refresh(panel,'Draft updated. APPLY to save.')
end
function Rebind.draw(panel) UI.draw(panel.ui) end
return Rebind
