local theme={}
for name,value in pairs(require("shiny.ui").theme) do theme[name]=value end
theme.font="ui"; theme.font_size=16
return theme
