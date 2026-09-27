-- No streamed world exists until a new journey or a complete checkpoint is selected.
local Shell=require("shiny.shell")
local UI=require("shiny.ui")
local theme=require("theme")
local Input=require("shiny.input")
local Controls=require("controls")
local actions
local shell,confirmation
local function load()
    local ok,err=sc.save.load("checkpoint")
    if not ok then shell.notice=err end
end
local function start()
    local ok,err=pcall(sc.save.delete,"checkpoint")
    if not ok then shell.notice=err; confirmation=nil; return end
    for _,key in ipairs({"position","traveler","courier","herbs","quest_stage","equipment","route_cleared"}) do
        sc.state.set(key,nil)
    end
    sc.state.set("quest_stage","meet")
    sc.scene("main.lua")
end
local function new_journey()
    confirmation=UI.new({id="new_journey",kind="modal",x=28,y=48,w=328,h=136,gap=8,children={
        {id="warning",kind="label",text="开始新旅程将清除原检查点、地图修改和采集进度。",h=44,wrap=306},
        {id="cancel",kind="button",text="保留原进度",h=24,on_click=function() confirmation=nil end},
        {id="confirm",kind="button",text="确认开始新旅程",h=24,on_click=start},
    }},theme)
    UI.layout(confirmation,384,216); confirmation.focus="cancel"
end
return {
    title="ShinyCore / Wayfarer",width=384,height=216,gravity=0,ambient=1,
    init=function()
        actions=Controls.new()
        shell=Shell.new("旅人 · 月光草","探索森林，采药修路，守护村庄")
        shell.ui.theme=theme
        UI.set(shell.ui,"title",{font_size=32,h=40})
        UI.set(shell.ui,"subtitle",{font_size=16})
        local exists=false
        for _,slot in ipairs(sc.save.list()) do if slot.slot=="checkpoint" then exists=true end end
        -- A missing primary may still have a valid backup.
        if not exists then exists=sc.save.read("checkpoint")~=nil end
        UI.set(shell.ui,"play",{text=exists and "继续旅程" or "开始旅程",on_click=exists and load or start})
        UI.set(shell.ui,"settings",{text="设置"})
        UI.set(shell.ui,"save",{text="新旅程",on_click=new_journey})
        UI.set(shell.ui,"load",{text="读取",on_click=load})
        UI.set(shell.ui,"quit",{text="退出"})
        UI.layout(shell.ui,384,216)
        sc.app.pause(true)
    end,
    update=function(dt)
        Input.update(actions)
        if confirmation then UI.update(confirmation,dt,384,216,actions) else Shell.update(shell,dt,actions) end
        sc.debug.watch("quest",{mode=confirmation and "confirm_new" or "title"})
    end,
    draw=function()
        Shell.draw(shell)
        if confirmation then sc.rect(0,0,384,216,"#08101EEE",true); UI.draw(confirmation) end
    end,
}
