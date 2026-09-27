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
    assert(options.route_index==nil or type(options.route_index)=="boolean","route_index must be boolean")
    assert(not options.route_index or options.navigation~=false,"route_index requires navigation")
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
        margin=options.margin,capacity=options.capacity,boundary=options.boundary~=false,
        coverage=metadata.object_coverage}
    assert(not options.route_index or region.capacity<1024,"route_index requires capacity at most 1023")
    local view=Tiles.new(metadata,options.images)
    return {region=region,view=view,slot=options.slot,name=options.name,prepare=options.prepare,export=options.export,
        residency=options.residency==true,retain_images=retain,resource_names={},
        navigation=options.navigation~=false,cell_size=options.cell_size,chunks={},owners={},edits={},prepared=Tiles.prepare(view,{}),
        route_index_enabled=options.route_index==true,route_index_dirty=false,route_index_present=false,
        route_index_revision=0,route_index_applied={}}
end
function World.request(world,areas,frame)
    assert(not world.transaction,"finish or cancel the world transaction first")
    local plan=Regions.request(world.region,areas,frame)
    local ok,issue=pcall(function()
        for _,chunk in ipairs(plan.leave) do
            local source=key(chunk.x,chunk.y)
            for _,entry in ipairs(assert(world.owners[source]).entries) do
                if not entry.moved and sc.identity.resolve(entry.object.persistent_id).status=="active" then
                    local entity=sc.get(entry.id)
                    local target=key(math.floor(entity.x/world.region.width),math.floor(entity.y/world.region.height))
                    if target~=source then
                        return "move "..entry.object.persistent_id.." to its loaded chunk before releasing "..source
                    end
                end
            end
        end
    end)
    if not ok or issue then
        Regions.cancel(world.region)
        if not ok then error(issue,0) end
        return nil,issue
    end
    return plan
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
    if transaction and transaction.kind=="transfer" then return nil,"object transfer must finish or retry" end
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
local function route_index_key(world) return world.name..":route-index" end
local function read_route_index(world,saved,record)
    if not world.route_index_enabled then return nil,false end
    local value=saved[route_index_key(world)]
    if not value then
        assert(not record,"saved streamed world lacks route index")
        return {},false
    end
    assert(type(value)=="table" and getmetatable(value)==nil and value.format==1 and
        type(value.modified)=="table" and getmetatable(value.modified)==nil,
        "invalid saved route index")
    local result,count={},0
    for i,name in ipairs(value.modified) do
        assert(i<=16384 and type(name)=="string" and #name<=24 and not result[name],
            "invalid saved route chunk")
        local x,y=name:match("^(%-?%d+):(%-?%d+)$")
        assert(x and y and tonumber(x)>=-31250 and tonumber(x)<=31250 and
            tonumber(y)>=-31250 and tonumber(y)<=31250 and key(tonumber(x),tonumber(y))==name,
            "invalid saved route coordinates")
        result[name]=true; count=i
    end
    for index in pairs(value.modified) do integer(index,1,count,"saved route index") end
    return result,true
