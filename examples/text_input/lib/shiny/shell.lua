-- Shared title/pause/settings UI used by the playable examples.
local UI=require("shiny.ui")
local Settings=require("shiny.settings")
local Shell={}
function Shell.new(title,subtitle,on_start)
    local shell={mode="title",title=title,subtitle=subtitle,elapsed=0}
    shell.ui=UI.new({id="menu",kind="overlay",padding=0,children={
        {id="panel",kind="column",x=22,y=8,w=340,h=198,gap=4,background="#17263EFF",children={
            {id="title",kind="label",text=title,h=26,font_size=20},
            {id="subtitle",kind="label",text=subtitle,h=24,font_size=11},
            {id="play",kind="button",text="PLAY / RESUME",h=20,on_click=function()
                local first=shell.mode=="title"; shell.mode="game"; sc.app.pause(false)
                if first and on_start then on_start() end
            end},
            {id="settings",kind="button",text="SETTINGS",h=20,on_click=function()
                shell.settings=Settings.new(function() shell.settings=nil; shell.ui.focus="settings" end)
            end},
            {id="checkpoints",kind="row",h=20,padding=0,children={
            {id="save",kind="button",text="SAVE",on_click=function()
                local ok,err=sc.save.write("checkpoint"); shell.notice=ok and "Saved" or err
            end},
            {id="load",kind="button",text="LOAD",on_click=function()
                local ok,err=sc.save.load("checkpoint"); if not ok then shell.notice=err end
            end},
            }},
            {id="quit",kind="button",text="QUIT",h=22,on_click=function() sc.app.quit() end},
        }}
    }})
    UI.layout(shell.ui,384,216)
    shell.ui.focus="play"
    return shell
end
function Shell.update(shell,dt)
    shell.elapsed=shell.elapsed+dt
    if shell.settings then Settings.update(shell.settings,dt); return false end
    if sc.input.key_pressed("escape") then
        shell.mode=shell.mode=="game" and "pause" or "game"
        sc.app.pause(shell.mode~="game")
    end
    if shell.mode~="game" then
        sc.app.pause(true)
        UI.update(shell.ui,dt,384,216)
        return false
    end
    return true
end
function Shell.draw(shell)
    if shell.mode~="game" then
        sc.rect(0,0,384,216,"#08101EE8",true)
        if shell.settings then Settings.draw(shell.settings) else UI.draw(shell.ui) end
        if shell.notice and not shell.settings then sc.text(shell.notice:sub(1,120),10,200,10,"#FFCB77FF",true) end
    end
end
function Shell.finish(shell,message)
    shell.mode="end"; UI.set(shell.ui,"title",{text="COMPLETE"}); UI.set(shell.ui,"subtitle",{text=message})
    sc.app.pause(true)
end
return Shell
