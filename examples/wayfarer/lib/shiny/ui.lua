-- Retained author data, dirty layout, fixed-tick events, draw-only presentation.
local Edit=require("shiny.textedit")
local UI={}
UI.theme={background="#111C2EFF", panel="#1D2C43FF", text="#E6EDF7FF", muted="#8CA4C4FF",
    accent="#66D9B0FF", focus="#FFCB77FF", padding=8, gap=6, font_size=14}
local interactive={button=true,checkbox=true,slider=true,input=true,tab=true,list=true}
local function inside(r,x,y) return x>=r.x and y>=r.y and x<r.x+r.w and y<r.y+r.h end
local function children(node)
    local out={}
    for _,child in ipairs(node.children or {}) do if child.visible~=false then out[#out+1]=child end end
    return out
end
local function emit(ui,node,event,value)
    ui.events[#ui.events+1]={id=node.id,event=event,value=value}
    local handler=node["on_"..event]
    if handler then handler(value,node) end
end
-- Cache grapheme positions and bounded draw runs when authored text changes.
local function text_layout(ui,node)
    local size=node.font_size or ui.theme.font_size
    local font=node.font or ui.theme.font or ""
    local text=node.kind=="input" and (node.value or "") or (node.text or "")
    if node.kind=="checkbox" then text=(node.value and "[x] " or "[ ] ")..text end
    local width=(node.kind=="input" and not node.multiline) and math.huge or math.max(1,node.rect.w-12)
    local cached=node.text_layout
    if cached and cached.text==text and cached.size==size and cached.font==font and cached.width==width then return cached end
    local result={text=text,size=size,font=font,width=width,runs={},positions={},height=size}
    local offsets=sc.input.boundaries(text)
    local advances,x,y,run={},0,0,nil
    for i=1,#offsets-1 do
        local first,last=offsets[i],offsets[i+1]
        local glyph=text:sub(first,last-1)
        if glyph=="\n" then
            result.positions[#result.positions+1]={byte=first,x=x,y=y,w=0}
            x=0; y=y+size; run=nil
        else
            local advance=advances[glyph]
            if not advance then advance=sc.measure(glyph,size,font); advances[glyph]=advance end
            if x>0 and x+advance>width then x=0; y=y+size; run=nil end
            result.positions[#result.positions+1]={byte=first,x=x,y=y,w=advance}
            assert(#glyph<=511,"a grapheme exceeds the native text command limit")
            if not run or #run.text+#glyph>511 then
                run={text="",x=x,y=y}; result.runs[#result.runs+1]=run
            end
            run.text=run.text..glyph; x=x+advance
        end
    end
    result.positions[#result.positions+1]={byte=#text+1,x=x,y=y,w=0}
    result.height=y+size; node.text_layout=result
    return result
end
local function caret_position(layout,byte)
    for _,position in ipairs(layout.positions) do if position.byte>=byte then return position end end
    return layout.positions[#layout.positions]
end
local function refresh_text(ui)
    for _,node in ipairs(ui.order) do
        if node.kind~="list" then text_layout(ui,node) end
    end
end
function UI.new(tree,theme)
    local ui={root=tree,theme=theme or UI.theme,nodes={},order={},dirty=true,focus=nil,events={},scroll={},width=0,height=0}
    local function index(node,parent)
        assert(type(node.id)=="string" and not ui.nodes[node.id],"UI needs unique string IDs")
        ui.nodes[node.id]=node; node.parent=parent
        for _,child in ipairs(node.children or {}) do index(child,node) end
    end
    index(tree,nil)
    return ui
end
function UI.set(ui,id,patch)
    local node=assert(ui.nodes[id],"unknown UI ID: "..tostring(id))
    for key,value in pairs(patch) do
        assert(key~="id" and key~="children" and key~="parent", "replace the UI tree to change its structure")
        node[key]=value
    end
    ui.dirty=true
end
local function layout(ui,node,x,y,w,h)
    local theme=ui.theme
    w=math.max(node.min_w or 0,math.min(node.max_w or w,node.w or w))
    h=math.max(node.min_h or 0,math.min(node.max_h or h,node.h or h))
    if node.anchor=="center" then x=x+(ui.width-w)/2; y=y+(ui.height-h)/2 end
    node.rect={x=x+(node.x or 0),y=y+(node.y or 0),w=w,h=h}
    ui.order[#ui.order+1]=node
    local items=children(node)
    local padding=node.padding or theme.padding
    local gap=node.gap or theme.gap
    local inner_w,inner_h=math.max(0,w-padding*2),math.max(0,h-padding*2)
    local px,py=node.rect.x+padding,node.rect.y+padding-(ui.scroll[node.id] or 0)
    local columns=node.columns or 1
    for i,child in ipairs(items) do
        local cw,ch,cx,cy=inner_w,child.h or 28,px,py
        if node.kind=="row" then
            cw=child.w or math.max(0,(inner_w-gap*(#items-1))/#items)
            ch=child.h or inner_h; px=px+cw+gap
        elseif node.kind=="grid" then
            cw=math.max(0,(inner_w-gap*(columns-1))/columns)
            cx=node.rect.x+padding+((i-1)%columns)*(cw+gap)
            cy=py+math.floor((i-1)/columns)*(ch+gap)
        elseif node.kind=="overlay" then ch=child.h or inner_h
        else py=py+ch+gap end
        layout(ui,child,cx,cy,cw,ch)
    end
    node.content_h=math.max(0,py-node.rect.y+padding)
end
function UI.layout(ui,width,height)
    if width~=ui.width or height~=ui.height then ui.dirty=true end
    ui.width,ui.height=width,height
    if not ui.dirty then return end
    ui.order={}; layout(ui,ui.root,0,0,width,height); refresh_text(ui); ui.dirty=false
end
local function under(node,parent)
    while node do if node==parent then return true end; node=node.parent end
    return false
end
local function visible_point(node,x,y)
    if not inside(node.rect,x,y) then return false end
    local parent=node.parent
    while parent do
        if parent.clip or parent.kind=="scroll" then if not inside(parent.rect,x,y) then return false end end
        parent=parent.parent
    end
    return true
end
function UI.update(ui,dt,width,height)
    assert(dt>=0,"nonnegative UI step required")
    UI.layout(ui,width,height); ui.events={}
    local x,y,viewport=sc.input.mouse()
    local modal
    for _,node in ipairs(ui.order) do if node.modal then modal=node end end
    local focusable={}
    for _,node in ipairs(ui.order) do
        if interactive[node.kind] and not node.disabled and (not modal or under(node,modal)) then focusable[#focusable+1]=node end
    end
    local valid_focus=false
    for _,node in ipairs(focusable) do if node.id==ui.focus then valid_focus=true; break end end
    if not valid_focus then ui.focus=nil end
    local advance=sc.input.key_pressed("tab") or sc.input.gamepad_pressed("dpad_down")
    local retreat=sc.input.gamepad_pressed("dpad_up") or (advance and sc.input.key_down("left_shift"))
    if advance or retreat then
        local index=0
        for i,node in ipairs(focusable) do if node.id==ui.focus then index=i end end
        if #focusable>0 then ui.focus=focusable[((index-1+(retreat and -1 or 1))%#focusable)+1].id end
    end
    local hovered
    if viewport then for i=#focusable,1,-1 do if visible_point(focusable[i],x,y) then hovered=focusable[i]; break end end end
    if hovered and sc.input.mouse_pressed("left") then ui.focus=hovered.id; ui.active=hovered.id end
    local activate=(sc.input.key_pressed("enter") or sc.input.gamepad_pressed("south")) and ui.nodes[ui.focus]
    if sc.input.mouse_released("left") then
        if hovered and hovered.id==ui.active then activate=hovered end
        ui.active=nil
    end
    if activate then
        if activate.kind=="checkbox" then activate.value=not activate.value; emit(ui,activate,"change",activate.value)
        elseif activate.kind=="button" or activate.kind=="tab" then emit(ui,activate,"click",true) end
    end
    sc.input.focus_text(false)
    local focused=ui.nodes[ui.focus]
    if focused and focused.kind=="slider" then
        local value=focused.value or 0
        if sc.input.key_pressed("left") or sc.input.gamepad_pressed("dpad_left") then value=value-(focused.step or .05) end
        if sc.input.key_pressed("right") or sc.input.gamepad_pressed("dpad_right") then value=value+(focused.step or .05) end
        if ui.active==focused.id and sc.input.mouse_down("left") then value=(x-focused.rect.x)/focused.rect.w end
        value=math.max(0,math.min(1,value))
        if value~=focused.value then focused.value=value; emit(ui,focused,"change",value) end
    elseif focused and focused.kind=="input" then
        local e=focused.editor
        if not e or e.value~=(focused.value or "") then e=Edit.new(focused.value); focused.editor=e end
        local shift=sc.input.key_down("left_shift") or sc.input.key_down("right_shift")
        local control=sc.input.key_down("left_control") or sc.input.key_down("right_control")
        local committed,composition=sc.input.text(); focused.composition=composition
        local before=text_layout(ui,focused)
        local function locate(px,py)
            local best,distance=e.cursor,math.huge
            for _,position in ipairs(before.positions) do
                local d=math.abs(position.y-py)*10000+math.abs(position.x-px)
                if d<distance then best,distance=position.byte,d end
            end
            return best
        end
        if hovered==focused and sc.input.mouse_pressed("left") then
            e.cursor=locate(x-focused.rect.x-6+(focused.text_scroll_x or 0),y-focused.rect.y-4+(focused.text_scroll_y or 0))
            if not shift then e.anchor=e.cursor end
        end
        if focused.multiline and (sc.input.key_pressed("up") or sc.input.key_pressed("down")) then
            local position=caret_position(before,e.cursor)
            e.cursor=locate(position.x,position.y+(sc.input.key_pressed("up") and -before.size or before.size))
            if not shift then e.anchor=e.cursor end
        end
        if sc.input.key_pressed("left") then Edit.move(e,-1,shift) end
        if sc.input.key_pressed("right") then Edit.move(e,1,shift) end
        if sc.input.key_pressed("home") then Edit.home(e,shift) end
        if sc.input.key_pressed("end") then Edit.finish(e,shift) end
        if sc.input.key_pressed("backspace") then Edit.erase(e,-1) end
        if sc.input.key_pressed("delete") then Edit.erase(e,1) end
        if control and sc.input.key_pressed("a") then Edit.select_all(e) end
        if control and sc.input.key_pressed("c") then sc.input.clipboard(Edit.selected(e)) end
        if control and sc.input.key_pressed("x") then sc.input.clipboard(Edit.selected(e)); Edit.insert(e,"") end
        if control and sc.input.key_pressed("v") then Edit.insert(e,sc.input.clipboard(),focused.max_bytes) end
        if control and sc.input.key_pressed("z") then Edit.undo(e) end
        if control and sc.input.key_pressed("y") then Edit.redo(e) end
        if focused.multiline and sc.input.key_pressed("enter") then Edit.insert(e,"\n",focused.max_bytes) end
        if not focused.multiline then committed=committed:gsub("[\r\n]","") end
        if #committed>0 and not control then Edit.insert(e,committed,focused.max_bytes) end
        if e.value~=focused.value then focused.value=e.value; emit(ui,focused,"change",e.value) end
        local updated=text_layout(ui,focused)
        local position=caret_position(updated,e.cursor)
        focused.text_scroll_x=math.max(0,position.x-focused.rect.w+20)
        focused.text_scroll_y=math.max(0,position.y-focused.rect.h+updated.size+8)
        sc.input.focus_text(focused.rect.x+6+position.x-focused.text_scroll_x,
            focused.rect.y+4+position.y-focused.text_scroll_y+updated.size)
    end
    local _,wheel=sc.input.wheel()
    if wheel~=0 and viewport then
        for i=#ui.order,1,-1 do
            local node=ui.order[i]
            if (node.kind=="scroll" or node.kind=="list") and visible_point(node,x,y) and (not modal or under(node,modal)) then
                local total=node.items and #node.items*(node.row_height or 24) or node.content_h
                ui.scroll[node.id]=math.max(0,math.min(math.max(0,total-node.rect.h),(ui.scroll[node.id] or 0)-wheel*32))
                ui.dirty=true; break
            end
        end
    end
    refresh_text(ui)
    return {pointer=hovered~=nil or modal~=nil,keyboard=focused~=nil or modal~=nil,events=ui.events}
end
local function draw_node(ui,node)
    if node.visible==false then return end
    local r,t=node.rect,ui.theme
    local color=node.color or t.text
    local kind=node.kind or "column"
    if node.background or (kind~="label" and kind~="image" and kind~="row" and kind~="column" and kind~="grid" and kind~="overlay") then
        sc.rect(r.x,r.y,r.w,r.h,node.background or t.panel,true)
    end
    if node.id==ui.focus then
        sc.rect(r.x,r.y,r.w,2,t.focus,true); sc.rect(r.x,r.y+r.h-2,r.w,2,t.focus,true)
    end
    local clipped=node.clip or kind=="scroll" or kind=="list" or kind=="input"
    if clipped then sc.clip(r.x,r.y,r.w,r.h) end
    if kind=="image" then sc.image(node.resource,r.x,r.y,r.w,r.h,true)
    elseif kind=="slider" or kind=="progress" then
        sc.rect(r.x,r.y+r.h/2-3,r.w*math.max(0,math.min(1,node.value or 0)),6,t.accent,true)
    elseif kind=="list" then
        local row=node.row_height or 24
        local offset=ui.scroll[node.id] or 0
        local first=math.floor(offset/row)+1
        local last=math.min(#(node.items or {}),first+math.ceil(r.h/row))
        for i=first,last do
            local item=node.items[i]
            sc.text(type(item)=="table" and (item.label or tostring(i)) or tostring(item),r.x+6,r.y+(i-1)*row-offset,
                node.font_size or t.font_size,color,true,{font=node.font or t.font})
        end
    else
        local cached=node.text_layout
        if cached then
            local sx,sy=node.text_scroll_x or 0,node.text_scroll_y or 0
            local ox,oy=r.x+6-sx,r.y+4-sy
            if kind=="input" and node.editor and ui.focus==node.id then
                local first,last=Edit.selection(node.editor)
                for _,position in ipairs(cached.positions) do
                    if position.byte>=first and position.byte<last then
                        sc.rect(ox+position.x,oy+position.y,math.max(1,position.w),cached.size,"#385777FF",true)
                    end
                end
                local caret=caret_position(cached,node.editor.cursor)
                sc.rect(ox+caret.x,oy+caret.y,1,cached.size,t.focus,true)
                if node.composition and #node.composition>0 then
                    sc.text(node.composition,ox+caret.x,oy+caret.y,cached.size,t.accent,true,{font=cached.font})
                end
            end
            for _,run in ipairs(cached.runs) do
                if oy+run.y+cached.size>=r.y and oy+run.y<r.y+r.h then
                    sc.text(run.text,ox+run.x,oy+run.y,cached.size,color,true,{font=cached.font})
                end
            end
        end
    end
    for _,child in ipairs(node.children or {}) do draw_node(ui,child) end
    if clipped then sc.clip() end
end
function UI.draw(ui) draw_node(ui,ui.root) end
function UI.inspect(ui)
    local out={}
    for _,node in ipairs(ui.order) do out[#out+1]={id=node.id,kind=node.kind or "column",rect=node.rect,focused=ui.focus==node.id} end
    return out
end
return UI