end
local function attach_route_index(world,changes)
    if not world.route_index_enabled or world.route_index_present and not world.route_index_dirty then return false end
    assert(world.route_index,"route index must load before saving")
    local names={}
    for name in pairs(world.route_index) do names[#names+1]=name end
    table.sort(names)
    changes[route_index_key(world)]={format=1,modified=names}
    return true
end
local function mark_route_chunks(world,touched)
    if not world.route_index_enabled then return end
    assert(world.route_index,"route index must load before patching")
    local changed=false
    for name in pairs(touched) do
        changed=true
        if not world.route_index[name] then
            world.route_index[name]=true; world.route_index_dirty=true
        end
    end
    if changed then world.route_index_revision=world.route_index_revision+1 end
end
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
        if not entry.moved and current.status=="active" then
            for _,id in ipairs(entry.ids) do add(sc.get(id).sprite) end
        end
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
    if world.route_index_enabled and not world.route_index then
        transaction.route_index,transaction.route_index_present=read_route_index(world,transaction.saved,transaction.record)
    end
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
    local terrain=Tiles.terrain(world.view,prepared,loaded)
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
    if #leaving>0 then transaction.route_index_written=attach_route_index(world,transaction.changes) end
    transaction.navigation=#loaded>0 and navigation or nil
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
            local result=sc.save.result(transaction.request)
            transaction.saved,transaction.record=result.chunks,result.record
        elseif transaction.phase=="write" and transaction.route_index_written then
            world.route_index_dirty=false; world.route_index_present=true
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
        if transaction.transfer then
            local ok,entries=pcall(Objects.commit_transfers,transaction.transfer)
            if not ok then transaction.error=tostring(entries); return nil,transaction.error end
        elseif transaction.draft then
            local ok,incoming=pcall(Objects.commit_transition,transaction.draft)
            if not ok then transaction.error=tostring(incoming); return nil,transaction.error end
            if world.residency then
                for i,spec in ipairs(transaction.draft.drafts) do
                    local sprite=spec.sprite
                    if sprite and sprite~="" then
                        world.resource_names[sc.get(transaction.draft.ids[i]).sprite]=world.resource_names[sprite] or sprite
                    end
                end
            end
            for i,entry in ipairs(transaction.ready) do transaction.owners[key(entry.x,entry.y)]=incoming[i] end
            world.chunks,world.owners,world.edits,world.prepared=transaction.chunks,transaction.owners,transaction.edits,transaction.prepared
            world.nav_region=transaction.navigation
            if transaction.route_index then
                world.route_index=transaction.route_index
                world.route_index_present=transaction.route_index_present
            end
        elseif transaction.kind=="patch" then
            local ok,err=pcall(sc.stream.terrain,transaction.terrain)
            if not ok then transaction.error=tostring(err); return nil,transaction.error end
            world.chunks,world.edits,world.prepared=transaction.chunks,transaction.edits,transaction.prepared
            mark_route_chunks(world,transaction.route_touched)
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
        or transaction.kind=="transfer" and "transferred"
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
    if world.route_index_enabled and not world.route_index then return nil,"route index is not loaded" end
    local items={}
    for i,entry in ipairs(ordered(world.chunks)) do items[i]=save_item(world,entry) end
    local transaction={kind="save",phase="write",changes=Objects.changes(items,world.export)}
    transaction.route_index_written=attach_route_index(world,transaction.changes)
    local request,err=submit_io(world,transaction)
    if not request then return nil,err end
    begin(world,transaction)
    return request
end
-- Migrate all selected objects with one save commit; unchanged objects need no write.
function World.transfer_many(world,names)
    if world.transaction or world.region.pending then return nil,"finish the current world transition first" end
    assert(type(names)=="table" and getmetatable(names)==nil and #names<=4096,
        "transfers require a plain array of at most 4096 IDs")
    local located={}
    for chunk_name,owner in pairs(world.owners) do
        for _,candidate in ipairs(owner.entries) do
            if not candidate.moved then
                local name=candidate.object.persistent_id
                assert(not located[name],"duplicate streamed object owner")
                located[name]={chunk=chunk_name,entry=candidate,owner=owner}
            end
        end
    end
    local moves,touched,seen,count={},{},{},0
    for i,name in ipairs(names) do
        count=i
        assert(type(name)=="string" and #name>0 and not seen[name],"invalid or duplicate object persistent ID")
        seen[name]=true
        local source=located[name]
        if not source or not source.entry.id then return nil,"streamed object is not active: "..name end
        local entity=sc.get(source.entry.id)
        local target_name=key(math.floor(entity.x/world.region.width),math.floor(entity.y/world.region.height))
        if target_name~=source.chunk then
            local target=world.owners[target_name]
            if not target then return nil,"target chunk is not loaded: "..target_name end
            moves[#moves+1]={source=source.owner,target=target,name=name}
            touched[source.chunk]=source.owner;touched[target_name]=target
        end
    end
    for index in pairs(names) do
        assert(type(index)=="number" and index%1==0 and index>=1 and index<=count,
            "transfers must be dense")
    end
    if #moves==0 then return false end
    local function extra(chunk_name)
        return world.edits[chunk_name] and {format=1,tiles=world.edits[chunk_name]} or nil
    end
    local owners,sorted={},{}
    for chunk_name in pairs(touched) do sorted[#sorted+1]=chunk_name end
    table.sort(sorted)
    for i,chunk_name in ipairs(sorted) do
        owners[i]={owner=touched[chunk_name],key=world.name..":"..chunk_name,extra=extra(chunk_name)}
    end
    local draft=Objects.prepare_transfers(moves,owners,world.export)
    local transaction={kind="transfer",phase="write",changes=draft.changes,transfer=draft}
    transaction.route_index_written=attach_route_index(world,transaction.changes)
    local request,err=submit_io(world,transaction)
    if not request then return nil,err end
    begin(world,transaction)
    return request
end
-- Single-object convenience API; both paths share the same batch transaction.
function World.transfer(world,name)
    assert(type(name)=="string" and #name>0,"object persistent ID required")
    return World.transfer_many(world,{name})
end
-- Returns changed,error,event. Events: published/saved/transferred/patched/reloaded/cancelled.
function World.update(world,dt)
    if world.transaction then return finish_transaction(world) end
    Tiles.update(world.view,dt)
    if not world.region.pending then return false end
    local ready=Regions.ready(world.region)
    if not ready then return false end
    local keys={}
    for i,entry in ipairs(ready) do keys[i]=save_key(world,entry) end
    if world.route_index_enabled and not world.route_index then
        assert(#keys<1024,"route index exceeds async read key capacity")
        keys[#keys+1]=route_index_key(world)
    end
    begin(world,{kind="transition",phase=#keys>0 and "read" or "prepare",keys=keys,ready=ready,saved={}})
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
    local loaded=ordered(chunks)
    local prepared=Tiles.prepare(world.view,loaded)
    local terrain=Tiles.terrain(world.view,prepared,loaded)
    if world.residency then
        local transaction={kind="patch",chunks=chunks,edits=edits,prepared=prepared,terrain=terrain,
            route_touched=touched}
        stage_images(world,transaction,image_names(world,prepared,world.owners),"publish")
        if transaction.phase=="images" then
            begin(world,transaction)
            return count,"pending" -- Observe patched/cancelled before changing dependent gameplay state.
        end
    end
    sc.stream.terrain(terrain)
    world.chunks,world.edits,world.prepared=chunks,edits,prepared
    mark_route_chunks(world,touched)
    return count
end
local function navigation_cell(world,x,y)
    assert(type(x)=="number" and type(y)=="number" and x==x and y==y
        and math.abs(x)<=1e6 and math.abs(y)<=1e6,"navigation requires finite world coordinates")
    if not world.navigation then return nil,nil,"disabled" end
    local region=world.nav_region
    if not region or not Regions.contains(world.region,{x=x,y=y}) then return nil,nil,"unloaded" end
    local column=math.floor((x-region.x)/region.cell_size)
    local row=math.floor((y-region.y)/region.cell_size)
    if column<0 or row<0 or column>=#region.rows[1] or row>=#region.rows then return nil,nil,"unloaded" end
    return column,row
end
-- Use the active published grid; pending chunks do not become navigable early.
function World.path(world,sx,sy,gx,gy,budget,radius)
    if budget~=nil then integer(budget,1,1048576,"navigation budget") end
    assert(radius==nil or type(radius)=="number" and radius==radius and radius>=0 and radius<=4096,
        "invalid navigation radius")
    local x,y,reason=navigation_cell(world,sx,sy)
    local target_x,target_y,target_reason=navigation_cell(world,gx,gy)
    if not x then return {status=reason,visited=0,points={}} end
    if not target_x then return {status=target_reason,visited=0,points={}} end
    local result=sc.navigation.path(x,y,target_x,target_y,budget,radius)
    local region=world.nav_region
    for _,point in ipairs(result.points) do
        point.x=region.x+(point.x+.5)*region.cell_size
        point.y=region.y+(point.y+.5)*region.cell_size
    end
    return result
end
function World.flow(world,gx,gy,budget,slot,radius)
    if budget~=nil then integer(budget,1,1048576,"navigation budget") end
    if slot~=nil then integer(slot,1,16,"flow slot") end
    assert(radius==nil or type(radius)=="number" and radius==radius and radius>=0 and radius<=4096,
        "invalid navigation radius")
    local x,y,reason=navigation_cell(world,gx,gy)
    if not x then return nil,reason,0 end
    return sc.navigation.flow(x,y,budget,slot,radius)
end
-- Refresh only confirmed published chunks; pending transitions never enter the graph.
function World.refresh_route(world,route)
    assert(world.navigation and world.nav_region,"route refresh requires published navigation")
    assert(not world.transaction and not world.region.pending,"finish the current world transition before refreshing routes")
    assert(type(route)=="table" and type(route.refresh)=="function" and type(route.data)=="table",
        "route refresh requires a stream route")
    assert(not world.route_index_enabled or type(route.invalidate)=="function",
        "indexed route refresh requires invalidation support")
    assert(route.data.chunk_width==world.region.width and route.data.chunk_height==world.region.height and
        route.data.cell_size==world.nav_region.cell_size,"route scale disagrees with streamed world")
    if world.route_index_enabled then
        assert(world.route_index,"route index is not loaded")
        if world.route_index_applied[route]~=world.route_index_revision then
            local names,chunks={},{}
            for name in pairs(world.route_index) do names[#names+1]=name end
            table.sort(names)
            for _,name in ipairs(names) do
                local x,y=name:match("^(%-?%d+):(%-?%d+)$")
                x,y=tonumber(x),tonumber(y)
                if x>=route.bounds[1] and x<=route.bounds[3] and
                    y>=route.bounds[2] and y<=route.bounds[4] then
                    chunks[#chunks+1]={x=x,y=y}
                end
            end
            route.invalidate(route,chunks)
            world.route_index_applied[route]=world.route_index_revision
        end
    end
    local mask=sc.navigation.mask(route.data.radius)
    local halo=route.data.radius>mask.cell_size/2 and math.ceil(route.data.radius/mask.cell_size) or 0
    local chunks={}
    for _,chunk in ipairs(ordered(world.chunks)) do
        local x=(chunk.x*route.data.chunk_width-mask.x)/mask.cell_size
        local y=(chunk.y*route.data.chunk_height-mask.y)/mask.cell_size
        if chunk.x>=route.bounds[1] and chunk.x<=route.bounds[3] and
            chunk.y>=route.bounds[2] and chunk.y<=route.bounds[4] and
            x>=halo and y>=halo and x+route.data.cells_x+halo<=mask.width and
            y+route.data.cells_y+halo<=mask.height then chunks[#chunks+1]=chunk end
    end
    return route.refresh(route,mask,chunks)
end
-- Hint one route chunk beyond the published region; the ordinary request still owns its commit frame.
function World.prefetch_route(world,result)
    if world.transaction or world.region.pending or not result then
        world.route_prefetch_key=nil
        return nil
    end
    local target
    if result.status=="ok" or result.status=="unverified" then
        for i=2,#result.points do
            local point=result.points[i]
            local x,y=math.floor(point.x/world.region.width),math.floor(point.y/world.region.height)
            if not world.region.active[key(x,y)] then target={x=x,y=y}; break end
        end
    end
    if not target and result.status=="unverified" then target=result.pending end
    if not target or world.region.active[key(target.x,target.y)] then
        world.route_prefetch_key=nil
        return nil
    end
    local name=key(target.x,target.y)
    if world.route_prefetch_key~=name or world.route_prefetch_revision~=result.revision then
        sc.stream.prefetch(target.x,target.y)
        world.route_prefetch_key,world.route_prefetch_revision=name,result.revision
    end
    return target
end
function World.draw(world,camera)
    if not world.residency or world.resident_names then Tiles.draw(world.view,world.prepared,camera) end
end
return World
