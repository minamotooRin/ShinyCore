local Input=require('shiny.input')
local Controls={}
function Controls.new()
    return Input.new({
        left={{key='a'},{button='dpad_left'},{axis='left_x',direction=-1}},
        right={{key='d'},{button='dpad_right'},{axis='left_x'}},
        up={{key='w'},{button='dpad_up'},{axis='left_y',direction=-1}},
        down={{key='s'},{button='dpad_down'},{axis='left_y'}},
        aim_left={{key='left'},{axis='right_x',direction=-1}},
        aim_right={{key='right'},{axis='right_x'}},
        aim_up={{key='up'},{axis='right_y',direction=-1}},
        aim_down={{key='down'},{axis='right_y'}},
        dash={{key='left_shift'},{button='south'}},
        menu={{key='escape'},{button='start'}},
    },'barrage')
end
function Controls.hint(actions,name,pad)
    for _,source in ipairs(actions.bindings[name] or {}) do
        if (pad and (source.button or source.axis)) or (not pad and (source.key or source.mouse)) then
            return Input.label(source)
        end
    end
    return '-'
end
return Controls
