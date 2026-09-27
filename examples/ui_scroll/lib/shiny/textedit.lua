-- Positions are one-based UTF-8 byte boundaries; edits never split a grapheme.
local Edit={}
function Edit.new(value)
    value=value or ""
    sc.input.boundaries(value)
    return {value=value,cursor=#value+1,anchor=#value+1,undo={},redo={}}
end
local function save(e)
    e.undo[#e.undo+1]={value=e.value,cursor=e.cursor,anchor=e.anchor}
    if #e.undo>64 then table.remove(e.undo,1) end
    e.redo={}
end
function Edit.selection(e) return math.min(e.cursor,e.anchor),math.max(e.cursor,e.anchor) end
function Edit.selected(e)
    local first,last=Edit.selection(e)
    return e.value:sub(first,last-1)
end
function Edit.insert(e,text,limit)
    sc.input.boundaries(text)
    local first,last=Edit.selection(e)
    if #e.value-(last-first)+#text>(limit or 4096) then return false end
    if first==last and #text==0 then return false end
    save(e)
    e.value=e.value:sub(1,first-1)..text..e.value:sub(last)
    e.cursor=first+#text; e.anchor=e.cursor
    return true
end
function Edit.move(e,direction,selecting)
    if not selecting and e.cursor~=e.anchor then
        local first,last=Edit.selection(e)
        e.cursor=direction<0 and first or last; e.anchor=e.cursor
        return
    end
    local offsets=sc.input.boundaries(e.value)
    local target=e.cursor
    if direction<0 then
        for _,position in ipairs(offsets) do if position<e.cursor then target=position else break end end
    else
        for _,position in ipairs(offsets) do if position>e.cursor then target=position; break end end
    end
    e.cursor=target
    if not selecting then e.anchor=target end
end
function Edit.erase(e,direction)
    if e.cursor==e.anchor then Edit.move(e,direction,true) end
    return Edit.insert(e,"")
end
function Edit.select_all(e) e.anchor=1; e.cursor=#e.value+1 end
function Edit.home(e,selecting,document)
    local prefix=e.value:sub(1,e.cursor-1)
    e.cursor=document and 1 or (prefix:match(".*()\n") or 0)+1
    if not selecting then e.anchor=e.cursor end
end
function Edit.finish(e,selecting,document)
    e.cursor=(not document and e.value:find("\n",e.cursor,true)) or (#e.value+1)
    if not selecting then e.anchor=e.cursor end
end
local function restore(e,source,destination)
    local previous=table.remove(source)
    if not previous then return false end
    destination[#destination+1]={value=e.value,cursor=e.cursor,anchor=e.anchor}
    e.value,e.cursor,e.anchor=previous.value,previous.cursor,previous.anchor
    return true
end
function Edit.undo(e) return restore(e,e.undo,e.redo) end
function Edit.redo(e) return restore(e,e.redo,e.undo) end
return Edit
