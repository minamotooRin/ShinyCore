-- An ordinary authored UI: changes stay local until Apply succeeds.
local UI=require("shiny.ui")
local Input=require("shiny.input")
local Rebind=require("shiny.rebind")
local Settings={}
local resolutions={{768,432},{960,540},{1152,648},{1280,720},{1920,1080}}
function Settings.new(on_back,actions)
    local panel={draft=sc.settings.get(),notice="",on_back=on_back}
    local draft=panel.draft
    local function resolution() return "SIZE  "..draft.width.." x "..draft.height end
    local display={
        {id="settings.resolution",kind="button",text=resolution(),h=22,on_click=function(_,node)
            local index=0
            for i,size in ipairs(resolutions) do if size[1]==draft.width and size[2]==draft.height then index=i end end
            local size=resolutions[index%#resolutions+1]
            draft.width,draft.height=size[1],size[2]; node.text=resolution()
        end},
        {id="settings.borderless",kind="checkbox",text="BORDERLESS",value=draft.mode=="borderless",h=22,
            on_change=function(value) draft.mode=value and "borderless" or "windowed" end},
        {id="settings.smooth",kind="checkbox",text="SMOOTH SCALE",value=draft.scale=="smooth",h=22,
            on_change=function(value) draft.scale=value and "smooth" or "integer" end},
        {id="settings.vsync",kind="checkbox",text="VSYNC",value=draft.vsync,h=22,
            on_change=function(value) draft.vsync=value end},
    }
    local audio={}
    for _,bus in ipairs({"master","music","sfx","ui"}) do
        audio[#audio+1]={id="settings."..bus..".row",kind="row",h=22,padding=0,gap=4,children={
            {id="settings."..bus..".label",kind="label",text=bus:upper(),w=60,font_size=10},
            {id="settings."..bus,kind="slider",value=draft.volume[bus],step=.05,
                on_change=function(value) draft.volume[bus]=value end},
        }}
    end
    panel.ui=UI.new({id="settings",kind="overlay",padding=0,children={
        {id="settings.panel",kind="column",x=10,y=8,w=364,h=200,padding=8,gap=4,background="#17263EFF",modal=true,children={
            {id="settings.title",kind="label",text="SETTINGS",h=22,font_size=18},
            {id="settings.controls",kind="row",h=100,padding=0,gap=12,children={
                {id="settings.display",kind="column",padding=0,gap=4,children=display},
                {id="settings.audio",kind="column",padding=0,gap=4,children=audio},
            }},
            {id="settings.notice",kind="label",text="Apply to save / Back to discard",h=22,font_size=10},
            {id="settings.buttons",kind="row",h=24,padding=0,children={
                {id="settings.apply",kind="button",text="APPLY",on_click=function()
                    -- Bindings may have changed elsewhere while this menu was open.
                    local ok,error=sc.settings.apply({width=draft.width,height=draft.height,mode=draft.mode,
                        scale=draft.scale,vsync=draft.vsync,volume=draft.volume})
                    panel.notice=ok and "Settings saved" or error
                    UI.set(panel.ui,"settings.notice",{text=panel.notice})
                end},
                {id="settings.bindings",kind="button",text="CONTROLS",visible=actions~=nil and actions.profile~=nil,on_click=function()
                    panel.bindings=Rebind.new(actions,function() panel.bindings=nil;panel.ui.focus="settings.bindings" end)
                end},
                {id="settings.back",kind="button",text="BACK",on_click=function() on_back() end},
            }},
        }}
    }})
    UI.layout(panel.ui,384,216); panel.ui.focus="settings.resolution"
    return panel
end
function Settings.update(panel,dt,actions)
    if actions then Input.consume_sources(actions,{all=true}) end
    if panel.bindings then Rebind.update(panel.bindings,dt);return end
    if sc.input.key_pressed("escape") or sc.input.gamepad_pressed("east") then panel.on_back(); return end
    UI.update(panel.ui,dt,384,216,actions)
end
function Settings.draw(panel)
    if panel.bindings then Rebind.draw(panel.bindings) else UI.draw(panel.ui) end
end
return Settings
