-- One room-level owner of chunk pins. Game world changes precede commit().
local Regions={}
local function integer(value,low,high,name)
    assert(type(value)=="number" and value%1==0 and value>=low and value<=high,name.." outside range")
    return math.tointeger(value)
end
local function key(x,y) return x..":"..y end
local function ordered(set)
    local list={}
    for _,entry in pairs(set) do list[#list+1]=entry end
    table.sort(list,function(a,b) return a.x==b.x and a.y<b.y or a.x<b.x end)
    return list
end
local function bounds(region,area,margin)
    assert(type(area)=="table" and getmetatable(area)==nil,"interest area must be plain data")
    local x,y,w,h=area.x,area.y,area.w or 0,area.h or 0
    for _,n in ipairs({x,y,w,h}) do
        assert(type(n)=="number" and n==n and math.abs(n)<math.huge,"interest area requires finite coordinates")
    end
    assert(type(x)=="number" and type(y)=="number" and w>=0 and h>=0,"invalid interest area")
    local left,top=math.floor(x/region.width),math.floor(y/region.height)
    local right=w==0 and left or math.max(left,math.ceil((x+w)/region.width)-1)
    local bottom=h==0 and top or math.max(top,math.ceil((y+h)/region.height)-1)
    return integer(left-margin,-31250,31250,"chunk x"),integer(top-margin,-31250,31250,"chunk y"),
        integer(right+margin,-31250,31250,"chunk x"),integer(bottom+margin,-31250,31250,"chunk y")
end
function Regions.new(options)
    options=options or {}
    assert(options.boundary==nil or type(options.boundary)=="boolean","boundary must be boolean")
    local coverage={}
    if options.coverage~=nil then
        assert(type(options.coverage)=="table" and getmetatable(options.coverage)==nil,"object coverage must be a plain array")
        assert(#options.coverage<=65536,"object coverage exceeds index capacity")
        for i,item in ipairs(options.coverage) do
            assert(type(item)=="table" and getmetatable(item)==nil and type(item.anchors)=="table"
                and getmetatable(item.anchors)==nil,"invalid object coverage entry")
            local x=integer(item.x,-31250,31250,"coverage x")
            local y=integer(item.y,-31250,31250,"coverage y")
            local name=key(x,y)
            assert(not coverage[name] and #item.anchors<=1024,"duplicate or oversized object coverage")
            local anchors,seen={},{}
            for j,owner in ipairs(item.anchors) do
                assert(type(owner)=="table" and getmetatable(owner)==nil,"invalid object coverage anchor")
                local ax=integer(owner.x,-31250,31250,"anchor x")
                local ay=integer(owner.y,-31250,31250,"anchor y")
                local id=key(ax,ay)
                assert(id~=name and not seen[id],"duplicate or self-referencing object anchor")
                seen[id]=true; anchors[j]={x=ax,y=ay}
            end
            assert(#anchors>0,"object coverage entry requires anchors")
            for index in pairs(item.anchors) do integer(index,1,#anchors,"anchor index") end
            coverage[name]=anchors
        end
        for index in pairs(options.coverage) do integer(index,1,#options.coverage,"coverage index") end
    end
    return {width=32*integer(options.tilewidth,1,256,"tilewidth"),
        height=32*integer(options.tileheight,1,256,"tileheight"),
        margin=integer(options.margin or 1,0,8,"margin"),
        capacity=integer(options.capacity or 256,1,1024,"capacity"),
        boundary=options.boundary==true,walls={},active={},visible={},coverage=coverage}
end

local function boundary_specs(region,set)
    local specs={}
    local function wall(x,y,w,h)
        -- Entity dimensions are bounded at 4096 pixels, including large tiles.
        for dx=0,w-1,4096 do for dy=0,h-1,4096 do
            specs[#specs+1]={x=x+dx,y=y+dy,w=math.min(4096,w-dx),h=math.min(4096,h-dy),
                color="#00000000",tag="shiny.loading-boundary",body={type="static",friction=0}}
        end end
    end
    for _,entry in ipairs(ordered(set)) do
        local x,y=entry.x*region.width,entry.y*region.height
        if not set[key(entry.x-1,entry.y)] then wall(x-2,y,2,region.height) end
        if not set[key(entry.x+1,entry.y)] then wall(x+region.width,y,2,region.height) end
        if not set[key(entry.x,entry.y-1)] then wall(x,y-2,region.width,2) end
        if not set[key(entry.x,entry.y+1)] then wall(x,y+region.height,region.width,2) end
    end
    return specs
end

-- Multiple areas (e.g. player and camera) share one pin for overlapping chunks.
function Regions.request(region,areas,frame)
    assert(not region.pending,"finish or cancel the current region request first")
    integer(frame,sc.tick(),4503599627370495,"commit frame")
    assert(type(areas)=="table" and getmetatable(areas)==nil,"areas must be a plain array")
    local desired,count,area_count={},0,0
    for i,area in ipairs(areas) do
        area_count=i
        local left,top,right,bottom=bounds(region,area,region.margin)
        assert((right-left+1)*(bottom-top+1)<=region.capacity,"interest area exceeds chunk capacity")
        for x=left,right do for y=top,bottom do
            local name=key(x,y)
            if not desired[name] then
                count=count+1; assert(count<=region.capacity,"interest union exceeds chunk capacity")
                desired[name]={x=x,y=y}
            end
        end end
    end
    for index in pairs(areas) do
        integer(index,1,area_count,"area index")
    end
    local visible=desired
    desired={}
    for name,entry in pairs(visible) do desired[name]=entry end
    for name in pairs(visible) do
        for _,anchor in ipairs(region.coverage[name] or {}) do
            local owner=key(anchor.x,anchor.y)
            if not desired[owner] then
                count=count+1; assert(count<=region.capacity,"object anchor coverage exceeds chunk capacity")
                desired[owner]=anchor
            end
        end
    end
    local visible_changed=false
    for name in pairs(visible) do if not region.visible[name] then visible_changed=true; break end end
    if not visible_changed then for name in pairs(region.visible) do
        if not visible[name] then visible_changed=true; break end
    end end
    local entering,leaving={},{}
    for name,entry in pairs(desired) do if not region.active[name] then entering[name]=entry end end
    for name,entry in pairs(region.active) do if not desired[name] then leaving[name]=entry end end
    local plan={enter=ordered(entering),leave=ordered(leaving),desired=desired,visible=visible,
        visible_changed=visible_changed,frame=frame}
    local acquired=0
    local ok,err=pcall(function()
        for i,entry in ipairs(plan.enter) do sc.stream.request(entry.x,entry.y,frame); acquired=i end
    end)
    if not ok then
        for i=1,acquired do local entry=plan.enter[i]; sc.stream.release(entry.x,entry.y) end
        error(err,0)
    end
    region.pending=plan
    return plan
end

-- Returns all entering chunk data together, or nil before publication. No disk wait.
function Regions.ready(region)
    local plan=assert(region.pending,"no pending region request")
    if sc.tick()<plan.frame then return nil end
    if plan.chunks then return plan.chunks end
    local chunks={}
    for i,entry in ipairs(plan.enter) do
        local chunk=sc.stream.get(entry.x,entry.y)
        if not chunk then return nil end
        chunks[i]={x=entry.x,y=entry.y,chunk=chunk}
    end
    plan.chunks=chunks
    return chunks
end

function Regions.commit(region,terrain,navigation,entities,parents)
    local plan=assert(region.pending,"no pending region request")
    assert(plan.chunks,"prepare entering chunks with ready() before committing")
    assert(terrain~=nil or (navigation==nil and entities==nil and parents==nil),"navigation/entity commit requires terrain")
    local incoming={}
    if entities then
        assert(type(entities)=="table" and getmetatable(entities)==nil,"entities must be a plain array")
        for i,spec in ipairs(entities) do incoming[i]=spec end
        for index in pairs(entities) do integer(index,1,#incoming,"entity index") end
    end
    local object_count=#incoming
    local relations
    if parents then
        assert(entities and type(parents)=="table" and getmetatable(parents)==nil,"parents require entities")
        relations={}
        for i,parent in ipairs(parents) do relations[i]=integer(parent,0,object_count,"parent index") end
        assert(#relations==object_count,"parent count must match entities")
        for index in pairs(parents) do integer(index,1,object_count,"parent index") end
    end
    local walls=region.walls
    local replace_walls=region.boundary and plan.visible_changed
    local specs
    if replace_walls then
        sc.get_many(walls)
        specs=boundary_specs(region,plan.visible)
        for _,spec in ipairs(specs) do
            incoming[#incoming+1]=spec
            if relations then relations[#relations+1]=0 end
        end
    end
    local replacement
    if terrain~=nil then
        -- Native preflight covers terrain, navigation and all replacement walls.
        local ok
        ok,replacement=sc.stream.terrain(terrain,navigation,(entities or specs) and incoming or nil,relations)
        assert(ok,"terrain publication failed")
    elseif specs then replacement=sc.spawn_many(specs) end
    if replace_walls then
        for _,id in ipairs(walls) do sc.destroy(id) end
        region.walls={}
        for i=object_count+1,#replacement do region.walls[#region.walls+1]=replacement[i] end
    end
    for _,entry in ipairs(plan.leave) do sc.stream.release(entry.x,entry.y) end
    region.active=plan.desired; region.visible=plan.visible; region.pending=nil
    if entities then
        local ids={}
        for i=1,object_count do ids[i]=replacement[i] end
        return ids
    end
end
function Regions.cancel(region)
    local plan=assert(region.pending,"no pending region request")
    for _,entry in ipairs(plan.enter) do sc.stream.release(entry.x,entry.y) end
    region.pending=nil
end
-- Read-only coverage for gameplay/loading boundaries; does not move rigid bodies.
function Regions.contains(region,area)
    local left,top,right,bottom=bounds(region,area,0)
    if (right-left+1)*(bottom-top+1)>region.capacity then return false end
    for x=left,right do for y=top,bottom do
        if not region.visible[key(x,y)] then return false end
    end end
    return true
end
return Regions
