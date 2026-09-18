local UI=require("shiny.ui")
local ui
local theme={background="#111C2EFF",panel="#1D2C43FF",text="#E6EDF7FF",muted="#8CA4C4FF",accent="#66D9B0FF",focus="#FFCB77FF",padding=10,gap=8,font_size=20,font="ui"}
return {title="ShinyCore / Text input",width=640,height=360,gravity=0,ambient=1,
init=function()
 ui=UI.new({id="root",kind="column",padding=20,children={
  {id="heading",kind="label",text="中文 / 星灯工坊",h=32,font_size=24},
  {id="help",kind="label",text="Click to focus / Shift + arrows select / Ctrl + Z undo",h=28,font_size=14},
  {id="name",kind="input",value="星灯工坊",h=38},
  {id="notes",kind="input",value="中文像素世界\n欢迎返回星灯工坊",multiline=true,h=124},
  {id="confirm",kind="button",text="保存 / Save",h=34,on_click=function() sc.debug.watch("saved",ui.nodes.notes.value) end},
 }},theme)
 UI.layout(ui,640,360); ui.focus="notes"
end,
update=function(dt) UI.update(ui,dt,640,360); sc.debug.watch("ui",UI.inspect(ui)); sc.debug.watch("text",ui.nodes.notes.value) end,
draw=function() sc.rect(0,0,640,360,"#111C2EFF",true); UI.draw(ui) end}
