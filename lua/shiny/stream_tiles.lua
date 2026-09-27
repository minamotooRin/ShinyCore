-- Prepared tile commands for published chunks. No entities or per-tile callbacks.
local Tiles={}
local alignments={unspecified={0,1},topleft={0,0},top={.5,0},topright={1,0},
    left={0,.5},center={.5,.5},right={1,.5},bottomleft={0,1},bottom={.5,1},bottomright={1,1}}
local function number(value,low,high,name)
    assert(type(value)=="number" and value==value and value>=low and value<=high,"invalid "..name)
    return value
end
local function integer(value,low,high,name)
    number(value,low,high,name); assert(value%1==0,"integer required: "..name); return math.tointeger(value)
end
local function tint(layer)
    local color=layer.tintcolor or "#FFFFFFFF" -- Tiled stores alpha first, unlike sc.image.
    assert(type(color)=="string" and (#color==7 or #color==9) and color:match("^#%x+$"),"invalid layer tintcolor")
    if #color==7 then color="#FF"..color:sub(2) end
    local alpha=math.floor(tonumber(color:sub(2,3),16)*number(layer.opacity or 1,0,1,"opacity")+.5)
    return "#"..color:sub(4):upper()..string.format("%02X",alpha)
end
function Tiles.new(metadata,images)
    assert(metadata.format==3 and metadata.chunk_size==32,"unsupported streamed map format")
    local view={width=integer(metadata.tilewidth,1,256,"tilewidth"),height=integer(metadata.tileheight,1,256,"tileheight"),
        sets={},layers={},time=0,origin_x=number(metadata.parallaxoriginx or 0,-1e6,1e6,"parallax origin x"),
        origin_y=number(metadata.parallaxoriginy or 0,-1e6,1e6,"parallax origin y")}
    images=images or {}
    for i,source in ipairs(metadata.tilesets or {}) do
        local set={first=integer(source.firstgid,1,0x0fffffff,"firstgid"),
            count=integer(source.tilecount,1,1048576,"tilecount"),
            columns=integer(source.columns,0,8192,"columns"),
            w=integer(source.tilewidth,1,4096,"tileset tilewidth"),
            h=integer(source.tileheight,1,4096,"tileset tileheight"),
            margin=integer(source.margin or 0,0,8192,"margin"),
            spacing=integer(source.spacing or 0,0,8192,"spacing"),animations={},images={},collisions={}}
        if source.image then
            assert(type(source.image)=="string" and set.columns>0,"atlas requires image path and positive columns")
            set.image=images[source.image] or source.image
            assert(type(set.image)=="string","image mapping must name a declared resource")
        else
            assert(set.columns==0,"image collection columns must be zero")
            set.count=0
            for _,tile in ipairs(source.tiles or {}) do
                local id=integer(tile.id,0,1048575,"collection tileid")
                set.count=math.max(set.count,id+1)
                if tile.image then
                    assert(type(tile.image)=="string" and not set.images[id],"invalid or duplicate tile image")
                    local resource=images[tile.image] or tile.image
                    assert(type(resource)=="string","image mapping must name a declared resource")
                    set.images[id]={image=resource,w=integer(tile.imagewidth,1,4096,"tile image width"),
                        h=integer(tile.imageheight,1,4096,"tile image height")}
                end
            end
            assert(set.count>0,"empty image collection")
        end
        local offset=source.tileoffset or {}
        set.x=number(offset.x or 0,-1e6,1e6,"tile offset x")
        set.y=number(offset.y or 0,-1e6,1e6,"tile offset y")
        local alignment=alignments[source.objectalignment or "unspecified"]
        assert(alignment,"unsupported tile object alignment")
        set.ax,set.ay=alignment[1],alignment[2]
        for _,tile in ipairs(source.tiles or {}) do
            local collision="empty"
            for _,property in ipairs(tile.properties or {}) do
                if property.name=="collision" then collision=property.value end
            end
            assert(collision=="empty" or collision=="solid" or collision=="one_way","unknown tile collision property")
            if tile.objectgroup then
                assert(type(tile.collision_shapes)=="table","tile collision requires rebuilt offline geometry")
                assert(collision~="one_way","object-group collision cannot be one-way")
                set.collisions[tile.id]=tile.collision_shapes
            else set.collisions[tile.id]=collision end
            if tile.animation then
                local animation={duration=0}
                for _,frame in ipairs(tile.animation) do
                    animation.duration=animation.duration+integer(frame.duration,1,3600000,"animation duration")
                    animation[#animation+1]={id=integer(frame.tileid,0,set.count-1,"animation tileid"),finish=animation.duration}
                    assert(set.image or set.images[frame.tileid],"animation frame has no image")
                end
                assert(#animation>0,"empty tile animation")
                set.animations[integer(tile.id,0,set.count-1,"animated tileid")]=animation
            end
        end
        view.sets[i]=set
    end
    table.sort(view.sets,function(a,b) return a.first<b.first end)
    for i=2,#view.sets do assert(view.sets[i].first>=view.sets[i-1].first+view.sets[i-1].count,"overlapping tilesets") end
    for i,layer in ipairs(metadata.layers) do
        assert(layer.type=="tilelayer" or layer.type=="objectgroup" or layer.type=="imagelayer","unsupported stream layer")
        view.layers[i]={tile=layer.type=="tilelayer",object=layer.type=="objectgroup",
            draworder=layer.draworder or "topdown",visible=layer.visible~=false,order=i-1,
            x=number(layer.offsetx or 0,-1e6,1e6,"layer offset x"),y=number(layer.offsety or 0,-1e6,1e6,"layer offset y"),
            px=number(layer.parallaxx or 1,-100,100,"parallax x"),py=number(layer.parallaxy or 1,-100,100,"parallax y"),
            color=tint(layer)}
        if layer.type=="objectgroup" then
            assert(layer.draworder==nil or layer.draworder=="topdown" or layer.draworder=="index","invalid object draworder")
        end
        if layer.type=="imagelayer" then
            local out=view.layers[i]
            assert(type(layer.image)=="string","image layer requires an image")
            out.image=images[layer.image] or layer.image
            assert(type(out.image)=="string","image mapping must name a declared resource")
            out.w=integer(layer.imagewidth,1,8192,"image layer width")
            out.h=integer(layer.imageheight,1,8192,"image layer height")
            for _,field in ipairs({"repeatx","repeaty"}) do
                assert(layer[field]==nil or type(layer[field])=="boolean","image repeat must be boolean")
                out[field]=layer[field]==true
            end
        end
    end
    return view
end
local function graphic(view,gid)
    local id=gid&0x0fffffff
    for i=#view.sets,1,-1 do
        local set=view.sets[i]
        if id>=set.first then
            assert(id<set.first+set.count,"GID outside tileset")
            id=id-set.first
            assert(set.image or set.images[id] or set.animations[id],"GID refers to a missing collection tile")
            return set,id
        end
    end
    error("GID has no tileset")
end
-- Prepare all records before assigning the returned draw list to the active world.
function Tiles.prepare(view,chunks)
    local result,seen={},{}
    for i=1,#view.layers do result[i]={} end
    for _,chunk in ipairs(chunks) do
        local cx=integer(chunk.x,-31250,31250,"chunk x")
        local cy=integer(chunk.y,-31250,31250,"chunk y")
        local key=cx..":"..cy; assert(not seen[key],"duplicate drawing chunk"); seen[key]=true
        for index,cells in pairs(chunk.layers) do
            local layer_index=tonumber(index)
            integer(layer_index,0,#view.layers-1,"layer index")
            local layer=view.layers[layer_index+1]
            assert(layer.tile and #cells==1024,"invalid tile layer cells")
            for i,gid in ipairs(cells) do
                integer(gid,0,0xffffffff,"GID")
                if gid~=0 then
                    local set,id=graphic(view,gid)
                    local x=cx*32+(i-1)%32; local y=cy*32+(i-1)//32
                    local list=result[layer_index+1]
                    list[#list+1]={set=set,id=id,gid=gid,column=x,row=y,
                        x=x*view.width+layer.x+set.x,y=y*view.height+layer.y+view.height+set.y}
                end
            end
        end
        for _,object in ipairs(chunk.objects or {}) do
            local layer_index=integer(object.layer,0,#view.layers-1,"object layer")
            local layer=view.layers[layer_index+1]
            assert(layer.object,"tile object requires an object layer")
            if object.gid then
                local set,id=graphic(view,integer(object.gid,1,0xffffffff,"object GID"))
                local image=set.image and set or set.images[id]
                if not image then image=set.images[set.animations[id][1].id] end
                local list=result[layer_index+1]
                list[#list+1]={object=object,set=set,id=id,gid=object.gid,
                    x=number(object.x,-1e6,1e6,"object x"),y=number(object.y,-1e6,1e6,"object y"),
                    w=number(object.width or image.w,.001,4096,"object width"),
                    h=number(object.height or image.h,.001,4096,"object height"),
                    angle=math.rad(number(object.rotation or 0,-1e6,1e6,"object rotation")),
                    draw_order=integer(object.draw_order,0,1048576,"object draw order")}
            end
        end
    end
    for index,list in ipairs(result) do
        local layer=view.layers[index]
        table.sort(list,function(a,b)
            if layer.object then
                if layer.draworder=="topdown" and a.y~=b.y then return a.y<b.y end
                return a.draw_order<b.draw_order
            end
            return a.row==b.row and a.column<b.column or a.row<b.row
        end)
    end
    return result
end
function Tiles.update(view,dt)
    view.time=view.time+number(dt,0,.25,"fixed update dt")*1000
end
-- Include every frame, not only the one selected by the current animation clock.
function Tiles.images(view,prepared)
    local names={}
    for index,list in ipairs(prepared) do
        local layer=view.layers[index]
        if layer.visible then
            if layer.image then names[layer.image]=true end
            for _,tile in ipairs(list) do
                local set=tile.set
                if set.image then names[set.image]=true
                elseif set.animations[tile.id] then
                    for _,frame in ipairs(set.animations[tile.id]) do names[set.images[frame.id].image]=true end
                else names[set.images[tile.id].image]=true end
            end
        end
    end
    local result={}
    for name in pairs(names) do result[#result+1]=name end
    table.sort(result)
    return result
end
-- Explicit CPU geometry preparation; sc.stream.terrain commits the result.
function Tiles.terrain(view,prepared,chunks)
    local shapes={}
    for _,list in ipairs(prepared) do for _,tile in ipairs(list) do
        if not tile.object then
        local set=tile.set
        local collision=set.collisions[tile.id] or "empty"
        if collision~="empty" then
            local image=set.image and set or set.images[tile.id]
            if not image then image=set.images[set.animations[tile.id][1].id] end
            if type(collision)=="table" then
                for _,source in ipairs(collision) do
                    assert(#source==6,"compiled collision must contain triangle vertices")
                    local vertices={}
                    local left,top,right,bottom=math.huge,math.huge,-math.huge,-math.huge
                    for i=1,#source,2 do
                        local u=number(source[i],-1e6,1e6,"collision x")/image.w
                        local v=number(source[i+1],-1e6,1e6,"collision y")/image.h
                        -- Inverse of the renderer's destination-to-source UV mapping.
                        if (tile.gid&0x20000000)~=0 then u,v=v,u end
                        if (tile.gid&0x80000000)~=0 then u=1-u end
                        if (tile.gid&0x40000000)~=0 then v=1-v end
                        local x,y=u*image.w,v*image.h
                        vertices[i],vertices[i+1]=x,y
                        left,top=math.min(left,x),math.min(top,y)
                        right,bottom=math.max(right,x),math.max(bottom,y)
                    end
                    for i=1,#vertices,2 do vertices[i]=vertices[i]-left; vertices[i+1]=vertices[i+1]-top end
                    shapes[#shapes+1]={x=tile.x+left,y=tile.y-image.h+top,w=right-left,h=bottom-top,vertices=vertices}
                end
            else
                local one_way=collision=="one_way"
                assert(not one_way or (tile.gid&0x60000000)==0,"one-way terrain cannot be flipped vertically or diagonally")
                shapes[#shapes+1]={x=tile.x,y=tile.y-image.h,w=image.w,h=image.h,one_way=one_way}
            end
        end
        end
    end end
    for _,chunk in ipairs(chunks or {}) do for _,object in ipairs(chunk.objects or {}) do
        for _,property in ipairs(object.properties or {}) do if property.name=="collision" then
            assert(property.value=="empty" or (property.value=="solid" or property.value=="one_way")
                and object.collision_shapes,"object collision requires rebuilt offline shapes")
        end end
        for _,shape in ipairs(object.collision_shapes or {}) do shapes[#shapes+1]=shape end
    end end
    return shapes
end
-- Loaded coverage only; native navigation rasterizes committed terrain separately.
function Tiles.navigation(view,chunks,cell_size)
    local divisor,other=view.width,view.height
    while other~=0 do divisor,other=other,divisor%other end
    cell_size=integer(cell_size or divisor,1,256,"navigation cell size")
    local cw,ch=view.width*32,view.height*32
    assert(cw%cell_size==0 and ch%cell_size==0,"navigation cells must divide chunk dimensions")
    local loaded={}
    local left,top,right,bottom=math.huge,math.huge,-math.huge,-math.huge
    for _,chunk in ipairs(chunks) do
        local x=integer(chunk.x,-31250,31250,"chunk x")
        local y=integer(chunk.y,-31250,31250,"chunk y")
        local key=x..":"..y; assert(not loaded[key],"duplicate navigation chunk"); loaded[key]=true
        left,top=math.min(left,x*cw),math.min(top,y*ch)
        right,bottom=math.max(right,(x+1)*cw),math.max(bottom,(y+1)*ch)
    end
    assert(left<right and top<bottom,"navigation requires loaded chunks")
    local width,height=(right-left)//cell_size,(bottom-top)//cell_size
    assert(width*height<=16384,"navigation region exceeds 16384 cells")
    local rows={}
    for y=0,height-1 do
        local row={}
        for x=0,width-1 do
            local key=math.floor((left+x*cell_size)/cw)..":"..math.floor((top+y*cell_size)/ch)
            row[x+1]=loaded[key] and "." or "#"
        end
        rows[y+1]=table.concat(row)
    end
    return {x=left,y=top,rows=rows,cell_size=cell_size}
end
local function image_layer(layer,x,y,area)
    if layer.repeatx then x=x+math.floor((area.x-x)/layer.w)*layer.w end
    if layer.repeaty then y=y+math.floor((area.y-y)/layer.h)*layer.h end
    local last_x=layer.repeatx and area.x+area.w or x+1
    local last_y=layer.repeaty and area.y+area.h or y+1
    local row=y
    while row<last_y do
        local column=x
        while column<last_x do
            local left,top=math.max(area.x,column),math.max(area.y,row)
            local right,bottom=math.min(area.x+area.w,column+layer.w),math.min(area.y+area.h,row+layer.h)
            -- Crop before subdivision so images up to 8192 pixels obey draw limits.
            if right>left and bottom>top then
                local dy=top
                while dy<bottom do
                    local h=math.min(4096,bottom-dy); local dx=left
                    while dx<right do
                        local w=math.min(4096,right-dx)
                        sc.image(layer.image,dx,dy,w,h,{layer=layer.order,color=layer.color,
                            source_x=dx-column,source_y=dy-row,source_w=w,source_h=h})
                        dx=dx+w
                    end
                    dy=dy+h
                end
            end
            column=column+layer.w
        end
        row=row+layer.h
    end
end
function Tiles.draw(view,prepared,camera)
    camera=camera or sc.camera.read()
    local cx=number(camera.anchor_x or camera.x,-1e6,1e6,"camera x"); local cy=number(camera.anchor_y or camera.y,-1e6,1e6,"camera y")
    local area=camera.visible
    number(area.x,-2e6,2e6,"visible x"); number(area.y,-2e6,2e6,"visible y")
    number(area.w,.001,131072,"visible width"); number(area.h,.001,131072,"visible height")
    for index,list in ipairs(prepared) do
        local layer=view.layers[index]
        local shift_x=(cx-view.origin_x)*(1-layer.px)
        local shift_y=(cy-view.origin_y)*(1-layer.py)
        if layer.visible and layer.image then
            image_layer(layer,layer.x+shift_x,layer.y+shift_y,area)
        end
        if layer.visible then for _,tile in ipairs(list) do
            if not tile.object or tile.object.visible~=false and sc.identity.resolve(tile.object.persistent_id).status~="deleted" then
            local set=tile.set
            local id=tile.id
            local animation=set.animations[id]
            if animation then
                local at=view.time%animation.duration
                for _,frame in ipairs(animation) do if at<frame.finish then id=frame.id; break end end
            end
            local image=set.image and set or set.images[id]
            local x,y,w,h,angle
            if tile.object then
                w,h,angle=tile.w,tile.h,tile.angle
                local dx,dy=w*(.5-set.ax),h*(.5-set.ay)
                local cosine,sine=math.cos(angle),math.sin(angle)
                x=tile.x+set.x+shift_x+cosine*dx-sine*dy-w*.5
                y=tile.y+set.y+shift_y+sine*dx+cosine*dy-h*.5
            else x,y,w,h,angle=tile.x+shift_x,tile.y-image.h+shift_y,image.w,image.h,0 end
            local radius=angle~=0 and math.sqrt(w*w+h*h)*.5 or 0
            local left,top,right,bottom=x-radius,y-radius,x+w+radius,y+h+radius
            if right>area.x and bottom>area.y and left<area.x+area.w and top<area.y+area.h then
                sc.image(image.image,x,y,w,h,{layer=layer.order,color=layer.color,angle=angle,
                    source_x=set.image and set.margin+(id%set.columns)*(set.w+set.spacing) or 0,
                    source_y=set.image and set.margin+(id//set.columns)*(set.h+set.spacing) or 0,source_w=image.w,source_h=image.h,
                    flip_x=(tile.gid&0x80000000)~=0,flip_y=(tile.gid&0x40000000)~=0,diagonal=(tile.gid&0x20000000)~=0})
            end
            end
        end end
    end
end
return Tiles
