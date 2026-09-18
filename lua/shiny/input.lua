-- Resolve actions once per tick, then dispatch contexts from highest priority down.
local Input = {}
function Input.new(bindings,profile)
    if profile then bindings=sc.settings.get().bindings[profile] or bindings end
    return {bindings=bindings,profile=profile,held={},pressed={},released={},consumed={}}
end
function Input.save(input)
    assert(input.profile,"persistent input requires a profile name")
    local bindings=sc.settings.get().bindings
    bindings[input.profile]=input.bindings
    return sc.settings.apply({bindings=bindings})
end
function Input.bind(input, name, sources)
    assert(type(name)=="string" and type(sources)=="table", "action name and sources required")
    input.bindings[name]=sources
end
function Input.update(input)
    input.consumed={}
    for name,sources in pairs(input.bindings) do
        local held,pressed=false,false
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
            else error("unknown binding source") end
        end
        local previous=input.held[name] or false
        input.pressed[name]=(held and not previous) or (pressed and not previous)
        input.released[name]=(previous and not held) or (pressed and not held)
        input.held[name]=held
    end
end
function Input.down(input,name) return not input.consumed[name] and (input.held[name] or false) end
function Input.pressed(input,name) return not input.consumed[name] and (input.pressed[name] or false) end
function Input.released(input,name) return not input.consumed[name] and (input.released[name] or false) end
function Input.consume(input,name) input.consumed[name]=true end
function Input.axis(input, negative, positive)
    return (Input.down(input,positive) and 1 or 0)-(Input.down(input,negative) and 1 or 0)
end
function Input.dispatch(input, contexts)
    for _,context in ipairs(contexts) do
        if context.enabled~=false then context.update(input) end
    end
end
return Input
