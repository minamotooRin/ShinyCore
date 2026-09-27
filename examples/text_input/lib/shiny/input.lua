-- Resolve actions once per tick, then dispatch contexts from highest priority down.
local Input = {}
local axes={left_x=true,left_y=true,right_x=true,right_y=true,left_trigger=true,right_trigger=true}
local function validate_sources(sources)
    assert(type(sources)=="table","action sources must be a table")
    for _,source in ipairs(sources) do
        assert(type(source)=="table","binding source must be a table")
        if source.axis then
            assert(axes[source.axis] and not (source.key or source.mouse or source.button),"invalid axis source")
            assert(source.direction==nil or source.direction==1 or source.direction==-1,"axis direction must be -1 or 1")
            local deadzone=source.deadzone
            assert(deadzone==nil or (type(deadzone)=="number" and deadzone>=0 and deadzone<1),"axis deadzone must be in [0,1)")
            local slot=source.slot
            assert(slot==nil or (type(slot)=="number" and slot%1==0 and slot>=1 and slot<=4),"axis slot must be 1..4")
        end
    end
end
function Input.copy_bindings(bindings)
    local copy={}
    for name,sources in pairs(bindings) do
        validate_sources(sources);copy[name]={}
        for i,source in ipairs(sources) do
            local item={};for key,value in pairs(source) do item[key]=value end
            copy[name][i]=item
        end
    end
    return copy
end
function Input.label(source)
    if source.key then return source.key:gsub('escape','esc'):upper() end
    if source.mouse then return 'MOUSE '..source.mouse:upper() end
    local slot=source.slot and ' @'..source.slot or ''
    if source.button then return source.button:upper()..slot end
    return source.axis:upper()..((source.direction or 1)<0 and '-' or '+')..slot
end
function Input.new(bindings,profile)
    local defaults=Input.copy_bindings(bindings)
    if profile then bindings=sc.settings.get().bindings[profile] or bindings end
    for _,sources in pairs(bindings) do validate_sources(sources) end
    return {bindings=bindings,defaults=defaults,profile=profile,held={},values={},pressed={},released={},consumed={},
        history={game={},ui={}},ui_consumed={},pending_consumed={},ui_blocked={}}
end
function Input.save(input)
    assert(input.profile,"persistent input requires a profile name")
    local bindings=sc.settings.get().bindings
    bindings[input.profile]=input.bindings
    return sc.settings.apply({bindings=bindings})
end
function Input.bind(input, name, sources)
    assert(type(name)=="string" and type(sources)=="table", "action name and sources required")
    validate_sources(sources)
    input.bindings[name]=sources
end
function Input.update(input,context)
    if context==nil then context="game" end
    assert(context=="game" or context=="ui","input context must be game or ui")
    input.context=context
    input.held=input.history[context]
    if context=="ui" then
        input.consumed={}
        input.ui_consumed=input.consumed
    else
        input.consumed=input.pending_consumed
        input.pending_consumed={}
        for name in pairs(input.ui_consumed) do input.consumed[name]=true end
    end
    for name,sources in pairs(input.bindings) do
        local held,pressed,value=false,false,0
        for _,source in ipairs(sources) do
            if source.key then
                held=held or sc.input.key_down(source.key)
                pressed=pressed or sc.input.key_pressed(source.key)
            elseif source.mouse then
                held=held or sc.input.mouse_down(source.mouse)
                pressed=pressed or sc.input.mouse_pressed(source.mouse)
            elseif source.button then
                held=held or sc.input.gamepad_down(source.button,source.slot)
                pressed=pressed or sc.input.gamepad_pressed(source.button,source.slot)
            elseif source.axis then
                value=math.max(value,sc.input.gamepad_axis(source.axis,source.deadzone,source.slot)*(source.direction or 1))
            else error("unknown binding source") end
        end
        input.values[name]=held and 1 or value
        held=held or value>0
        local previous=input.held[name] or false
        input.pressed[name]=(held and not previous) or (pressed and not previous)
        input.released[name]=(previous and not held) or (pressed and not held)
        input.held[name]=held
        if context=="game" and (input.consumed[name] or input.ui_blocked[name]) then
            input.consumed[name]=true
            input.ui_blocked[name]=held or nil -- Hide the matching release even after a menu closes.
        end
    end
end
function Input.down(input,name) return not input.consumed[name] and (input.held[name] or false) end
function Input.value(input,name) return not input.consumed[name] and (input.values[name] or 0) or 0 end
function Input.pressed(input,name) return not input.consumed[name] and (input.pressed[name] or false) end
function Input.released(input,name) return not input.consumed[name] and (input.released[name] or false) end
function Input.consume(input,name)
    input.consumed[name]=true
    -- Accumulate host-frame edges until a fixed update receives them. The latest
    -- UI mask also covers multiple catch-up updates before the next host frame.
    if input.context=="ui" then input.pending_consumed[name]=true
    else input.ui_blocked[name]=input.held[name] or input.ui_blocked[name] end
end
-- Consume named actions backed by sources handled by a higher-priority UI.
-- Matching includes release edges, so closing a modal cannot leak a click-up.
function Input.consume_sources(input,capture)
    for name,sources in pairs(input.bindings) do
        if capture.all then Input.consume(input,name)
        else
            for _,source in ipairs(sources) do
                local handled=false
                if source.key and (capture.keyboard or (capture.keys and capture.keys[source.key])) then
                    handled=sc.input.key_down(source.key) or sc.input.key_pressed(source.key) or sc.input.key_released(source.key)
                elseif source.mouse and capture.mouse and capture.mouse[source.mouse] then
                    handled=sc.input.mouse_down(source.mouse) or sc.input.mouse_pressed(source.mouse) or sc.input.mouse_released(source.mouse)
                elseif source.button and capture.buttons and capture.buttons[source.button] then
                    handled=sc.input.gamepad_down(source.button,source.slot) or sc.input.gamepad_pressed(source.button,source.slot) or sc.input.gamepad_released(source.button,source.slot)
                elseif source.axis and capture.axes and capture.axes[source.axis] then
                    handled=true -- Explicit axis ownership includes its neutral/release snapshot.
                end
                if handled then Input.consume(input,name); break end
            end
        end
    end
end
function Input.axis(input, negative, positive)
    return Input.value(input,positive)-Input.value(input,negative)
end
function Input.dispatch(input, contexts)
    for _,context in ipairs(contexts) do
        if context.enabled~=false then context.update(input) end
    end
end
return Input
