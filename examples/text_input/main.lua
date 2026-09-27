local UI=require("shiny.ui")
local ui
local theme={background="#111C2EFF",panel="#1D2C43FF",text="#E6EDF7FF",muted="#8CA4C4FF",accent="#66D9B0FF",focus="#FFCB77FF",padding=10,gap=8,font_size=20,font="ui"}
return {title="ShinyCore / Text input",width=640,height=360,gravity=0,ambient=1,
init=function()
 ui=UI.new({id="root",kind="scroll",padding=20,children={
  {id="heading",kind="label",text="中文 / 星灯工坊",h=32,font_size=24,tooltip="Text input and scrolling fixture."},
  {id="help",kind="label",text="Click to focus / Shift + arrows select / Ctrl + Z undo",h=28,font_size=14},
  {id="name",kind="input",value="星灯工坊",h=38,tooltip="Single line: pasted line breaks are removed."},
  {id="notes",kind="input",value="中文像素世界\n欢迎返回星灯工坊",multiline=true,h=124,tooltip="Drag to select. Composition previews keep the original text until committed."},
  {id="size_label",kind="label",text="Text size",h=24,font_size=16},
  {id="size",kind="slider",value=.5,step=.1,page_step=.25,h=24,
   tooltip="Left / Right: step. Page Up / Down: large step. Home / End: limits.",
   on_change=function(value) UI.set(ui,"notes",{font_size=math.floor(14+value*12+.5)}) end},
  {id="confirm",kind="button",text="Inspect note",h=34,tooltip="Expose the current note in the Agent debug watch.",on_click=function() sc.debug.watch("inspected_note",ui.nodes.notes.value) end},
  {id="scroll_help",kind="label",text="Wheel or drag the scrollbar. Tab / gamepad focus reveals controls automatically.",h=96,font_size=16},
  {id="return",kind="button",text="Return to name",h=34,tooltip="Move focus to the name field and reveal it in the panel.",on_click=function() ui.focus="name" end},
 }},theme)
 UI.layout(ui,640,360); ui.focus="notes"
end,
update=function(dt) UI.update(ui,dt,640,360); sc.debug.watch("ui",UI.inspect(ui)); sc.debug.watch("text",ui.nodes.notes.value) end,
draw=function() sc.rect(0,0,640,360,"#111C2EFF",true); UI.draw(ui) end}
