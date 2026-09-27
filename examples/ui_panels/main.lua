local UI=require('shiny.ui')
local theme={};for key,value in pairs(UI.theme) do theme[key]=value end
theme.font_size=12
theme.skins={button={resource='panel',slice={left=4,right=4,top=4,bottom=4}}}
local names={'top_left','top','top_right','left','center','right','bottom_left','bottom','bottom_right'}
local nodes={}
local selected='center'
for _,name in ipairs(names) do
    nodes[#nodes+1]={id=name,kind='button',anchor=name,w=100,h=38,text=name,
        on_click=function() selected=name end}
end
local ui=UI.new({id='anchors',kind='overlay',padding=12,children=nodes},theme)
return {width=384,height=216,gravity=0,ambient=1,
    init=function() UI.layout(ui,384,216);ui.focus='center' end,
    ui_update=function(dt) UI.update(ui,dt,384,216) end,
    update=function() sc.debug.watch('selected',selected);sc.debug.watch('focus',ui.focus) end,
    draw=function()
        sc.rect(0,0,384,216,theme.background,true)
        UI.draw(ui)
    end,
}
