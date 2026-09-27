-- Retained author data, dirty layout, fixed-tick events, draw-only presentation.
local Edit=require("shiny.textedit")
local Input=require("shiny.input")
local UI={}
UI.theme={background="#111C2EFF", panel="#1D2C43FF", text="#E6EDF7FF", muted="#8CA4C4FF",
    accent="#66D9B0FF", error="#F47C7CFF", focus="#FFCB77FF", selection="#385777FF", padding=8, gap=6, font_size=14,
    tooltip_delay=.5,tooltip_width=240}
local interactive={button=true,checkbox=true,slider=true,input=true,tab=true,list=true}
local clickable={button=true,checkbox=true,tab=true}
local hint_keys={"tab","enter","space","left","right","up","down","home","end","page_up","page_down"}
local hint_buttons={"dpad_up","dpad_down","dpad_left","dpad_right","left_shoulder","right_shoulder","south"}
local repeat_keys={"left","right","up","down","page_up","page_down","backspace","delete"}
local repeat_buttons={"dpad_left","dpad_right","dpad_up","dpad_down"}
local function repeat_inputs(ui,dt,composing,modal)
    if composing or ui.focus~=ui.last_focus or modal~=ui.repeat_modal or sc.input.mouse_pressed("left") or
        sc.input.key_pressed("tab") or sc.input.gamepad_pressed("left_shoulder") or sc.input.gamepad_pressed("right_shoulder") then
        ui.repeat_hold=nil
    end
    local hold=ui.repeat_hold or {}; ui.repeat_hold=hold
    ui.repeat_keys={}; ui.repeat_buttons={}; ui.repeat_modal=modal
    local function sample(names,down,pressed,events,prefix)
        for _,name in ipairs(names) do
            local id=prefix..name
            events[name]=pressed(name)
            if composing or not down(name) then hold[id]=nil
            elseif events[name] or hold[id]==nil then hold[id]=.4
            else
                hold[id]=hold[id]-dt
                if dt>0 and hold[id]<=1e-9 then
                    events[name]=true
                    -- At most one action per UI update; a slow frame never floods callbacks.
                    hold[id]=.05
                end
            end
        end
    end
    sample(repeat_keys,sc.input.key_down,sc.input.key_pressed,ui.repeat_keys,"key:")
    sample(repeat_buttons,sc.input.gamepad_down,sc.input.gamepad_pressed,ui.repeat_buttons,"pad:")
end
local function key_pressed(ui,key) return ui.repeat_keys[key] or sc.input.key_pressed(key) end
local function pad_pressed(ui,key) return ui.repeat_buttons[key] or sc.input.gamepad_pressed(key) end
local function clips(node) return node.clip or node.kind=="scroll" or node.kind=="list" or node.kind=="input" end
local function field(node,patch,key)
    if patch and patch[key]~=nil then return patch[key] end
    return node[key]
end
local anchors={top_left={0,0},top={.5,0},top_right={1,0},left={0,.5},center={.5,.5},
    right={1,.5},bottom_left={0,1},bottom={.5,1},bottom_right={1,1}}
local function layout_fields(node,patch)
    local anchor=field(node,patch,"anchor")
    assert(anchor==nil or anchors[anchor],"unknown UI anchor")
    local axis=field(node,patch,"axis")
    assert(axis==nil or (field(node,patch,"kind")=="scroll" and
        (axis=="vertical" or axis=="horizontal")),"scroll axis must be vertical or horizontal")
    for _,key in ipairs({"x","y","w","h","min_w","min_h","max_w","max_h","padding","gap","columns"}) do
        local value=field(node,patch,key)
        assert(value==nil or (type(value)=="number" and value==value and math.abs(value)<math.huge and
            (key=="x" or key=="y" or value>=0)),"UI "..key.." must be finite and dimensions/spacing nonnegative")
    end
    for _,axis in ipairs({"w","h"}) do
        local low,high=field(node,patch,"min_"..axis),field(node,patch,"max_"..axis)
        assert(not low or not high or low<=high,"UI minimum exceeds maximum")
    end
    local columns=field(node,patch,"columns")
    assert(columns==nil or (columns>=1 and columns%1==0),"grid columns must be a positive integer")
