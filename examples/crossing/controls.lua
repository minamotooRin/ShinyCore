-- Editable actions, persisted separately from campaign checkpoints.
local Input=require('shiny.input')
local Controls={}
function Controls.new()
    return Input.new({
        left={{key='a'},{key='left'},{button='dpad_left'}},
        right={{key='d'},{key='right'},{button='dpad_right'}},
        jump={{key='space'},{button='south'}},
        use={{key='e'},{button='west'}},
        rescue={{key='r'},{button='north'}},
        menu={{key='escape'},{button='start'}},
        save={{key='f6'}},load={{key='f9'}},
    },'crossing')
end
function Controls.hint(actions,name,pad)
    for _,source in ipairs(actions.bindings[name] or {}) do
        local label=pad and source.button or (not pad and source.key)
        if label then return source.slot and Input.label(source) or label:gsub('dpad_',''):gsub('escape','esc'):upper() end
        if (pad and source.axis) or (not pad and source.mouse) then return Input.label(source) end
    end
    return '-'
end
function Controls.legend(actions,pad)
    local function key(name) return Controls.hint(actions,name,pad) end
    return key('left')..'/'..key('right')..' MOVE  '..key('jump')..' JUMP  '..key('use')..
        ' USE  '..key('menu')..' MENU  '..key('rescue')..' RESCUE'
end
return Controls
