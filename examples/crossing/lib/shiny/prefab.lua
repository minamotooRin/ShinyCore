-- Plain-data composition. Named children merge; array values replace atomically.
local Prefab = {}
local function copy(value, active)
    if type(value) ~= "table" then return value end
    assert(getmetatable(value) == nil, "prefab data cannot have metatables")
    active = active or {}
    assert(not active[value], "cyclic prefab data")
    active[value] = true
    local out = {}
    for key, item in pairs(value) do out[key] = copy(item, active) end
    active[value] = nil
    return out
end
function Prefab.merge(defaults, overrides)
    local result = copy(defaults)
    for key, value in pairs(overrides or {}) do
        if type(value) == "table" and type(result[key]) == "table" and #value == 0 and #result[key] == 0 then
            result[key] = Prefab.merge(result[key], value)
        else result[key] = copy(value) end
    end
    return result
end
function Prefab.spawn(definition, overrides)
    local data = Prefab.merge(definition, overrides)
    assert(data.entity, "prefab requires entity defaults")
    local root = sc.spawn(data.entity)
    local instance = {id=root, data=data.components or {}, children={}}
    local names = {}
    for name in pairs(data.children or {}) do names[#names+1] = name end
    table.sort(names)
    local ok,err=pcall(function()
        for _,name in ipairs(names) do
            local child=copy(data.children[name])
            assert(not child.body or child.body==false,"visual children cannot own rigid bodies")
            child.body=false
            local offset={x=child.x or 0,y=child.y or 0}
            child.x,child.y=(data.entity.x or 0)+offset.x,(data.entity.y or 0)+offset.y
            instance.children[name]={id=sc.spawn(child),offset=offset}
        end
    end)
    if not ok then
        for _,name in ipairs(names) do
            local child=instance.children[name]
            if child then sc.destroy(child.id) end
        end
        sc.destroy(root)
        error(err,0)
    end
    return instance
end
function Prefab.update(instance)
    local root = sc.get(instance.id)
    local cosine, sine = math.cos(root.angle or 0), math.sin(root.angle or 0)
    for _, child in pairs(instance.children) do
        local p=child.offset
        sc.set(child.id, {x=root.x+p.x*cosine-p.y*sine, y=root.y+p.x*sine+p.y*cosine, angle=root.angle})
    end
end
function Prefab.destroy(instance)
    for _, child in pairs(instance.children) do sc.destroy(child.id) end
    sc.destroy(instance.id)
end
return Prefab
