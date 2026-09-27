-- Explicit chunk-object state, with optional joint terrain/region publication.
local Objects={}
local Prefab=require("shiny.prefab")
local active={}
local function plain(value, message)
    assert(type(value)=="table" and getmetatable(value)==nil,message)
    return value
end
local function copy(value, active)
    if type(value)~="table" then
        assert(type(value)=="nil" or type(value)=="boolean" or type(value)=="string"
            or (type(value)=="number" and value==value and math.abs(value)<math.huge),"state requires finite plain data")
        return value
    end
    plain(value,"state cannot have metatables")
    active=active or {}; assert(not active[value],"cyclic state"); active[value]=true
    local result={}
    for key,item in pairs(value) do
        assert(type(key)=="string" or (type(key)=="number" and key%1==0),"invalid state key")
        result[key]=copy(item,active)
    end
    active[value]=nil
    return result
end
local function resolution(entry)
    local current=sc.identity.resolve(entry.object.persistent_id)
    assert(current.status=="deleted" or (current.status=="active" and current.id==entry.id),
        "stream object was unloaded or replaced outside its owner")
    return current
end

-- prepare(object, saved_data) -> entity specification, room-local gameplay data.
-- Preparation/export callbacks must not mutate the world or perform application IO.
local function prepare_owner(chunk,saved,prepare)
    plain(chunk,"chunk must be plain data")
    assert(type(prepare)=="function","object preparation callback required")
    local authored=plain(chunk.objects,"chunk.objects must be an array")
    if saved~=nil then
        plain(saved,"invalid object snapshot")
        assert(saved.format==1,"unsupported object snapshot format")
        plain(saved.objects,"snapshot.objects must be a table")
    end
    local owner={entries={},unloaded=false}
    local drafts,parents,live,names={},{},{},{}
    for i,source in ipairs(authored) do
        local object=copy(source)
        local name=object.persistent_id
        assert(type(name)=="string" and not names[name],"missing or duplicate object persistent_id")
        names[name]=true
        local status=sc.identity.resolve(name).status
        assert(status~="active","stream object already belongs to an active owner")
        local record=saved and saved.objects[name]
        if record~=nil then
            plain(record,"invalid saved object")
            assert((record.deleted==true and record.data==nil)
                or (record.deleted==nil and type(record.data)=="table"),"invalid saved object state")
        end
        local entry={object=object,deleted=status=="deleted" or (record and record.deleted==true) or false}
        owner.entries[i]=entry
        if not entry.deleted then
            local spec,data=prepare(copy(object),record and copy(record.data))
            spec=copy(plain(spec,"prepare must return an entity specification or prefab"))
            local nested=spec.entity~=nil or spec.children~=nil or spec.components~=nil
            local definition=nested and spec or {entity=spec}
            if nested then
                for key in pairs(spec) do
                    assert(key=="entity" or key=="children" or key=="components","unknown streamed prefab field")
                end
                plain(spec.entity,"streamed prefab requires an entity")
            end
            local root=definition.entity
            assert(root.persistent_id==nil or root.persistent_id==name,"prepare cannot replace persistent_id")
            root.persistent_id=name
            local plan=Prefab.plan(definition)
            for j=2,#plan.specs do assert(plan.specs[j].persistent_id==nil,"streamed children belong to the root persistent ID") end
            local first=#drafts+1
            for j,draft in ipairs(plan.specs) do
                drafts[#drafts+1]=draft
                parents[#parents+1]=plan.parents[j]==0 and 0 or first+plan.parents[j]-1
            end
            live[#live+1]={entry=entry,plan=plan,first=first}
            entry.data=data;entry.components=plan.instance.data
            entry.ids=plan.instance.ids;entry.children=plan.instance.children
        end
    end
    for key in pairs(authored) do
        assert(type(key)=="number" and key%1==0 and key>=1 and key<=#owner.entries,"chunk.objects must be dense")
    end
    if saved then
        for name in pairs(saved.objects) do assert(names[name],"snapshot refers to an unknown authored object") end
    end
    return owner,drafts,parents,live
end

local function prepare_load(items,prepare,publication)
    plain(items,"load items must be a plain array")
    if publication then
        plain(publication,"publication must be a plain object")
        for name in pairs(publication) do
            assert(name=="terrain" or name=="navigation" or name=="region","unknown publication field")
        end
        plain(publication.terrain,"publication requires prepared terrain")
        if publication.navigation then plain(publication.navigation,"navigation must be plain data") end
        if publication.region then plain(publication.region,"region must be an interest-region owner") end
    end
    assert(type(prepare)=="function","object preparation callback required")
    local count=0
    for i,item in ipairs(items) do plain(item,"load item requires chunk and optional saved state"); count=i end
    for key in pairs(items) do
        assert(type(key)=="number" and key%1==0 and key>=1 and key<=count,"load items must be dense")
    end
    local owners,drafts,parents,live,names={},{},{},{},{}
    for i,item in ipairs(items) do
        local owner,specs,relations,entries=prepare_owner(item.chunk,item.saved,prepare)
        owners[i]=owner
        for _,entry in ipairs(owner.entries) do
            local name=entry.object.persistent_id
            assert(not names[name],"duplicate object ownership in load batch")
            names[name]=true
        end
        local offset=#drafts
        for n,spec in ipairs(specs) do
            drafts[#drafts+1]=spec
            parents[#parents+1]=relations[n]==0 and 0 or relations[n]+offset
        end
        for _,link in ipairs(entries) do link.first=link.first+offset;live[#live+1]=link end
    end
    -- Discovery may leave unloaded IDs on failure, but never partial active objects.
    -- Reserve identity capacity for saved deletion markers as well as live objects.
    for _,owner in ipairs(owners) do
        for _,entry in ipairs(owner.entries) do
            assert(sc.identity.resolve(entry.object.persistent_id).status~="active","prepare changed object ownership")
        end
    end
    for _,owner in ipairs(owners) do
        for _,entry in ipairs(owner.entries) do sc.identity.declare(entry.object.persistent_id) end
    end
    return owners,drafts,parents,live
end
local function child_path(path)
    if path==nil then return {} end
    plain(path,"child path must be a plain array")
    assert(#path<=32,"child path exceeds prefab depth")
    local result={}
    for i,name in ipairs(path) do
        assert(type(name)=="string" and #name>0,"child path requires nonempty names")
        result[i]=name
    end
    for key in pairs(path) do
        assert(type(key)=="number" and key%1==0 and key>=1 and key<=#result,"child path must be dense")
    end
    return result
end

-- A plain reference survives chunk unload and room reconstruction; it never stores a handle.
function Objects.reference(entry,path)
    assert(type(entry)=="table" and type(entry.object)=="table" and entry.id,
        "stream object entry required")
    assert(resolution(entry).status=="active","stream object is not active")
    local names=child_path(path)
    local node=entry
    for _,name in ipairs(names) do
        node=assert(node.children and node.children[name],"unknown streamed child")
    end
    sc.get(node.id)
    local ref=sc.identity.reference(entry.id)
    ref.children=names
    return ref
end
function Objects.resolve(ref)
    plain(ref,"stream object reference must be plain data")
    for key in pairs(ref) do
        assert(key=="room" or key=="persistent_id" or key=="children","unknown stream object reference field")
    end
    local names=child_path(ref.children)
    local current=sc.identity.resolve{room=ref.room,persistent_id=ref.persistent_id}
    if current.status~="active" then return {status=current.status} end
    local entry=active[ref.persistent_id]
    if not entry or entry.id~=current.id then return {status="unmanaged"} end
    local node=entry
    for _,name in ipairs(names) do
        node=node.children and node.children[name]
        if not node then return {status="path_missing"} end
    end
    if not pcall(sc.get,node.id) then return {status="stale"} end
    return {status="active",id=node.id}
end
local function publish_load(owners,drafts,parents,live,publication)
    local ids
    if publication then
        if publication.region then
            ids=require("shiny.stream_regions").commit(publication.region,publication.terrain,publication.navigation,drafts,parents)
        else
            local ok
            ok,ids=sc.stream.terrain(publication.terrain,publication.navigation,drafts,parents)
            assert(ok,"terrain publication failed")
        end
    else ids=sc.spawn_many(drafts,parents) end
    for _,link in ipairs(live) do
        Prefab.bind(link.plan,ids,link.first)
        link.entry.id=link.plan.instance.id
        active[link.entry.object.persistent_id]=link.entry
    end
    for _,owner in ipairs(owners) do
        for _,entry in ipairs(owner.entries) do
            if entry.deleted then sc.identity.remove(entry.object.persistent_id) end
        end
    end
    return owners,ids
end
function Objects.load_many(items,prepare,publication)
    local owners,drafts,parents,live=prepare_load(items,prepare,publication)
    return (publish_load(owners,drafts,parents,live,publication))
end
function Objects.load(chunk,saved,prepare,publication)
    return Objects.load_many({{chunk=chunk,saved=saved}},prepare,publication)[1]
end

local function validate_snapshot(owner,snapshot)
    assert(not owner.unloaded,"object owner is unloaded")
    for _,entry in ipairs(owner.entries) do
        local deleted=resolution(entry).status=="deleted"
        assert(deleted==(snapshot.objects[entry.object.persistent_id].deleted==true),"export changed object lifecycle")
        if deleted then
            for _,id in ipairs(entry.ids or {}) do assert(not pcall(sc.get,id),"deleted streamed object has a live child") end
        else sc.get_many(entry.ids) end
    end
end

function Objects.snapshot(owner,export)
    assert(not owner.unloaded,"object owner is unloaded")
    assert(type(export)=="function","object export callback required")
    local result={format=1,objects={}}
    for _,entry in ipairs(owner.entries) do
        local name=entry.object.persistent_id
        local deleted=resolution(entry).status=="deleted"
        if deleted then
            for _,id in ipairs(entry.ids or {}) do assert(not pcall(sc.get,id),"deleted streamed object has a live child") end
            result.objects[name]={deleted=true}
        else
            sc.get_many(entry.ids)
            local data=export(sc.get(entry.id),entry.data,copy(entry.object),entry)
            result.objects[name]={data=copy(plain(data,"export must return explicit state data"))}
        end
    end
    -- Catch callbacks that violated ownership before any disk commit or release.
    validate_snapshot(owner,result)
    return result
end

local function prepare_unload(items,export)
    plain(items,"unload items must be a plain array")
    assert(type(export)=="function","object export callback required")
    local owners,keys,names,batch={},{},{},{}
    local count=0
    for i,item in ipairs(items) do
        plain(item,"unload item requires owner and key")
        local owner=plain(item.owner,"unload owner required")
        assert(not owner.unloaded,"object owner is unloaded")
        assert(type(item.key)=="string" and not keys[item.key],"missing or duplicate chunk save key")
        assert(not owners[owner],"duplicate unload owner")
        owners[owner]=true; keys[item.key]=true; count=i
        batch[i]={owner=owner,key=item.key,extra=item.extra~=nil and copy(plain(item.extra,"chunk extra state must be plain data")) or nil}
        for _,entry in ipairs(owner.entries) do
            local name=entry.object.persistent_id
            assert(not names[name],"duplicate object ownership in unload batch")
            names[name]=true
        end
    end
    for key in pairs(items) do
        assert(type(key)=="number" and key%1==0 and key>=1 and key<=count,"unload items must be dense")
    end
    local changes={}
    for _,item in ipairs(batch) do
        changes[item.key]=Objects.snapshot(item.owner,export)
        changes[item.key].extra=item.extra
    end
    -- An exporter for a later chunk must not invalidate an earlier snapshot.
    for _,item in ipairs(batch) do validate_snapshot(item.owner,changes[item.key]) end
    return batch,changes
end
local function release_owners(batch)
    for _,item in ipairs(batch) do
        for _,entry in ipairs(item.owner.entries) do
            if sc.identity.resolve(entry.object.persistent_id).status=="active" then
                for i=#entry.ids,2,-1 do sc.destroy(entry.ids[i]) end
                sc.identity.unload(entry.id)
            end
            if active[entry.object.persistent_id]==entry then active[entry.object.persistent_id]=nil end
        end
        item.owner.unloaded=true
    end
end
-- Delete every visual child before the persistent root; a partial external deletion is rejected.
function Objects.destroy(entry)
    assert(type(entry)=="table" and entry.id and entry.ids,"stream object entry required")
    assert(resolution(entry).status=="active","stream object is not active")
    sc.get_many(entry.ids)
    for i=#entry.ids,1,-1 do sc.destroy(entry.ids[i]) end
    if active[entry.object.persistent_id]==entry then active[entry.object.persistent_id]=nil end
    return true
end
function Objects.save_many(items,slot,export)
    local batch,changes=prepare_unload(items,export)
    if #batch==0 then return true end
    return sc.save.write_chunks(slot,changes)
end
-- Detached explicit data for an asynchronous checkpoint; keeps every owner active.
function Objects.changes(items,export)
    local _,changes=prepare_unload(items,export)
    return changes
end
function Objects.unload_many(items,slot,export)
    local batch,changes=prepare_unload(items,export)
    if #batch>0 then
        local ok,err=sc.save.write_chunks(slot,changes)
        if not ok then return nil,err end
    end
    release_owners(batch)
    return true
end
function Objects.prepare_transition(leaving,entering,prepare,export,publication)
    plain(publication,"transition requires a publication")
    local batch,changes=prepare_unload(leaving,export)
    local owners,drafts,parents,live=prepare_load(entering,prepare,publication)
    -- Preparation callbacks must not invalidate already exported objects.
    for _,item in ipairs(batch) do validate_snapshot(item.owner,changes[item.key]) end
    return {batch=batch,changes=changes,owners=owners,drafts=drafts,parents=parents,live=live,
        ids=false,publication=publication}
end
-- Call only after the prepared changes have been saved. Never replay a committed draft.
function Objects.commit_transition(draft)
    assert(not draft.committed,"object transition already committed")
    for _,item in ipairs(draft.batch) do validate_snapshot(item.owner,draft.changes[item.key]) end
    local _,ids=publish_load(draft.owners,draft.drafts,draft.parents,draft.live,draft.publication)
    draft.ids=ids
    release_owners(draft.batch)
    draft.committed=true
    return draft.owners
end
-- Synchronous utility for explicit callers; stream_world uses the prepared draft asynchronously.
function Objects.transition(leaving,entering,slot,prepare,export,publication)
    local draft=Objects.prepare_transition(leaving,entering,prepare,export,publication)
    if #draft.batch>0 then
        local ok,err=sc.save.write_chunks(slot,draft.changes)
        if not ok then return nil,err end
    end
    return Objects.commit_transition(draft)
end
function Objects.unload(owner,slot,key,export)
    return Objects.unload_many({{owner=owner,key=key}},slot,export)
end
return Objects
