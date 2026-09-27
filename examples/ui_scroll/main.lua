local UI=require("shiny.ui")
local ui
local chosen=8
local cards={}
for i=1,12 do
    local number=i
    cards[i]={id="room-"..i,kind="button",w=80,h=50,text=("ROOM %02d"):format(i),
        on_click=function()
            chosen=number
            UI.set(ui,"choice",{text=("Selected: ROOM %02d"):format(number)})
        end}
end
ui=UI.new{id="root",kind="column",padding=12,gap=6,children={
    {id="title",kind="label",h=24,text="HORIZONTAL SCROLL"},
    {id="hint",kind="label",h=20,text="Wheel, drag the bar, or move focus with Tab"},
    {id="rooms",kind="scroll",axis="horizontal",h=80,padding=8,gap=6,children=cards},
    {id="choice",kind="label",h=24,text="Selected: ROOM 08"},
    {id="footer",kind="label",h=20,text="The selected card stays visible when focus moves."},
}}
return {width=384,height=216,gravity=0,ambient=1,
    init=function() UI.layout(ui,384,216);ui.focus="room-8" end,
    ui_update=function(dt) UI.update(ui,dt,384,216) end,
    update=function()
        sc.debug.watch("ui_scroll",{selected=chosen,offset=ui.scroll.rooms or 0,
            maximum=ui.nodes.rooms.scroll_max})
    end,
    draw=function()
        sc.rect(0,0,384,216,"#111C2EFF",true)
        UI.draw(ui)
    end,
}
