-- Room-owned orchestration; native publication stays in the existing stream API.
local Regions=require("shiny.stream_regions")
local Tiles=require("shiny.stream_tiles")
local Objects=require("shiny.stream_objects")
local World={}
local function key(x,y) return x..":"..y end
local function ordered(set)
    local result={}
    for _,entry in pairs(set) do result[#result+1]=entry end
    table.sort(result,function(a,b) return a.x==b.x and a.y<b.y or a.x<b.x end)
    return result
end
local function integer(value,low,high,name)
    assert(type(value)=="number" and value%1==0 and value>=low and value<=high,"invalid "..name)
    return math.tointeger(value)
end
local function copy_layers(layers)
    local result={}
    for index,cells in pairs(layers or {}) do
        result[index]={}
        for cell,gid in pairs(cells) do result[index][cell]=gid end
    end
    return result
end
local function set_cell(world,chunk,layer,cell,gid)
    layer=integer(layer,0,#world.view.layers-1,"tile layer")
    assert(world.view.layers[layer+1].tile,"map edits require a tile layer")
    integer(cell,1,1024,"tile index"); integer(gid,0,0xffffffff,"GID")
    local name=tostring(layer)
    if not chunk.layers[name] then
        chunk.layers[name]={}
        for i=1,1024 do chunk.layers[name][i]=0 end
    end
    chunk.layers[name][cell]=gid
end
local function restore_tiles(world,chunk,saved)
    local extra=saved and saved.extra
    if extra==nil then return chunk,nil end
    assert(type(extra)=="table" and extra.format==1 and type(extra.tiles)=="table","invalid streamed tile state")
    local restored={x=chunk.x,y=chunk.y,objects=chunk.objects,layers=copy_layers(chunk.layers)}
    for layer,cells in pairs(extra.tiles) do
        assert(type(layer)=="string" and tostring(tonumber(layer))==layer and type(cells)=="table","invalid saved tile layer")
        for cell,gid in pairs(cells) do
            assert(type(cell)=="string" and tostring(tonumber(cell))==cell,"invalid saved tile index")
            set_cell(world,restored,tonumber(layer),tonumber(cell),gid)
        end
    end
    return restored,copy_layers(extra.tiles)
end
function World.new(options)
    assert(type(options)=="table" and getmetatable(options)==nil,"stream world requires plain options")
    assert(type(options.name)=="string" and #options.name<=64 and options.name:match("^[%w_.%-]+$"),"stream world requires a stable name")
    assert(type(options.slot)=="string" and #options.slot>0,"stream world requires a save slot")
    assert(type(options.prepare)=="function" and type(options.export)=="function","object prepare/export callbacks required")
    assert(options.navigation==nil or type(options.navigation)=="boolean","navigation must be boolean")
    assert(options.boundary==nil or type(options.boundary)=="boolean","boundary must be boolean")
    assert(options.residency==nil or type(options.residency)=="boolean","residency must be boolean")
    assert(options.retain_images==nil or options.residency==true,"retain_images requires residency")
    local retain={}
    if options.retain_images then
        assert(type(options.retain_images)=="table" and getmetatable(options.retain_images)==nil,"retain_images must be a plain array")
        for i,name in ipairs(options.retain_images) do
            assert(i<=128 and type(name)=="string" and #name>0,"invalid retained image")
            retain[i]=name
        end
        for index in pairs(options.retain_images) do integer(index,1,#retain,"retained image index") end
    end
    if options.cell_size~=nil then
        assert(type(options.cell_size)=="number" and options.cell_size%1==0 and options.cell_size>=1 and options.cell_size<=256,"cell_size must be 1..256")
    end
    sc.stream.open(options.index)
    local metadata=sc.stream.metadata()
    local region=Regions.new{tilewidth=metadata.tilewidth,tileheight=metadata.tileheight,
        margin=options.margin,capacity=options.capacity,boundary=options.boundary~=false}
    local view=Tiles.new(metadata,options.images)
    return {region=region,view=view,slot=options.slot,name=options.name,prepare=options.prepare,export=options.export,
        residency=options.residency==true,retain_images=retain,resource_names={},
        navigation=options.navigation~=false,cell_size=options.cell_size,chunks={},owners={},edits={},prepared=Tiles.prepare(view,{})}
end
function World.request(world,areas,frame)
    assert(not world.transaction,"finish or cancel the world transaction first")
    return Regions.request(world.region,areas,frame)
end
function World.status(world)
    local transaction=world.transaction
    if not transaction then return nil end
    local status=transaction.request and sc.save.status(transaction.request)
        or transaction.phase=="images" and transaction.image_request and sc.images.status(transaction.image_request)
        or {status=transaction.error and "failed" or "complete"}
    if status.status=="ready" then status.status="complete" end
    status.kind=transaction.kind; status.phase=transaction.phase; status.cancelling=transaction.cancelled==true
    status.error=transaction.error or status.error
    return status
end
function World.retry(world)
    local transaction=assert(world.transaction,"no world transaction")
    assert(not transaction.cancelled,"world transaction is cancelling")
    if transaction.request then return sc.save.retry(transaction.request) end
    if transaction.phase=="images" and transaction.image_request then return sc.images.retry(transaction.image_request) end
    assert(transaction.error,"world transaction has not failed")
    transaction.error=nil
    return true
end
-- Cancels publication, never an accepted disk write. Actual region release stays in update.
function World.cancel(world)
    local transaction=world.transaction
    if not transaction then Regions.cancel(world.region); return true end
    transaction.cancelled=true
    if transaction.image_request then sc.images.cancel(transaction.image_request); transaction.image_request=nil end
    if transaction.request and sc.save.status(transaction.request).status~="pending" then
        sc.save.release(transaction.request); transaction.request=nil
    end
    return false -- World.update must finish cancellation, preserving main-thread phases.
end
-- Call from ui_update so cancelled IO failures cannot hold the host gate.
function World.ui_update(world)
    local transaction=world.transaction
    if transaction and transaction.cancelled and transaction.request
        and sc.save.status(transaction.request).status=="failed" then
        sc.save.release(transaction.request); transaction.request=nil
    end
end
function World.contains(world,area) return Regions.contains(world.region,area) end
local function save_key(world,entry) return world.name..":"..key(entry.x,entry.y) end
local function save_item(world,entry)
    local name=key(math.tointeger(entry.x),math.tointeger(entry.y))
    return {owner=assert(world.owners[name]),key=world.name..":"..name,
        extra=world.edits[name] and {format=1,tiles=world.edits[name]} or nil}
end
local function image_names(world,prepared,owners,drafts)
    local names={}
    local function add(name) if name and name~="" then names[world.resource_names[name] or name]=true end end
    for _,name in ipairs(Tiles.images(world.view,prepared)) do add(name) end
    for _,name in ipairs(world.retain_images) do add(name) end
    for _,owner in pairs(owners) do for _,entry in ipairs(owner.entries) do
        local current=sc.identity.resolve(entry.object.persistent_id)
        if current.status=="active" then add(sc.get(current.id).sprite) end
    end end
    for _,spec in ipairs(drafts or {}) do add(spec.sprite) end
    local result={}
    for name in pairs(names) do result[#result+1]=name end
    table.sort(result)
    return result
end
local function stage_images(world,transaction,names,next_phase)
    transaction.phase=next_phase
    if not world.residency then return end
    local same=world.resident_names and #world.resident_names==#names
    if same then for i,name in ipairs(names) do if world.resident_names[i]~=name then same=false; break end end end
    if not same then transaction.names=names; transaction.after_images=next_phase; transaction.phase="images" end
end
local function prepare_transition(world,transaction)
    local region,ready=world.region,transaction.ready
    local plan=region.pending
    local chunks,owners,edits={},{},{}
    for name,entry in pairs(plan.desired) do
        chunks[name]=world.chunks[name]; owners[name]=world.owners[name]; edits[name]=world.edits[name]
    end
    local entering={}
    for i,entry in ipairs(ready) do
        local saved=transaction.saved[save_key(world,entry)]
        local name=key(entry.x,entry.y)
        chunks[name],edits[name]=restore_tiles(world,entry.chunk,saved)
        entering[i]={chunk=chunks[name],saved=saved}
    end
    local loaded={}
    for _,entry in ipairs(ordered(plan.desired)) do loaded[#loaded+1]=assert(chunks[key(entry.x,entry.y)]) end
    local prepared=Tiles.prepare(world.view,loaded)
    local terrain=Tiles.terrain(world.view,prepared)
    local navigation
    if world.navigation then
        navigation=#loaded>0 and Tiles.navigation(world.view,loaded,world.cell_size)
            or {x=0,y=0,rows={"#"},cell_size=world.cell_size or 8}
    end
    local leaving={}
    for i,entry in ipairs(plan.leave) do
        leaving[i]=save_item(world,entry)
    end
    local draft=Objects.prepare_transition(leaving,entering,world.prepare,world.export,
        {terrain=terrain,navigation=navigation,region=region})
    transaction.draft,transaction.changes=draft,draft.changes
    transaction.chunks,transaction.owners,transaction.edits,transaction.prepared=chunks,owners,edits,prepared
    stage_images(world,transaction,world.residency and image_names(world,prepared,owners,draft.drafts),#leaving>0 and "write" or "publish")
    transaction.saved=nil
end
local function submit_io(world,transaction)
    local request,err
    if transaction.phase=="read" then request,err=sc.save.read_chunks_async(world.slot,transaction.keys)
    else request,err=sc.save.write_chunks_async(world.slot,transaction.changes) end
    transaction.request=request
    return request,err
end
local function begin(world,transaction)
    transaction.paused=sc.app.paused()
    world.transaction=transaction
    sc.app.pause(true)
end
local function finish_transaction(world)
    local transaction=world.transaction
    if transaction.request then
        local status=sc.save.status(transaction.request)
        if status.status=="pending" then return false end
        if status.status=="failed" and not transaction.cancelled then return nil,status.error end
        if transaction.phase=="read" and not transaction.cancelled then
            transaction.saved=sc.save.result(transaction.request).chunks
        end
        sc.save.release(transaction.request); transaction.request=nil
        transaction.phase=transaction.phase=="read" and "prepare" or "publish"
    end
    if transaction.cancelled then
        if transaction.kind=="transition" then Regions.cancel(world.region) end
    else
        if transaction.error then return nil,transaction.error end
        if transaction.phase=="prepare" then
            local ok,err=pcall(prepare_transition,world,transaction)
            if not ok then transaction.error=tostring(err); return nil,transaction.error end
        end
        if transaction.phase=="images" then
            if not transaction.image_request then
                local ok,request=pcall(sc.images.prepare,transaction.names)
                if not ok then transaction.error=tostring(request); return nil,transaction.error end
                transaction.image_request=request
                return false
            end
            local status=sc.images.status(transaction.image_request)
            if status.status=="pending" then return false end
            if status.status=="failed" then return nil,status.error end
            transaction.phase=transaction.after_images
        end
        if transaction.phase=="read" or transaction.phase=="write" then
            local request,err=submit_io(world,transaction)
            if not request then transaction.error=err; return nil,err end
            return false
        end
        if transaction.draft then
            local ok,incoming=pcall(Objects.commit_transition,transaction.draft)
            if not ok then transaction.error=tostring(incoming); return nil,transaction.error end
            if world.residency then
                for i,entry in ipairs(transaction.draft.live) do
                    local sprite=transaction.draft.drafts[i].sprite
                    if sprite and sprite~="" then world.resource_names[sc.get(entry.id).sprite]=world.resource_names[sprite] or sprite end
                end
            end
            for i,entry in ipairs(transaction.ready) do transaction.owners[key(entry.x,entry.y)]=incoming[i] end
            world.chunks,world.owners,world.edits,world.prepared=transaction.chunks,transaction.owners,transaction.edits,transaction.prepared
        elseif transaction.kind=="patch" then
            local ok,err=pcall(sc.stream.terrain,transaction.terrain)
            if not ok then transaction.error=tostring(err); return nil,transaction.error end
            world.chunks,world.edits,world.prepared=transaction.chunks,transaction.edits,transaction.prepared
        end
        if transaction.image_request then
            sc.images.commit(transaction.image_request); transaction.image_request=nil
            world.resident_names=transaction.names
        end
    end
    world.transaction=nil
    sc.app.pause(transaction.paused)
    return not transaction.cancelled and transaction.kind=="transition",nil,
        transaction.cancelled and "cancelled" or transaction.kind=="save" and "saved"
        or transaction.kind=="patch" and "patched" or transaction.kind=="reload" and "reloaded" or "published"
end
function World.reload_images(world)
    assert(world.residency,"reload_images requires residency")
    if world.transaction or world.region.pending then return nil,"finish the current world transaction before reloading images" end
    local ok,request=pcall(sc.images.reload)
    if not ok then return nil,tostring(request) end
    begin(world,{kind="reload",phase="images",image_request=request,after_images="publish",names=world.resident_names})
    return request
end
function World.save(world)
    if world.transaction or world.region.pending then return nil,"finish the current world transition before saving" end
    local items={}
    for i,entry in ipairs(ordered(world.chunks)) do items[i]=save_item(world,entry) end
    local transaction={kind="save",phase="write",changes=Objects.changes(items,world.export)}
    local request,err=submit_io(world,transaction)
    if not request then return nil,err end
    begin(world,transaction)
    return request
end
-- Returns changed,error,event. Events: published/saved/patched/reloaded/cancelled.
function World.update(world,dt)
    if world.transaction then return finish_transaction(world) end
    Tiles.update(world.view,dt)
    if not world.region.pending then return false end
    local ready=Regions.ready(world.region)
    if not ready then return false end
    local keys={}
    for i,entry in ipairs(ready) do keys[i]=save_key(world,entry) end
    begin(world,{kind="transition",phase=#ready>0 and "read" or "prepare",keys=keys,ready=ready,saved={}})
    return finish_transaction(world)
end
-- Edit loaded global tile coordinates; duplicate cells use the final input value.
function World.patch(world,items)
    assert(not world.transaction,"cannot edit a world during a save/transition")
    assert(type(items)=="table" and getmetatable(items)==nil,"map patch must be a plain array")
    local chunks,edits,touched={},{},{}
    for name,chunk in pairs(world.chunks) do chunks[name]=chunk; edits[name]=world.edits[name] end
    local count=0
    for i,item in ipairs(items) do
        assert(i<=16384 and type(item)=="table" and getmetatable(item)==nil,"invalid map patch entry")
        for field in pairs(item) do assert(field=="x" or field=="y" or field=="layer" or field=="gid","unknown map patch field") end
        local x=integer(item.x,-1e6,1e6,"tile x"); local y=integer(item.y,-1e6,1e6,"tile y")
        local name=key(x//32,y//32)
        local chunk=assert(chunks[name],"cannot edit an unloaded chunk")
        if not touched[name] then
            chunk={x=chunk.x,y=chunk.y,objects=chunk.objects,layers=copy_layers(chunk.layers)}
            chunks[name]=chunk; edits[name]=copy_layers(edits[name]); touched[name]=true
        end
        local cell=(y%32)*32+x%32+1
        set_cell(world,chunk,item.layer,cell,item.gid)
        local layer=tostring(math.tointeger(item.layer))
        edits[name][layer]=edits[name][layer] or {}; edits[name][layer][tostring(cell)]=item.gid
        count=i
    end
    for index in pairs(items) do integer(index,1,count,"patch index") end
    if count==0 then return 0 end
    local prepared=Tiles.prepare(world.view,ordered(chunks))
    local terrain=Tiles.terrain(world.view,prepared)
    if world.residency then
        local transaction={kind="patch",chunks=chunks,edits=edits,prepared=prepared,terrain=terrain}
        stage_images(world,transaction,image_names(world,prepared,world.owners),"publish")
        if transaction.phase=="images" then
            begin(world,transaction)
            return count,"pending" -- Observe patched/cancelled before changing dependent gameplay state.
        end
    end
    sc.stream.terrain(terrain)
    world.chunks,world.edits,world.prepared=chunks,edits,prepared
    return count
end
function World.draw(world,camera)
    if not world.residency or world.resident_names then Tiles.draw(world.view,world.prepared,camera) end
end
return World
