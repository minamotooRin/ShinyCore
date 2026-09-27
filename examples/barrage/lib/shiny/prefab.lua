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
local function indexed(value)
    for key in pairs(value) do if type(key)=="number" then return true end end
    return false
end
local function merge(result,overrides)
    for key,value in pairs(overrides) do
        if type(value)=="table" and type(result[key])=="table"
            and not indexed(value) and not indexed(result[key]) then
            merge(result[key],value)
        else result[key]=value end
    end
    return result
end
function Prefab.merge(defaults,overrides)
    assert(type(defaults)=="table" and (overrides==nil or type(overrides)=="table"),"prefab tables required")
    -- Validate both graphs before merging; overrides must not invoke metamethods either.
    return merge(copy(defaults),copy(overrides or {}))
end
function Prefab.plan(definition,overrides)
    local data=Prefab.merge(definition,overrides)
    assert(type(data.entity)=="table","prefab requires entity defaults")
    local specs,parents={data.entity},{0}
    local instance={data=data.components or {},children={},destroyed=false}
    local slots={}
    local function append(children,parent,into,depth)
        assert(type(children)=="table","children must be a named table")
        assert(depth<=32,"prefab attachment depth exceeds 32")
        local names={}
        for name in pairs(children) do
            assert(type(name)=="string" and name~="","child names must be nonempty strings")
            names[#names+1]=name
        end
        table.sort(names)
        for _,name in ipairs(names) do
            local child=children[name]
            assert(type(child)=="table","child requires entity fields")
            local nested=child.entity~=nil or child.children~=nil or child.components~=nil
            if nested then
                for key in pairs(child) do
                    assert(key=="entity" or key=="children" or key=="components","unknown child wrapper field")
                end
                assert(type(child.entity)=="table","nested child requires entity fields")
                assert(child.components==nil or type(child.components)=="table","child components must be a table")
            end
            local index=#specs+1
            specs[index]=nested and child.entity or child;parents[index]=parent
            local node={id=0,data=nested and (child.components or {}) or {},children={}}
            into[name]=node;slots[index]=node
            if nested and child.children~=nil then append(child.children,index,node.children,depth+1) end
        end
    end
    append(data.children or {},1,instance.children,1)
    instance.id=0;instance.ids={}
    for i=1,#specs do instance.ids[i]=0 end
    return {specs=specs,parents=parents,instance=instance,slots=slots,bound=false}
end
function Prefab.bind(plan,ids,first)
    first=first or 1
    assert(not plan.bound and #ids>=first+#plan.specs-1,"prefab binding requires complete batch IDs")
    local instance=plan.instance
    for i=1,#plan.specs do instance.ids[i]=ids[first+i-1] end
    instance.id=instance.ids[1]
    for i=2,#plan.specs do plan.slots[i].id=instance.ids[i] end
    plan.bound=true
    return instance
end
function Prefab.spawn(definition,overrides)
    local plan=Prefab.plan(definition,overrides)
    -- Native preflight covers creation and all relationships in one transaction.
    return Prefab.bind(plan,sc.spawn_many(plan.specs,plan.parents))
end
function Prefab.destroy(instance)
    if instance.destroyed then return false end
    sc.get_many(instance.ids) -- A stale child fails before any live member is destroyed.
    for i=#instance.ids,1,-1 do sc.destroy(instance.ids[i]) end
    instance.destroyed=true
    return true
end
return Prefab
