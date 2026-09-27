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
local function merge(defaults,overrides)
    local result=copy(defaults)
    for key,value in pairs(overrides) do
        if type(value)=="table" and type(result[key])=="table" and #value==0 and #result[key]==0 then
            result[key]=merge(result[key],value)
        else result[key]=copy(value) end
    end
    return result
end
function Prefab.merge(defaults,overrides)
    assert(type(defaults)=="table" and (overrides==nil or type(overrides)=="table"),"prefab tables required")
    -- Validate both graphs before merging; overrides must not invoke metamethods either.
    return merge(copy(defaults),copy(overrides or {}))
end
function Prefab.spawn(definition,overrides)
    local data=Prefab.merge(definition,overrides)
    assert(type(data.entity)=="table","prefab requires entity defaults")
    local names={}
    for name in pairs(data.children or {}) do
        assert(type(name)=="string" and name~="","child names must be nonempty strings")
        names[#names+1]=name
    end
    table.sort(names)
    local specs,parents={data.entity},{0}
    local instance={data=data.components or {},children={},destroyed=false}
    for _,name in ipairs(names) do
        local child=data.children[name]
        assert(type(child)=="table","child requires entity fields")
        specs[#specs+1]=child;parents[#parents+1]=1
    end
    -- Native preflight covers creation and all relationships in one transaction.
    local ids=sc.spawn_many(specs,parents)
    instance.id=ids[1];instance.ids=ids
    for i,name in ipairs(names) do instance.children[name]={id=ids[i+1]} end
    return instance
end
function Prefab.destroy(instance)
    if instance.destroyed then return false end
    sc.get_many(instance.ids) -- A stale child fails before any live member is destroyed.
    for i=#instance.ids,1,-1 do sc.destroy(instance.ids[i]) end
    instance.destroyed=true
    return true
end
return Prefab
