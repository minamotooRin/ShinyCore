-- No streamed world exists until a new journey or a complete checkpoint is selected.
local Shell=require("shiny.shell")
local UI=require("shiny.ui")
local theme=require("theme")
local Input=require("shiny.input")
local Controls=require("controls")
local actions
local shell,confirmation,confirmation_mode,request,request_kind,settings_click
local probe_needed,has_checkpoint,queued_action,probe_error,start_ready=true,nil,nil,nil,false
local show_delete_error
local function load()
    local ok,err=sc.save.load("checkpoint")
    if not ok then shell.notice=err end
end
local function finish_start()
    for _,key in ipairs({"position","traveler","courier","herbs","quest_stage","equipment","route_cleared"}) do
        sc.state.set(key,nil)
    end
    sc.state.set("quest_stage","meet")
    sc.scene("main.lua")
end
local function start()
    local id,err=sc.save.delete_async("checkpoint")
    if not id and err and err:find("disk save directory",1,true) then
        local ok,reason=pcall(sc.save.delete,"checkpoint")
        if ok then finish_start() else show_delete_error(reason) end
        return
    end
    if not id then show_delete_error(err); return end
    request,request_kind=id,"delete"
    confirmation,confirmation_mode=nil,nil
    shell.notice=nil
    UI.set(shell.ui,"play",{text="清理旧进度…",disabled=true})
    for _,name in ipairs({"settings","save","load"}) do UI.set(shell.ui,name,{disabled=true}) end
end
local function new_journey()
    confirmation_mode="confirm_new"
    confirmation=UI.new({id="new_journey",kind="modal",x=28,y=48,w=328,h=136,gap=8,children={
        {id="warning",kind="label",text="开始新旅程将清除原检查点、地图修改和采集进度。",h=44,wrap=306},
        {id="cancel",kind="button",text="保留原进度",h=24,on_click=function() confirmation=nil; confirmation_mode=nil end},
        {id="confirm",kind="button",text="确认开始新旅程",h=24,on_click=start},
    }},theme)
    UI.layout(confirmation,384,216); confirmation.focus="cancel"
end
show_delete_error=function(err)
    if err then sc.log("checkpoint removal: "..err) end
    shell.notice="清理旧进度失败。可重试或返回标题。"
    confirmation_mode="delete_error"
    confirmation=UI.new({id="delete_error",kind="modal",x=28,y=48,w=328,h=136,gap=8,children={
        {id="warning",kind="label",text="检查点未能完整清理；重新尝试或返回标题检查存档。",h=44,wrap=306},
        {id="retry",kind="button",text="重试清理",h=24,on_click=start},
        {id="cancel",kind="button",text="返回标题",h=24,on_click=function()
            confirmation,confirmation_mode=nil,nil; probe_needed=true
        end},
    }},theme)
    UI.layout(confirmation,384,216); confirmation.focus="retry"
end
local function show_checkpoint(record,err)
    has_checkpoint=record and true or false
    probe_error=err
    shell.notice=err and "存档读取失败：请重试或新建旅程；详情见错误日志。" or nil
    UI.set(shell.ui,"play",{
        text=err and "存档读取失败" or (has_checkpoint and "继续旅程" or "开始旅程"),
        disabled=err~=nil,
        on_click=has_checkpoint and load or start,
    })
    UI.set(shell.ui,"settings",{disabled=false,on_click=settings_click})
    UI.set(shell.ui,"save",{disabled=false,on_click=new_journey})
    UI.set(shell.ui,"load",{
        text=err and "重试读取" or "读取",
        disabled=not err and not has_checkpoint,
        on_click=err and function() probe_needed=true; shell.notice=nil end or load,
    })
    if err then shell.ui.focus="load" end
end
local function show_pending()
    UI.set(shell.ui,"play",{text="检查存档…",disabled=false,on_click=function() queued_action="play" end})
    UI.set(shell.ui,"settings",{text="设置",disabled=false,on_click=function() queued_action="settings" end})
    UI.set(shell.ui,"save",{text="新旅程",disabled=false,on_click=function() queued_action="new" end})
    UI.set(shell.ui,"load",{text="读取",disabled=false,on_click=function() queued_action="load" end})
end
local function probe()
    probe_needed=false
    show_pending()
    local id,err=sc.save.read_chunks_async("checkpoint",{})
    if id then request,request_kind=id,"probe"; return end
    -- Headless runs without --save-dir use the bounded in-memory save service.
    if err and err:find("disk save directory",1,true) then
        local record,read_error=sc.save.read("checkpoint")
        show_checkpoint(record,read_error~="save slot does not exist" and read_error or nil)
    else show_checkpoint(nil,err) end
end
return {
    title="ShinyCore / Wayfarer",width=384,height=216,gravity=0,ambient=1,
    init=function()
        actions=Controls.new()
        shell=Shell.new("旅人 · 月光草","探索森林，采药修路，守护村庄")
        settings_click=shell.ui.nodes.settings.on_click
        shell.ui.theme=theme
        UI.set(shell.ui,"title",{font_size=32,h=40})
        UI.set(shell.ui,"subtitle",{font_size=16})
        show_pending()
        UI.set(shell.ui,"quit",{text="退出"})
        UI.layout(shell.ui,384,216)
        assert(sc.audio.music("theme",{loop=true,volume=.07,fade=.4}))
        sc.app.pause(true)
    end,
    update=function(dt)
        Input.update(actions)
        if start_ready then start_ready=false; finish_start() end
        if probe_needed then probe() end
        if queued_action and not request and not probe_needed then
            local action=queued_action; queued_action=nil
            if action=="play" then
                if not probe_error then
                    if has_checkpoint then load() elseif has_checkpoint==false then start() end
                end
            elseif action=="load" then
                if has_checkpoint and not probe_error then load() end
            elseif action=="new" then new_journey()
            elseif action=="settings" then settings_click() end
        end
        if confirmation then UI.update(confirmation,dt,384,216,actions) else Shell.update(shell,dt,actions) end
        sc.debug.watch("quest",{mode=confirmation_mode or "title"})
    end,
    ui_update=function(dt)
        if not request then return end
        local status=sc.save.status(request)
        if status.status=="pending" then
            Input.update(actions,"ui")
            Shell.update(shell,dt,actions)
            return
        end
        local operation=request_kind
        local result=operation=="probe" and status.status=="complete" and sc.save.result(request) or nil
        sc.save.release(request)
        request,request_kind=nil,nil
        if operation=="delete" then
            if status.status=="complete" then start_ready=true
            else show_delete_error(status.error) end
        else show_checkpoint(result and result.record,status.error) end
    end,
    draw=function()
        Shell.draw(shell)
        if confirmation then sc.rect(0,0,384,216,"#08101EEE",true); UI.draw(confirmation) end
    end,
}