end
local function skin_fields(node,patch,theme)
    local skin=field(node,patch,"skin")
    if skin==nil and theme.skins then skin=theme.skins[field(node,patch,"kind") or "column"] end
    if skin==nil or skin==false then return end
    assert(type(skin)=="table" and getmetatable(skin)==nil,"UI skin requires a plain image descriptor or false")
    assert(type(skin.resource)=="string" and #skin.resource>0 and #skin.resource<=127 and not skin.resource:find('\0',1,true),"skin resource requires an image name")
    local options={screen=true}
    for key,value in pairs(skin) do
        if key=="resource" then
        elseif key=="slice" then
            assert(type(value)=="table" and getmetatable(value)==nil,"skin slice requires a plain table")
            local slice={}
            for side,n in pairs(value) do
                assert((side=="left" or side=="right" or side=="top" or side=="bottom") and
                    type(n)=="number" and n>=0 and n<=8192,"invalid skin slice inset")
                slice[side]=n
            end
            options.slice=slice
        elseif key=="source_x" or key=="source_y" or key=="source_w" or key=="source_h" then
            assert(type(value)=="number" and value>=0 and value<=8192,"invalid skin source coordinate")
            options[key]=value
        elseif key=="flip_x" or key=="flip_y" or key=="diagonal" then
            assert(type(value)=="boolean","skin image flags must be boolean"); options[key]=value
        elseif key=="color" then
            assert(type(value)=="string" and (#value==7 or #value==9) and value:match("^#%x+$"),"invalid skin color")
            options.color=value
        else error("unknown skin field: "..tostring(key)) end
    end
    local w,h=options.source_w or 0,options.source_h or 0
    assert((w==0 and h==0 and (options.source_x or 0)==0 and (options.source_y or 0)==0) or
        (w>0 and h>0),"skin source requires a complete region")
    if options.slice and w>0 then
        local slice=options.slice
        assert((slice.left or 0)+(slice.right or 0)<w and (slice.top or 0)+(slice.bottom or 0)<h,"skin slice requires a positive center")
    end
    return skin.resource,options
end
local function tooltip_fields(node,patch)
    local text,delay,width=field(node,patch,"tooltip"),field(node,patch,"tooltip_delay"),field(node,patch,"tooltip_width")
    assert(text==nil or text==false or (type(text)=="string" and #text<=4096),"tooltip must be false or a string of at most 4096 bytes")
    assert(delay==nil or (type(delay)=="number" and delay>=0 and delay<math.huge),"tooltip_delay must be finite and nonnegative")
    assert(width==nil or (type(width)=="number" and width>=24 and width<math.huge),"tooltip_width must be finite and at least 24")
end
local function control_fields(node,patch)
    local kind,value=field(node,patch,"kind"),field(node,patch,"value")
    if kind=="slider" then
        assert(value==nil or (type(value)=="number" and value>=0 and value<=1),"slider value must be 0..1")
        for _,key in ipairs({"step","page_step"}) do
            local n=field(node,patch,key)
            assert(n==nil or (type(n)=="number" and n>0 and n<=1),"slider "..key.." must be greater than 0 and at most 1")
        end
    elseif kind=="checkbox" then assert(value==nil or type(value)=="boolean","checkbox value must be boolean")
    elseif kind=="list" then
        assert(value==nil or value==false or (type(value)=="number" and value>=1 and value<math.huge and value%1==0),"list value must be a positive item index or false to clear")
        local row=field(node,patch,"row_height")
        assert(row==nil or (type(row)=="number" and row>0 and row<math.huge),"list row_height must be finite and positive")
    end
end
local function list_ids(items)
    if items==nil then return end
    assert(type(items)=="table" and getmetatable(items)==nil,"list items require a plain dense array")
    local count,length,ids,keyed=0,#items,{},0
    for key,item in pairs(items) do
        assert(type(key)=="number" and key%1==0 and key>=1 and key<=length,"list items require a dense array")
        count=count+1
        assert(type(item)=="string" or (type(item)=="table" and getmetatable(item)==nil),"list item requires text or a plain record")
        local label=type(item)=="table" and item.label or item
        if type(item)=="table" and item.label==nil then label=tostring(key) end
        assert(type(label)=="string" and #label<=4095 and not label:find('\0',1,true),"list label requires text of at most 4095 bytes without NUL")
        if type(item)=="table" and item.id~=nil then
            local id=item.id
            assert(type(id)=="string" and #id>0 and #id<=128 and not id:find('\0',1,true) and not ids[id],"list item IDs require unique strings of 1..128 bytes without NUL")
            ids[id]=key; keyed=keyed+1
        end
    end
    assert(count==length,"list items require a dense array")
    assert(keyed==0 or keyed==length,"give every list item an ID, or none")
    return keyed>0 and ids or nil
end
local function inside(r,x,y) return x>=r.x and y>=r.y and x<r.x+r.w and y<r.y+r.h end
local function input_text(node,text)
    text=text:gsub("\r\n","\n"):gsub("\r","\n")
    if not node.multiline then text=text:gsub("\n","") end
    return text
end
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
local function under(node,parent)
    while node do if node==parent then return true end; node=node.parent end
    return false
end
local function enabled(node)
    while node do if node.disabled then return false end; node=node.parent end
    return true
end
-- Geometry dependencies follow parent flow; stable siblings reuse their assigned slot.
local geometry_fields={x=true,y=true,w=true,h=true,min_w=true,min_h=true,max_w=true,max_h=true,
    padding=true,gap=true,columns=true,anchor=true,kind=true,axis=true,visible=true}
local function invalidate(ui,node,order)
    ui.layout_pending=true
    if order then ui.order_pending=true end
    while node do node.layout_dirty=true; node=node.parent end
end
local function select_tab(ui,node,notify)
    local changed=not node.value
    for _,tab in ipairs(ui.tab_groups[node.group]) do
        tab.value=tab==node
        local page=ui.nodes[tab.page]
        if page.visible~=tab.value then page.visible=tab.value; invalidate(ui,page,true) end
    end
    if changed and notify then emit(ui,node,"change",node.page) end
end
function UI.select_tab(ui,id)
    local node=assert(ui.nodes[id],"unknown tab ID")
    assert(node.kind=="tab" and node.group and enabled(node) and node.visible~=false,"expected an enabled grouped tab")
    select_tab(ui,node,true)
end
-- Cache grapheme positions and bounded draw runs when authored text changes.
local function text_layout(ui,node)
    local size=node.font_size or ui.theme.font_size
    local font=node.font or ui.theme.font or ""
    local text=node.kind=="input" and (node.value or "") or (node.text or "")
    if node.kind=="checkbox" then text=(node.value and "[x] " or "[ ] ")..text end
    local first,last,caret,target_first,target_last,segments,segment_key
    if node.kind=="input" and ui.focus==node.id and node.editor and node.composition and #node.composition>0 then
        first,last=Edit.selection(node.editor)
        text=text:sub(1,first-1)..node.composition..text:sub(last)
        last=first+#node.composition
        caret=first+(node.composition_cursor or #node.composition+1)-1
        target_first=first+(node.composition_start or #node.composition+1)-1
        target_last=first+(node.composition_end or #node.composition+1)-1
        segments=node.composition_segments
        segment_key=node.composition_segments_key
    end
    local width=(node.kind=="input" and not node.multiline) and math.huge or math.max(1,node.rect.w-12)
    local cached=node.text_layout
    if cached and cached.text==text and cached.size==size and cached.font==font and cached.width==width and
        cached.preedit_first==first and cached.preedit_last==last and cached.preedit_cursor==caret and
        cached.target_first==target_first and cached.target_last==target_last and cached.segment_key==segment_key then return cached end
    local result={text=text,size=size,font=font,width=width,runs={},positions={},height=size,
        preedit_first=first,preedit_last=last,preedit_cursor=caret,target_first=target_first,target_last=target_last,segment_key=segment_key}
    local offsets=sc.input.boundaries(text)
    local advances,x,y,run={},0,0,nil
    local segment_index=1
    local priorities={input=0,fixed=0,converted=1,error=2,target_converted=3,target_unconverted=3}
    for i=1,#offsets-1 do
        local start,finish=offsets[i],offsets[i+1]
        local glyph=text:sub(start,finish-1)
        local composing=first and finish>first and start<last or false
        local targeted=composing and target_first<target_last and finish>target_first and start<target_last or false
        local kind,segment_end
        if composing and segments and #segments>0 then
            local low,high=start-first+1,finish-first+1
            while segments[segment_index] and segments[segment_index].finish<=low do segment_index=segment_index+1 end
            local at=segment_index
            while segments[at] and segments[at].start<high do
                local segment=segments[at]
                if not kind or priorities[segment.kind]>priorities[kind] then kind=segment.kind end
                segment_end=high>=segment.finish; at=at+1
            end
            targeted=kind=="target_converted" or kind=="target_unconverted"
        end
        if glyph=="\n" then
            result.positions[#result.positions+1]={byte=start,x=x,y=y,w=0,composition=composing}
            x=0; y=y+size; run=nil
        else
            local advance=advances[glyph]
            if not advance then advance=sc.measure(glyph,size,font); advances[glyph]=advance end
            if x>0 and x+advance>width then x=0; y=y+size; run=nil end
            result.positions[#result.positions+1]={byte=start,x=x,y=y,w=advance,composition=composing,targeted=targeted,kind=kind,segment_end=segment_end}
            assert(#glyph<=511,"a grapheme exceeds the native text command limit")
            if not run or run.composition~=composing or run.kind~=kind or #run.text+#glyph>511 then
                run={text="",x=x,y=y,composition=composing,kind=kind}; result.runs[#result.runs+1]=run
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
        if node.kind=="input" and node.id~=ui.focus then node.composition=nil; node.composition_segments=nil; node.composition_segments_key=nil end
        if node.kind~="list" then text_layout(ui,node) end
    end
end
function UI.new(tree,theme)
    local ui={root=tree,theme=theme or UI.theme,nodes={},order={},dirty=true,focus=nil,events={},scroll={},width=0,height=0}
    ui.tab_groups={}
    ui.hint={}
    tooltip_fields(ui.theme)
    layout_fields(ui.theme)
    assert(ui.theme.skins==nil or (type(ui.theme.skins)=="table" and getmetatable(ui.theme.skins)==nil),"theme skins require a plain table")
    ui.authored_nodes={}
    local tabs={}
    local function index(node,parent)
        assert(type(node.id)=="string" and not ui.nodes[node.id],"UI needs unique string IDs")
        tooltip_fields(node)
        control_fields(node)
        layout_fields(node)
        node.skin_resource,node.skin_options=skin_fields(node,nil,ui.theme)
        if node.kind=="list" then
            node.list_ids=list_ids(node.items)
            if node.value==false then node.value=nil end
        end
        if node.kind=="slider" and node.value==nil then node.value=0 end
        if node.kind=="checkbox" and node.value==nil then node.value=false end
        ui.nodes[node.id]=node; node.parent=parent
        ui.authored_nodes[#ui.authored_nodes+1]=node
        if node.kind=="tab" and (node.group or node.page) then tabs[#tabs+1]=node end
        for _,child in ipairs(node.children or {}) do index(child,node) end
    end
    index(tree,nil)
    local pages={}
    for _,tab in ipairs(tabs) do
        assert(type(tab.group)=="string" and #tab.group>0,"tab group requires a name")
        assert(tab.value==nil or type(tab.value)=="boolean","tab value must be boolean")
        local page=ui.nodes[tab.page]
        assert(page and not under(tab,page),"tab page must exist and cannot contain its tab")
        assert(not pages[tab.page],"tab pages must have one owner")
        pages[tab.page]=true
        local group=ui.tab_groups[tab.group] or {}; ui.tab_groups[tab.group]=group
        group[#group+1]=tab
    end
    for _,group in pairs(ui.tab_groups) do
        local selected
        for _,tab in ipairs(group) do
            for _,peer in ipairs(group) do
                assert(not under(peer,ui.nodes[tab.page]),"tab page cannot contain tabs from its own group")
            end
            if tab.value then assert(not selected,"tab group has multiple selected tabs"); selected=tab end
        end
        if not selected then
            for _,tab in ipairs(group) do if enabled(tab) and tab.visible~=false then selected=tab; break end end
        end
        if selected then select_tab(ui,selected,false)
        else for _,tab in ipairs(group) do tab.value=false; ui.nodes[tab.page].visible=false end end
    end
    if sc.debug and sc.debug.ui then sc.debug.ui(tree.id,ui) end
    return ui
end
function UI.set(ui,id,patch)
    local node=assert(ui.nodes[id],"unknown UI ID: "..tostring(id))
    tooltip_fields(node,patch)
    control_fields(node,patch)
    layout_fields(node,patch)
    local skin_resource,skin_options=skin_fields(node,patch,ui.theme)
    for key,value in pairs(patch) do
        assert(key~="id" and key~="children" and key~="parent", "replace the UI tree to change its structure")
        assert(node.kind~="tab" or (key~="group" and key~="page" and ((key~="value" and key~="kind") or not node.group)),"use UI.select_tab or rebuild tab relationships")
    end
    local is_list=field(node,patch,"kind")=="list"
    local changed_items=is_list and (patch.items~=nil or node.kind~="list")
    local ids=changed_items and list_ids(field(node,patch,"items")) or nil
    local previous=node.value
    local selected=previous and node.list_ids and node.items[previous]
    local geometry,order=false,false
    for key,value in pairs(patch) do
        if node[key]~=value then
            geometry=geometry or geometry_fields[key] or (is_list and (key=="items" or key=="row_height"))
            order=order or key=="visible"
        end
    end
    for key,value in pairs(patch) do node[key]=value end
    node.skin_resource,node.skin_options=skin_resource,skin_options
    if changed_items then
        node.list_ids=ids
        if patch.value==nil and selected then
            node.value=ids and ids[selected.id] or nil
            if node.value~=previous then node.list_change_pending=true end
        end
    end
    if is_list then
        if node.value==false then node.value=nil end
        if changed_items or patch.value~=nil or patch.row_height~=nil then node.list_reveal=true end
    end
    if geometry then invalidate(ui,node,order) end
    ui.text_pending=true; ui.tooltip=nil
end
local function list_metrics(node)
    local row=node.row_height or 24
    assert(type(row)=="number" and row>0 and row<math.huge,"list row_height must be finite and positive")
    return #(node.items or {}),row
end
local function translate(node,dx,dy)
    node.rect.x,node.rect.y=node.rect.x+dx,node.rect.y+dy
    if node.layout_slot then
        node.layout_slot.x,node.layout_slot.y=node.layout_slot.x+dx,node.layout_slot.y+dy
    end
    for _,child in ipairs(node.children or {}) do if child.visible~=false then translate(child,dx,dy) end end
end
local function layout(ui,node,x,y,w,h,force)
    local slot=node.layout_slot
    if not force and not node.layout_dirty and slot and slot.x==x and slot.y==y and slot.w==w and slot.h==h then return end
    node.layout_slot={x=x,y=y,w=w,h=h}; node.layout_dirty=nil
    local available_w,available_h=w,h
    local theme=ui.theme
    w=math.max(node.min_w or 0,math.min(node.max_w or w,node.w or w))
    h=math.max(node.min_h or 0,math.min(node.max_h or h,node.h or h))
    local anchor=anchors[node.anchor or "top_left"]
    x=x+(available_w-w)*anchor[1]; y=y+(available_h-h)*anchor[2]
    node.rect={x=x+(node.x or 0),y=y+(node.y or 0),w=w,h=h}
    local items=children(node)
    local padding=node.padding or theme.padding
    local gap=node.gap or theme.gap
    local horizontal=node.kind=="scroll" and node.axis=="horizontal"
    local offset=node.kind=="scroll" and (ui.scroll[node.id] or 0) or 0
    local inner_w,inner_h=math.max(0,w-padding*2),math.max(0,h-padding*2)
    local px,py=node.rect.x+padding-(horizontal and offset or 0),
        node.rect.y+padding-(horizontal and 0 or offset)
    local columns=node.columns or 1
    assert(type(columns)=="number" and columns>=1 and columns<math.huge and columns%1==0,"grid columns must be a positive integer")
    local bottom,right,row_height=py,px,0
    for i,child in ipairs(items) do
        local cw,ch,cx,cy=inner_w,child.h or 28,px,py
        if node.kind=="row" then
            cw=child.w or math.max(0,(inner_w-gap*(#items-1))/#items)
            ch=inner_h
        elseif horizontal then
            cw=child.w or 28
            ch=child.h or inner_h
        elseif node.kind=="grid" then
            cw=math.max(0,(inner_w-gap*(columns-1))/columns)
            cx=node.rect.x+padding+((i-1)%columns)*(cw+gap)
        elseif node.kind=="overlay" then ch=inner_h end
        layout(ui,child,cx,cy,cw,ch,force)
        bottom=math.max(bottom,child.rect.y+child.rect.h)
        right=math.max(right,child.rect.x+child.rect.w)
        if node.kind=="row" then px=px+child.rect.w+gap
        elseif horizontal then px=px+child.rect.w+gap
        elseif node.kind=="grid" then
            row_height=math.max(row_height,child.rect.h)
            if i%columns==0 then py=py+row_height+gap; row_height=0 end
        elseif node.kind~="overlay" then py=py+child.rect.h+gap end
    end
    node.content_h=math.max(0,bottom-node.rect.y+offset+padding)
    if horizontal then
        node.content_h=math.max(0,bottom-node.rect.y+padding)
        node.content_w=math.max(0,right-node.rect.x+offset+padding)
    end
    if node.kind=="list" then local count,row=list_metrics(node); node.content_h=count*row end
    if node.kind=="scroll" or node.kind=="list" then
        node.scroll_max=math.max(0,(horizontal and node.content_w or node.content_h)-(horizontal and w or h))
        local position=math.max(0,math.min(node.scroll_max,ui.scroll[node.id] or 0))
        ui.scroll[node.id]=position
        if node.kind=="scroll" and position~=offset then
            for _,child in ipairs(items) do
                translate(child,horizontal and offset-position or 0,horizontal and 0 or offset-position)
            end
        end
    end
end
function UI.layout(ui,width,height)
    local force=ui.dirty
    local resized=width~=ui.width or height~=ui.height
    ui.width,ui.height=width,height
    if not force and not resized and not ui.layout_pending and not ui.text_pending then return end
    ui.tooltip=nil
    if force or resized or ui.layout_pending then
        if ui.root.visible~=false then layout(ui,ui.root,0,0,width,height,force) end
        if force or ui.order_pending then
            ui.order={}
            local function collect(node)
                if node.visible==false then return end
                ui.order[#ui.order+1]=node
                for _,child in ipairs(node.children or {}) do collect(child) end
            end
            collect(ui.root)
        end
    end
    refresh_text(ui)
    ui.dirty=false; ui.layout_pending=false; ui.order_pending=false; ui.text_pending=false
end
local function scroll_set(ui,node,offset)
    offset=math.max(0,math.min(node.scroll_max or 0,offset))
    if offset==(ui.scroll[node.id] or 0) then return false end
    ui.scroll[node.id]=offset; invalidate(ui,node); return true
end
function UI.scroll_to(ui,id,offset)
    local node=assert(ui.nodes[id],"unknown UI ID")
    assert(node.kind=="scroll" or node.kind=="list","scroll_to requires a scroll container or list")
    assert(type(offset)=="number" and offset==offset and math.abs(offset)<math.huge,"scroll offset must be finite")
    assert(node.rect,"layout the UI before scrolling")
    UI.layout(ui,ui.width,ui.height)
    scroll_set(ui,node,offset); UI.layout(ui,ui.width,ui.height)
    ui.scroll_request=(ui.scroll_request or 0)+1
    return ui.scroll[id]
end
local function reveal(ui,node)
    local parent=node and node.parent
    while parent do
        if parent.kind=="scroll" then
            local padding=parent.padding or ui.theme.padding
            local horizontal=parent.axis=="horizontal"
            local first=horizontal and node.rect.x or node.rect.y
            local last=first+(horizontal and node.rect.w or node.rect.h)
            local start=(horizontal and parent.rect.x or parent.rect.y)+padding
            local finish=start+(horizontal and parent.rect.w or parent.rect.h)-2*padding
            local delta=0
            if first<start then delta=first-start
            elseif last>finish then delta=math.min(first-start,last-finish) end
            if scroll_set(ui,parent,(ui.scroll[parent.id] or 0)+delta) then UI.layout(ui,ui.width,ui.height) end
        end
        parent=parent.parent
    end
end
local function scrollbar(ui,node)
    local maximum=node.scroll_max or 0
    if maximum<=0 or node.rect.h<=0 or node.rect.w<=0 then return end
    local r=node.rect
    if node.kind=="scroll" and node.axis=="horizontal" then
        local width=math.min(r.w,math.max(18,r.w*r.w/(r.w+maximum)))
        return {axis="horizontal",x=r.x,y=r.y+math.max(0,r.h-8),w=r.w,h=math.min(8,r.h),
            thumb_x=r.x+(ui.scroll[node.id] or 0)/maximum*(r.w-width),thumb_w=width}
    end
    local height=math.min(r.h,math.max(18,r.h*r.h/(r.h+maximum)))
    return {axis="vertical",x=r.x+math.max(0,r.w-8),y=r.y,w=math.min(8,r.w),h=r.h,
        thumb_y=r.y+(ui.scroll[node.id] or 0)/maximum*(r.h-height),thumb_h=height}
end
local function visible_point(node,x,y)
    if not inside(node.rect,x,y) then return false end
    local parent=node.parent
    while parent do
        if clips(parent) then if not inside(parent.rect,x,y) then return false end end
        parent=parent.parent
    end
    return true
end
local function visible_rect(ui,node)
    local r=node.rect
    local x,y,right,bottom=math.max(0,r.x),math.max(0,r.y),math.min(ui.width,r.x+r.w),math.min(ui.height,r.y+r.h)
    local parent=node.parent
    while parent do
        if clips(parent) then
            r=parent.rect
            x,y,right,bottom=math.max(x,r.x),math.max(y,r.y),math.min(right,r.x+r.w),math.min(bottom,r.y+r.h)
        end
        parent=parent.parent
    end
    if right>x and bottom>y then return {x=x,y=y,w=right-x,h=bottom-y} end
end
local function drag_axis(scroll,point,first,last,maximum,size,dt)
    local overflow=point<first and point-first or point>last and point-last or 0
    if overflow~=0 and dt>0 then
        local speed=math.min(size*12,size*4+math.abs(overflow)*4)
        scroll=math.max(0,math.min(maximum,scroll+(overflow<0 and -1 or 1)*speed*math.min(dt,.25)))
    end
    return scroll,math.max(first,math.min(last,point))
end
local function update_tooltip(ui,dt,modal,x,y,viewport,bar_pointer,wheel_pointer,was_visible)
    local hint=ui.hint
    local pressed=sc.input.mouse_pressed("left") or sc.input.mouse_pressed("right") or sc.input.mouse_pressed("middle")
    local committed,composition=sc.input.text()
    local focus_input=#committed>0 or #composition>0
    if not focus_input then
        for _,key in ipairs(hint_keys) do if sc.input.key_pressed(key) then focus_input=true; break end end
        if not focus_input then
            for _,button in ipairs(hint_buttons) do if sc.input.gamepad_pressed(button) then focus_input=true; break end end
        end
    end
    if x~=hint.x or y~=hint.y or viewport~=hint.inside or pressed or wheel_pointer then hint.mode="pointer" end
    if focus_input or (ui.focus~=ui.last_focus and not pressed) then hint.mode="focus" end
    hint.x,hint.y,hint.inside=x,y,viewport
    local target
    if hint.mode=="focus" then target=ui.nodes[ui.focus]
    elseif viewport and not bar_pointer then
        for i=#ui.order,1,-1 do
            local node=ui.order[i]
            if (not modal or under(node,modal)) and visible_point(node,x,y) and
                (node.tooltip or interactive[node.kind] or node.background or node.modal) then
                target=node; break -- An overlapping control without a tip still blocks lower tips.
            end
        end
    end
    local anchor=target and enabled(target) and (not modal or under(target,modal)) and visible_rect(ui,target)
    if not anchor or type(target.tooltip)~="string" or target.tooltip=="" then target=nil end
    local owner=target and target.id
    local text=target and target.tooltip
    if owner~=hint.owner or text~=hint.text or hint.modal~=modal or hint.source~=hint.mode then
        hint.owner,hint.text,hint.modal,hint.source=owner,text,modal,hint.mode
        hint.elapsed=0; hint.dismissed=false
    end
    local escape=was_visible and sc.input.key_pressed("escape")
    ui.tooltip=nil
    if escape then hint.dismissed=true end
    if pressed or sc.input.mouse_released("left") or ui.active or ui.scroll_drag or wheel_pointer or
        #committed>0 or #composition>0 then
        hint.elapsed=0; return escape
    end
    if not target or hint.dismissed then return escape end
    tooltip_fields(target); tooltip_fields(ui.theme)
    hint.elapsed=(hint.elapsed or 0)+dt
    if hint.elapsed<(target.tooltip_delay or ui.theme.tooltip_delay or UI.theme.tooltip_delay) then return escape end
    local popup=hint.popup or {kind="label",rect={}}; hint.popup=popup
    local margin=math.min(4,ui.width/4,ui.height/4)
    local width,height=ui.width-margin*2,ui.height-margin*2
    if width<=0 or height<=0 then return escape end
    popup.owner=owner; popup.text=text:gsub("\r\n","\n"):gsub("\r","\n")
    popup.font=target.font or ui.theme.font; popup.font_size=ui.theme.font_size
    popup.rect.w=math.min(width,target.tooltip_width or ui.theme.tooltip_width or UI.theme.tooltip_width)
    local cached=text_layout(ui,popup)
    local natural=0
    for _,position in ipairs(cached.positions) do natural=math.max(natural,position.x+position.w) end
    local r=popup.rect
    r.w=math.min(width,natural+12); r.h=math.min(height,cached.height+8)
    popup.truncated=cached.height+8>r.h
    popup.lines=math.max(0,math.floor((r.h-8)/cached.size))
    if hint.mode=="pointer" then
        r.x,r.y=x+12,y+18
        if r.y+r.h>ui.height-margin then r.y=y-r.h-8 end
    else
        r.x,r.y=anchor.x,anchor.y+anchor.h+6
        if r.y+r.h>ui.height-margin then r.y=anchor.y-r.h-6 end
    end
    r.x=math.max(margin,math.min(ui.width-margin-r.w,r.x))
    r.y=math.max(margin,math.min(ui.height-margin-r.h,r.y))
    ui.tooltip=popup
    return escape
end
local function scrollbar_input(ui,modal,x,y,viewport)
    local drag=ui.scroll_drag
    if drag then
        local node=ui.nodes[drag.id]
        local visible=false
        for _,item in ipairs(ui.order) do if item==node then visible=true; break end end
        local bar=visible and enabled(node) and (not modal or under(node,modal)) and scrollbar(ui,node)
        local horizontal=bar and bar.axis=="horizontal"
        local length=bar and (horizontal and bar.w or bar.h)
        local thumb=bar and (horizontal and bar.thumb_w or bar.thumb_h)
        if bar and length>thumb and (sc.input.mouse_down("left") or sc.input.mouse_released("left")) then
            scroll_set(ui,node,((horizontal and x-bar.x or y-bar.y)-drag.grab)/(length-thumb)*node.scroll_max)
        end
        if not bar or not sc.input.mouse_down("left") then ui.scroll_drag=nil end
        return true -- Keep the release from activating content under the old thumb.
    end
    if not viewport then return false end
    for i=#ui.order,1,-1 do
        local node=ui.order[i]
        if (node.kind=="scroll" or node.kind=="list") and enabled(node) and (not modal or under(node,modal)) then
            local bar=scrollbar(ui,node)
            if bar and inside(bar,x,y) and visible_point(node,x,y) then
                if sc.input.mouse_pressed("left") then
                    ui.active=nil
                    if node.kind=="list" then ui.focus=node.id end
                    local horizontal=bar.axis=="horizontal"
                    local point=horizontal and x or y
                    local first=horizontal and bar.thumb_x or bar.thumb_y
                    local size=horizontal and bar.thumb_w or bar.thumb_h
                    if point>=first and point<first+size then
                        ui.scroll_drag={id=node.id,grab=point-first}
                    else scroll_set(ui,node,(ui.scroll[node.id] or 0)+
                        (point<first and -1 or 1)*(horizontal and bar.w or bar.h)) end
                end
                return true
            end
        end
    end
    return false
end
local function modal_focus(ui)
    local visible={}
    for _,node in ipairs(ui.order) do if node.modal then visible[#visible+1]=node end end
    local stack=ui.modal_stack or {}; ui.modal_stack=stack
    local old_top=stack[#stack] and stack[#stack].node
    local old_focus,old_active=ui.focus,ui.active
    local common=0
    while stack[common+1] and visible[common+1]==stack[common+1].node do common=common+1 end
    for i=#stack,common+1,-1 do
        ui.focus=stack[i].previous; stack[i]=nil; ui.active=nil
    end
    for i=common+1,#visible do
        local modal=visible[i]
        stack[i]={node=modal,previous=ui.focus}; ui.active=nil
        local current=ui.nodes[ui.focus]
        if not current or not under(current,modal) then ui.focus=nil end
    end
    local modal=visible[#visible]
    if modal==old_top then ui.focus=old_focus; ui.active=old_active end
    if modal~=old_top then ui.scroll_drag=nil end
    local valid,first=false,nil
    for _,node in ipairs(ui.order) do
        if interactive[node.kind] and enabled(node) and (not modal or under(node,modal)) then
            first=first or node.id
            if node.id==ui.focus then valid=true end
        end
    end
    if not valid then ui.focus=modal and first or nil end
    return modal
end
local function directional_focus(nodes,current,dx,dy)
    if not current then return nodes[(dx<0 or dy<0) and #nodes or 1] end
    local origin=current.rect
    local best,best_lane,best_distance,best_cross
    for _,node in ipairs(nodes) do
        local r=node.rect
        local forward=dx*(r.x+r.w/2-origin.x-origin.w/2)+dy*(r.y+r.h/2-origin.y-origin.h/2)
        if node~=current and r.w>0 and r.h>0 and forward>0 then
            local low,high,other_low,other_high
            if dx~=0 then low,high,other_low,other_high=origin.y,origin.y+origin.h,r.y,r.y+r.h
            else low,high,other_low,other_high=origin.x,origin.x+origin.w,r.x,r.x+r.w end
            local lane=math.min(high,other_high)>math.max(low,other_low) and 0 or 1
            local cross=math.abs((low+high-other_low-other_high)/2)
            local distance=forward*forward+cross*cross
            if not best or lane<best_lane or (lane==best_lane and
                (distance<best_distance or (distance==best_distance and cross<best_cross))) then
                best,best_lane,best_distance,best_cross=node,lane,distance,cross
            end
        end
    end
    return best or current -- At an edge, keep focus; Tab/shoulders still wrap.
end
local function update_list(ui,node,y,hovered,focused,pad_navigation,spatial)
    local count,row=list_metrics(node)
    node.content_h=count*row; node.scroll_max=math.max(0,node.content_h-node.rect.h)
    local previous=node.value
    assert(previous==nil or (type(previous)=="number" and previous>=1 and previous%1==0),"list value must be a positive item index or nil")
    local value=previous and previous<=count and previous or nil
    local offset=math.max(0,math.min(math.max(0,count*row-node.rect.h),ui.scroll[node.id] or 0))
    local reveal=node.list_reveal
    node.list_reveal=nil
    if focused and count>0 then
        local down=not spatial and (key_pressed(ui,"down") or (pad_navigation and pad_pressed(ui,"dpad_down")))
        local up=not spatial and (key_pressed(ui,"up") or (pad_navigation and pad_pressed(ui,"dpad_up")))
        local page_down,page_up=key_pressed(ui,"page_down"),key_pressed(ui,"page_up")
        if down or up or page_down or page_up then
            local step=(page_down or page_up) and math.max(1,math.floor(node.rect.h/row)) or 1
            value=math.max(1,math.min(count,value and value+((up or page_up) and -step or step) or 1))
            reveal=true
        end
        if sc.input.key_pressed("home") then value=1; reveal=true end
        if sc.input.key_pressed("end") then value=count; reveal=true end
        if hovered and sc.input.mouse_pressed("left") then
            local index=math.floor((y-node.rect.y+offset)/row)+1
            if index>=1 and index<=count then value=index; reveal=true end
        end
    end
    if reveal and value then
        local top,bottom=(value-1)*row,value*row
        if top<offset then offset=top elseif bottom>offset+node.rect.h then offset=bottom-node.rect.h end
    end
    ui.scroll[node.id]=offset
    node.value=value
    local changed=node.list_change_pending; node.list_change_pending=nil
    if changed or value~=previous then emit(ui,node,"change",value) end
    if focused and node.value and (sc.input.key_pressed("enter") or sc.input.key_pressed("space") or sc.input.gamepad_pressed("south")) then
        emit(ui,node,"activate",node.value)
    end
end
function UI.update(ui,dt,width,height,actions)
    assert(type(dt)=="number" and dt>=0 and dt<math.huge,"finite nonnegative UI step required")
    local tooltip_was_visible=ui.tooltip~=nil
    local layout_changed=ui.dirty or ui.layout_pending or width~=ui.width or height~=ui.height
    local scroll_request=ui.scroll_request
    UI.layout(ui,width,height); ui.events={}
    local x,y,viewport=sc.input.mouse()
    local was_active=ui.active
    local starting_focus=ui.focus
    local modal=modal_focus(ui)
    local focusable={}
    for _,node in ipairs(ui.order) do
        if interactive[node.kind] and enabled(node) and (not modal or under(node,modal)) then focusable[#focusable+1]=node end
    end
    local valid_focus,valid_active=false,false
    for _,node in ipairs(focusable) do
        if node.id==ui.focus then valid_focus=true end
        if node.id==ui.active then valid_active=true end
    end
    if not valid_focus then ui.focus=nil end
    if not valid_active then ui.active=nil end
    local focused_before=ui.focus
    local entry=ui.nodes[ui.focus]
    local committed,composition,composition_cursor,composition_start,composition_end,composition_segments,segments_truncated=sc.input.text()
    if #composition>0 and ui.composition_owner==nil then
        ui.composition_owner=entry and entry.kind=="input" and entry.id or false
    end
    if ui.composition_owner~=nil and ui.composition_owner~=ui.focus then ui.composition_owner=false end
    -- Candidate navigation and its final commit/cancel frame belong to the IME.
    local composing=entry and entry.kind=="input" and (#composition>0 or #(entry.composition or "")>0)
    repeat_inputs(ui,dt,composing,modal)
    local tab=not composing and sc.input.key_pressed("tab")
    local advance=tab or sc.input.gamepad_pressed("right_shoulder")
    local retreat=sc.input.gamepad_pressed("left_shoulder") or
        (tab and (sc.input.key_down("left_shift") or sc.input.key_down("right_shift")))
    local spatial=false
    if not composing and (advance or retreat) then
        local index=retreat and 1 or 0
        for i,node in ipairs(focusable) do if node.id==ui.focus then index=i end end
        if #focusable>0 then ui.focus=focusable[((index-1+(retreat and -1 or 1))%#focusable)+1].id end
    elseif not composing and #focusable>0 then
        local text=entry and entry.kind=="input"
        local horizontal=entry and (entry.kind=="slider" or (entry.kind=="tab" and entry.group))
        local vertical=entry and entry.kind=="list"
        local function pressed(key,owned)
            return not owned and ((entry and not text and key_pressed(ui,key)) or pad_pressed(ui,"dpad_"..key))
        end
        local dx=(pressed("right",horizontal) and 1 or 0)-(pressed("left",horizontal) and 1 or 0)
        local dy=(pressed("down",vertical) and 1 or 0)-(pressed("up",vertical) and 1 or 0)
        if dx~=0 or dy~=0 then
            -- One direction per snapshot; horizontal wins simultaneous diagonals.
            if dx~=0 then dy=0 end
            spatial=true; ui.focus=directional_focus(focusable,entry,dx,dy).id
        end
    end
    if layout_changed or ui.focus~=starting_focus or ui.focus~=ui.last_focus then reveal(ui,ui.nodes[ui.focus]) end
    local bar_pointer=scrollbar_input(ui,modal,x,y,viewport)
    UI.layout(ui,width,height)
    local hovered
    if viewport and not bar_pointer then for i=#focusable,1,-1 do if visible_point(focusable[i],x,y) then hovered=focusable[i]; break end end end
    if hovered and sc.input.mouse_pressed("left") then ui.focus=hovered.id; ui.active=hovered.id end
    if ui.focus~=focused_before and ui.composition_owner~=nil then ui.composition_owner=false end
    local current=ui.nodes[ui.focus]
    local space_confirm=not composing and current and clickable[current.kind] and sc.input.key_pressed("space")
    local confirmed=not composing and (sc.input.key_pressed("enter") or sc.input.gamepad_pressed("south") or space_confirm) and current
    local activate=confirmed
    local pointer_capture=ui.active
    if sc.input.mouse_released("left") then
        if hovered and hovered.id==ui.active then activate=hovered end
        ui.active=nil
    end
    if activate then
        if activate.kind=="checkbox" then activate.value=not activate.value; emit(ui,activate,"change",activate.value)
        elseif activate.kind=="button" or activate.kind=="tab" then
            if activate.kind=="tab" and activate.group then UI.select_tab(ui,activate.id) end
            emit(ui,activate,"click",true)
        end
    end
    sc.input.focus_text(false)
    local focused=ui.nodes[ui.focus]
    if not spatial and focused and focused.kind=="tab" and focused.group then
        local left=key_pressed(ui,"left") or pad_pressed(ui,"dpad_left")
        local right=key_pressed(ui,"right") or pad_pressed(ui,"dpad_right")
        if left or right then
            local peers,index={},1
            for _,node in ipairs(focusable) do
                if node.kind=="tab" and node.group==focused.group then
                    peers[#peers+1]=node; if node==focused then index=#peers end
                end
            end
            focused=peers[(index-1+(left and -1 or 1))%#peers+1]
            ui.focus=focused.id; UI.select_tab(ui,focused.id)
        end
    end
    for _,node in ipairs(ui.order) do
        if node.kind=="list" then update_list(ui,node,y,hovered==node,focused==node,focused_before==node.id,spatial) end
    end
    if focused and focused.kind=="slider" then
        control_fields(focused)
        local value=focused.value or 0
        local step=focused.step or .05
        local page=focused.page_step or math.min(1,step*10)
        local left=not spatial and (key_pressed(ui,"left") or pad_pressed(ui,"dpad_left"))
        local right=not spatial and (key_pressed(ui,"right") or pad_pressed(ui,"dpad_right"))
        value=value+((right and 1 or 0)-(left and 1 or 0))*step+
            ((key_pressed(ui,"page_up") and 1 or 0)-(key_pressed(ui,"page_down") and 1 or 0))*page
        if sc.input.key_pressed("home") then value=0 elseif sc.input.key_pressed("end") then value=1 end
        if pointer_capture==focused.id and focused.rect.w>0 and
            (sc.input.mouse_down("left") or sc.input.mouse_pressed("left") or sc.input.mouse_released("left")) then
            value=(x-focused.rect.x)/focused.rect.w
        end
        value=math.max(0,math.min(1,value))
        if value~=focused.value then focused.value=value; emit(ui,focused,"change",value) end
    elseif focused and focused.kind=="input" then
        local e=focused.editor
        if not e or e.value~=(focused.value or "") then e=Edit.new(focused.value); focused.editor=e end
        local shift=sc.input.key_down("left_shift") or sc.input.key_down("right_shift")
        local control=sc.input.key_down("left_control") or sc.input.key_down("right_control")
        local preedit=composition
        if focused_before~=focused.id or (ui.composition_owner~=nil and ui.composition_owner~=focused.id) then committed,preedit="","" end
        committed,preedit=input_text(focused,committed),input_text(focused,preedit)
        focused.composition=preedit
        local function preedit_position(byte)
            return #input_text(focused,composition:sub(1,byte-1))+1
        end
        focused.composition_cursor=preedit_position(composition_cursor)
        focused.composition_start=preedit_position(composition_start)
        focused.composition_end=preedit_position(composition_end)
        local segments,key={},{}
        if #preedit>0 then for _,segment in ipairs(composition_segments) do
            local first,last=preedit_position(segment.start),preedit_position(segment.finish)
            if last>first then
                segments[#segments+1]={start=first,finish=last,kind=segment.kind}
                key[#key+1]=first..":"..last..":"..segment.kind
            end
        end end
        focused.composition_segments=segments
        focused.composition_segments_key=table.concat(key,";")
        focused.composition_segments_truncated=segments_truncated
        local before=text_layout(ui,focused)
        local function locate(px,py)
            local best,line_distance,column_distance=e.cursor,math.huge,math.huge
            local line=math.max(0,math.floor(py/before.size))*before.size
            for _,position in ipairs(before.positions) do
                local dy,dx=math.abs(position.y-line),math.abs(position.x-px)
                if dy<line_distance or (dy==line_distance and dx<column_distance) then
                    best,line_distance,column_distance=position.byte,dy,dx
                end
            end
            return best
        end
        local pointer_press=hovered==focused and sc.input.mouse_pressed("left")
        local pointer_drag=ui.active==focused.id and sc.input.mouse_down("left")
        local pointer_release=was_active==focused.id and sc.input.mouse_released("left")
        if composing or before~=focused.text_goal_layout or ui.last_focus~=focused.id or
            pointer_press or pointer_drag or pointer_release then focused.text_goal_x=nil end
        focused.text_goal_layout=before
        if not composing and (pointer_press or pointer_drag or pointer_release) then
            local px,py=x,y
            if pointer_drag or pointer_release then
                local visible=visible_rect(ui,focused)
                if visible then
                    local step=pointer_drag and dt or 0
                    local left=math.min(visible.x+6,visible.x+visible.w-1)
                    local top=math.min(visible.y+4,visible.y+visible.h-1)
                    if focused.multiline then
                        focused.text_scroll_y,py=drag_axis(focused.text_scroll_y or 0,y,top,
                            visible.y+visible.h-1,math.max(0,before.height-focused.rect.h+8),before.size,step)
                        px=math.max(visible.x,math.min(visible.x+visible.w-1,x))
                    else
                        focused.text_scroll_x,px=drag_axis(focused.text_scroll_x or 0,x,left,
                            visible.x+visible.w-1,math.max(0,before.positions[#before.positions].x-focused.rect.w+20),before.size,step)
                        py=math.max(visible.y,math.min(visible.y+visible.h-1,y))
                    end
                end
            end
            e.cursor=locate(px-focused.rect.x-6+(focused.text_scroll_x or 0),py-focused.rect.y-4+(focused.text_scroll_y or 0))
            if pointer_press and not shift then e.anchor=e.cursor end
        end
        if not composing and not spatial then
            local up,down=key_pressed(ui,"up"),key_pressed(ui,"down")
            local page_up,page_down=key_pressed(ui,"page_up"),key_pressed(ui,"page_down")
            if focused.multiline and (up or down or page_up or page_down) then
                local position=caret_position(before,e.cursor)
                focused.text_goal_x=focused.text_goal_x or position.x
                local rows=(page_up or page_down) and math.max(1,math.floor((focused.rect.h-8)/before.size)) or 1
                e.cursor=locate(focused.text_goal_x,position.y+((up or page_up) and -1 or 1)*rows*before.size)
                if not shift then e.anchor=e.cursor end
            end
            if key_pressed(ui,"left") then Edit.move(e,-1,shift); focused.text_goal_x=nil end
            if key_pressed(ui,"right") then Edit.move(e,1,shift); focused.text_goal_x=nil end
            if sc.input.key_pressed("home") then Edit.home(e,shift,control); focused.text_goal_x=nil end
            if sc.input.key_pressed("end") then Edit.finish(e,shift,control); focused.text_goal_x=nil end
            if key_pressed(ui,"backspace") then Edit.erase(e,-1) end
            if key_pressed(ui,"delete") then Edit.erase(e,1) end
            if control and sc.input.key_pressed("a") then Edit.select_all(e); focused.text_goal_x=nil end
            if control and sc.input.key_pressed("c") then sc.input.clipboard(Edit.selected(e)) end
            if control and sc.input.key_pressed("x") then sc.input.clipboard(Edit.selected(e)); Edit.insert(e,"") end
            if control and sc.input.key_pressed("v") then
                Edit.insert(e,input_text(focused,sc.input.clipboard()),focused.max_bytes)
            end
            if control and sc.input.key_pressed("z") then
                if shift then Edit.redo(e) else Edit.undo(e) end
                focused.text_goal_x=nil
            end
            if control and sc.input.key_pressed("y") then Edit.redo(e); focused.text_goal_x=nil end
            if focused.multiline and #committed==0 and sc.input.key_pressed("enter") then Edit.insert(e,"\n",focused.max_bytes) end
        end
        if #committed>0 and (composing or not control) then Edit.insert(e,committed,focused.max_bytes) end
        if e.value~=focused.value then focused.text_goal_x=nil; focused.value=e.value; emit(ui,focused,"change",e.value) end
    end
    local wheel_x,wheel_y=sc.input.wheel()
    local wheel_pointer=false
    if (wheel_x~=0 or wheel_y~=0) and viewport then
        local target
        for i=#ui.order,1,-1 do
            local node=ui.order[i]
            if (node.kind=="scroll" or node.kind=="list") and enabled(node) and visible_point(node,x,y) and (not modal or under(node,modal)) then target=node; break end
        end
        wheel_pointer=target~=nil
        while target and (not modal or under(target,modal)) do
            local wheel=target.kind=="scroll" and target.axis=="horizontal" and
                (wheel_x~=0 and wheel_x or wheel_y) or wheel_y
            if (target.kind=="scroll" or target.kind=="list") and enabled(target) and wheel~=0 and
                scroll_set(ui,target,(ui.scroll[target.id] or 0)-wheel*32) then break end
            target=target.parent -- An exhausted inner viewport hands the wheel to its parent.
        end
    end
    UI.layout(ui,width,height)
    local final_modal=modal_focus(ui)
    if ui.focus~=focused_before and ui.scroll_request==scroll_request then reveal(ui,ui.nodes[ui.focus]) end
    if entry and ui.focus~=entry.id then
        entry.composition=nil
        if ui.composition_owner~=nil then ui.composition_owner=false end
    end
    if #composition==0 then ui.composition_owner=nil end
    refresh_text(ui)
    local text_focus=ui.nodes[ui.focus]
    if text_focus and text_focus.kind=="input" and text_focus.editor then
        local updated=text_focus.text_layout
        local position=caret_position(updated,updated.preedit_cursor or text_focus.editor.cursor)
        local visible_w=math.max(1,text_focus.rect.w-14)
        local visible_h=math.max(1,text_focus.rect.h-8)
        local sx,sy=text_focus.text_scroll_x or 0,text_focus.text_scroll_y or 0
        if position.x<sx then sx=position.x
        elseif position.x+1>sx+visible_w then sx=position.x+1-visible_w end
        if position.y<sy then sy=position.y
        elseif position.y+updated.size>sy+visible_h then sy=position.y+updated.size-visible_h end
        local content_w=text_focus.multiline and updated.width or updated.positions[#updated.positions].x
        text_focus.text_scroll_x=math.max(0,math.min(sx,content_w-visible_w+1))
        text_focus.text_scroll_y=math.max(0,math.min(sy,updated.height-visible_h))
        sc.input.focus_text(text_focus.rect.x+6+position.x-text_focus.text_scroll_x,
            text_focus.rect.y+4+position.y-text_focus.text_scroll_y+updated.size)
    end
    local tooltip_escape=update_tooltip(ui,dt,final_modal,x,y,viewport,bar_pointer,wheel_pointer,tooltip_was_visible)
    if actions then
        local keys,buttons={},{}
        if tooltip_escape then keys.escape=true end
        if #focusable>0 then
            keys.tab=true; buttons.dpad_up=true; buttons.dpad_down=true
            buttons.dpad_left=true; buttons.dpad_right=true
            buttons.left_shoulder=true; buttons.right_shoulder=true
        end
        if entry or spatial then
            keys.left=true; keys.right=true; keys.up=true; keys.down=true
        end
        if focused or confirmed then keys.enter=true; buttons.south=true end
        if space_confirm or (focused and (clickable[focused.kind] or focused.kind=="list")) then keys.space=true end
        if focused and (focused.kind=="slider" or focused.kind=="input" or (focused.kind=="tab" and focused.group)) then
            keys.left=true; keys.right=true; buttons.dpad_left=true; buttons.dpad_right=true
        end
        if focused and focused.kind=="list" then
            for _,key in ipairs({"up","down","home","end","page_up","page_down"}) do keys[key]=true end
        end
        if focused and focused.kind=="slider" then
            keys.home=true; keys["end"]=true; keys.page_up=true; keys.page_down=true
        end
        Input.consume_sources(actions,{all=modal~=nil or final_modal~=nil,keyboard=focused and focused.kind=="input",
            keys=keys,buttons=buttons,mouse=(hovered or was_active or ui.active or bar_pointer) and {left=true} or nil})
    end
    if ui.focus~=focused_before and not spatial or final_modal~=modal then ui.repeat_hold=nil end
    ui.last_focus=ui.focus
    return {pointer=hovered~=nil or bar_pointer or wheel_pointer or modal~=nil or final_modal~=nil,
        keyboard=focused~=nil or tooltip_escape or modal~=nil or final_modal~=nil,events=ui.events}
end
local function draw_node(ui,node)
    if node.visible==false then return end
    local r,t=node.rect,ui.theme
    local color=node.color or t.text
    local kind=node.kind or "column"
    if node.skin_resource then sc.image(node.skin_resource,r.x,r.y,r.w,r.h,node.skin_options)
    elseif node.background or (kind~="label" and kind~="image" and kind~="row" and kind~="column" and kind~="grid" and kind~="overlay") then
        sc.rect(r.x,r.y,r.w,r.h,node.background or t.panel,true)
    end
    if kind=="tab" and node.group and node.value then
        if node.skin_resource then sc.rect(r.x,r.y+r.h-2,r.w,2,t.accent,true)
        else sc.rect(r.x,r.y,r.w,r.h,t.selection or UI.theme.selection,true) end
    end
    local clipped=clips(node)
    if clipped then sc.clip(r.x,r.y,r.w,r.h) end
    if kind=="image" then sc.image(node.resource,r.x,r.y,r.w,r.h,true)
    elseif kind=="slider" and r.w>0 and r.h>0 then
        local value=math.max(0,math.min(1,node.value or 0))
        local track=math.min(6,r.h)
        sc.rect(r.x,r.y+(r.h-track)/2,r.w,track,t.muted,true)
        sc.rect(r.x,r.y+(r.h-track)/2,r.w*value,track,t.accent,true)
        local w,h=math.min(8,r.w),math.min(16,r.h)
        sc.rect(r.x+(r.w-w)*value,r.y+(r.h-h)/2,w,h,enabled(node) and t.focus or t.muted,true)
    elseif kind=="progress" then
        sc.rect(r.x,r.y+r.h/2-3,r.w*math.max(0,math.min(1,node.value or 0)),6,t.accent,true)
    elseif kind=="list" then
        local count,row=list_metrics(node)
        local offset=ui.scroll[node.id] or 0
        local first=math.floor(offset/row)+1
        local last=math.min(count,first+math.ceil(r.h/row))
        for i=first,last do
            local item=node.items[i]
            if i==node.value then sc.rect(r.x,r.y+(i-1)*row-offset,r.w,row,t.selection or UI.theme.selection,true) end
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
                    local visible=ox+position.x+position.w>=r.x and ox+position.x<r.x+r.w and
                        oy+position.y+cached.size>=r.y and oy+position.y<r.y+r.h
                    if visible and not cached.preedit_first and position.byte>=first and position.byte<last then
                        sc.rect(ox+position.x,oy+position.y,math.max(1,position.w),cached.size,t.selection or UI.theme.selection,true)
                    end
                    if visible and position.composition then
                        if position.targeted then
                            sc.rect(ox+position.x,oy+position.y,math.max(1,position.w),cached.size,t.selection or UI.theme.selection,true)
                        end
                        local kind=position.kind
                        local width=math.max(1,position.w-(position.segment_end and 1 or 0))
                        local baseline=oy+position.y+cached.size-1
                        if kind=="input" then
                            for dx=0,width-1,4 do sc.rect(ox+position.x+dx,baseline,math.min(2,width-dx),1,t.accent,true) end
                        elseif kind~="fixed" then
                            local height=(position.targeted or kind=="error") and 2 or 1
                            local ink=kind=="error" and (t.error or UI.theme.error) or kind=="converted" and t.muted or t.accent
                            sc.rect(ox+position.x,baseline-height+1,width,height,ink,true)
                        end
                    end
                end
            end
            for _,run in ipairs(cached.runs) do
                if oy+run.y+cached.size>=r.y and oy+run.y<r.y+r.h then
                    local ink=run.composition and t.accent or color
                    if run.kind=="converted" then ink=t.text
                    elseif run.kind=="fixed" then ink=t.muted
                    elseif run.kind=="error" then ink=t.error or UI.theme.error end
                    sc.text(run.text,ox+run.x,oy+run.y,cached.size,ink,true,{font=cached.font})
                end
            end
            if kind=="input" and node.editor and ui.focus==node.id then
                local caret=caret_position(cached,cached.preedit_cursor or node.editor.cursor)
                sc.rect(ox+caret.x,oy+caret.y,1,cached.size,t.focus,true)
            end
        end
    end
    for _,child in ipairs(node.children or {}) do draw_node(ui,child) end
    if kind=="scroll" or kind=="list" then
        local bar=scrollbar(ui,node)
        if bar then
            sc.rect(bar.x,bar.y,bar.w,bar.h,t.background,true)
            if bar.axis=="horizontal" then
                sc.rect(bar.thumb_x,bar.y+1,bar.thumb_w,math.max(1,bar.h-2),
                    enabled(node) and t.accent or t.muted,true)
            else
                sc.rect(bar.x+1,bar.thumb_y,math.max(1,bar.w-2),bar.thumb_h,
                    enabled(node) and t.accent or t.muted,true)
            end
        end
    end
    if clipped then sc.clip() end
    if node.id==ui.focus then
        sc.rect(r.x,r.y,r.w,2,t.focus,true); sc.rect(r.x,r.y+r.h-2,r.w,2,t.focus,true)
    end
end
function UI.draw(ui)
    draw_node(ui,ui.root)
    local popup=ui.tooltip
    if not popup then return end
    local r,cached,t=popup.rect,popup.text_layout,ui.theme
    sc.rect(r.x,r.y,r.w,r.h,t.panel,true)
    sc.rect(r.x,r.y,r.w,math.min(2,r.h),t.accent,true)
    sc.clip(r.x,r.y,r.w,r.h)
    for _,run in ipairs(cached.runs) do
        if run.y<(popup.truncated and math.max(0,popup.lines-1)*cached.size or r.h-4) then
            sc.text(run.text,r.x+6+run.x,r.y+4+run.y,cached.size,t.text,true,{font=cached.font})
        end
    end
    if popup.truncated and popup.lines>0 then
        sc.text("...",r.x+6,r.y+4+(popup.lines-1)*cached.size,cached.size,t.muted,true,{font=cached.font})
    end
    sc.clip()
end
function UI.inspect(ui,offset,limit)
    if offset==nil then offset=0 end
    if limit==nil then limit=128 end
    assert(type(offset)=="number" and offset>=0 and offset<math.huge and offset%1==0,"inspect offset must be a nonnegative integer")
    assert(type(limit)=="number" and limit>=1 and limit<=256 and limit%1==0,"inspect limit must be 1..256")
    local nodes=rawget(ui,"authored_nodes")
    local total,out=rawlen(nodes),{}
    local function scalar(value)
        local kind=type(value)
        if kind=="string" or kind=="boolean" then return value end
        if kind=="number" and value==value and math.abs(value)<math.huge then return value end
    end
    for i=offset+1,math.min(total,offset+limit) do
        local node=rawget(nodes,i)
        local id=scalar(rawget(node,"id"))
        local parent=rawget(node,"parent")
        local row={id=id,kind=scalar(rawget(node,"kind")) or "column",
            parent=type(parent)=="table" and scalar(rawget(parent,"id")) or nil,
            focused=rawget(ui,"focus")==id,active=rawget(ui,"active")==id,
            modal=not not rawget(node,"modal"),visible=true,enabled=true,
            layout_pending=not not (rawget(ui,"dirty") or rawget(ui,"layout_pending"))}
        local ancestor=node
        for _=1,total do
            if type(ancestor)~="table" then break end
            row.visible=row.visible and rawget(ancestor,"visible")~=false
            row.enabled=row.enabled and not rawget(ancestor,"disabled")
            ancestor=rawget(ancestor,"parent")
        end
        if ancestor~=nil then row.invalid_parent=true; row.visible=false; row.enabled=false end
        local rect=rawget(node,"rect")
        if row.visible and not row.layout_pending and type(rect)=="table" then
            row.rect={x=scalar(rawget(rect,"x")),y=scalar(rawget(rect,"y")),w=scalar(rawget(rect,"w")),h=scalar(rawget(rect,"h"))}
        end
        row.value=scalar(rawget(node,"value"))
        if type(row.value)=="string" and #row.value>256 then
            local last=256
            -- Do not split a UTF-8 continuation sequence at the preview boundary.
            while last>0 and row.value:byte(last+1)>=128 and row.value:byte(last+1)<192 do last=last-1 end
            row.value_bytes=#row.value; row.value=row.value:sub(1,last); row.value_truncated=true
        end
        row.group=scalar(rawget(node,"group")); row.page=scalar(rawget(node,"page"))
        local items=rawget(node,"items")
        if type(items)=="table" then
            row.item_count=rawlen(items)
            local item=row.value and rawget(items,row.value)
            if type(item)=="table" then row.selected_item_id=scalar(rawget(item,"id")) end
        end
        local scroll=rawget(ui,"scroll")
        row.scroll=type(scroll)=="table" and scalar(rawget(scroll,id)) or nil
        row.scroll_max=scalar(rawget(node,"scroll_max"))
        if row.kind=="scroll" or row.kind=="list" then
            row.scroll_axis=row.kind=="scroll" and (scalar(rawget(node,"axis")) or "vertical") or "vertical"
        end
        local drag=rawget(ui,"scroll_drag")
        row.scroll_capture=type(drag)=="table" and rawget(drag,"id")==id or false
        local popup=rawget(ui,"tooltip")
        row.tooltip_visible=row.visible and not row.layout_pending and type(popup)=="table" and rawget(popup,"owner")==id or false
        if row.tooltip_visible then
            row.tooltip_truncated=not not rawget(popup,"truncated")
            local r=rawget(popup,"rect")
            if type(r)=="table" then row.tooltip_rect={x=scalar(rawget(r,"x")),y=scalar(rawget(r,"y")),w=scalar(rawget(r,"w")),h=scalar(rawget(r,"h"))} end
        end
        local editor=rawget(node,"editor")
        if type(editor)=="table" then row.cursor=scalar(rawget(editor,"cursor")); row.anchor=scalar(rawget(editor,"anchor")) end
        out[#out+1]=row
    end
    local next_offset=offset+#out
    return out,next_offset<total and next_offset or nil,total
end
return UI
