-- LANTERN / THE QUIET BELOW
-- A complete game is a returned scene table. Coordinates are world pixels;
-- updates happen at 60 Hz. This file deliberately has no external Lua modules.
local player, gate
local collected = 0
local wisps = {}
local coyote, buffer, facing, elapsed = 0, 0, 1, 0
local destination = false
local notes = {440, 554.37, 659.25}
local color = {
    pale = "#eadbb0", gold = "#edb75e", dim = "#78918b",
    teal = "#557d78", shadow = "#112329", ink = "#091318",
}

local function cave()
    local rows = {}
    for y = 0, 26 do
        local row = {}
        for x = 0, 95 do
            local roof = 1 + math.floor((math.sin(x * .42) + 1) * 1.1)
            local solid = x == 0 or x == 95 or y <= roof or y >= 23
            row[#row + 1] = solid and "#" or "."
        end
        rows[#rows + 1] = table.concat(row)
    end
    local function platform(y, x, width)
        local r = rows[y + 1]
        rows[y + 1] = r:sub(1, x) .. string.rep("=", width) .. r:sub(x + width + 1)
    end
    platform(20, 36, 9)
    platform(19, 61, 10)
    return rows
end

local function move(dt)
    local p = sc.get(player)
    coyote = p.grounded and .10 or math.max(0, coyote - dt)
    buffer = sc.pressed("jump") and .12 or math.max(0, buffer - dt)
    local axis = (sc.down("right") and 1 or 0) - (sc.down("left") and 1 or 0)
    local velocity = axis * 104
    if axis ~= 0 then facing = axis end
    local vy = p.vy
    if buffer > 0 and coyote > 0 then
        vy, coyote, buffer = -235, 0, 0
        sc.emit(p.x + 6, p.y + 17, 5, "#729c8855", 16, .3)
        sc.tone(185, .045, .04)
    end
    if sc.released("jump") and vy < -100 then vy = -100 end
    local frame = (facing < 0 and 4 or 0)
    if not p.grounded then frame = frame + 3
    elseif axis ~= 0 then frame = frame + math.floor(elapsed * 10) % 3 end
    sc.set(player, {vx = velocity, vy = vy, frame = frame})
    if p.y > 240 then
        sc.set(player, {x = 40, y = 166, vx = 0, vy = 0})
        coyote, buffer = 0, 0
    end
end

local function decoration()
    -- Decorative entities render behind the keeper. They have no collisions.
    local function stone(x, y, w, h, tint, layer, glow)
        sc.spawn({x = x, y = y, w = w, h = h, color = tint, layer = layer or -20, glow = glow or 0})
    end
    -- The distant architecture is deliberately low contrast: it gives the
    -- cavern depth while walkable ledges remain the brightest straight edges.
    for i, x in ipairs({57, 185, 308, 433, 578, 681}) do
        local y = 39 + (i * 13) % 26
        local width = 12 + (i % 3) * 5
        stone(x, y, width, 184 - y, "#18333b", -30)
        stone(x + 2, y + 3, 2, 181 - y, "#24434a", -29)
        stone(x - 4, y - 3, width + 8, 5, "#26434a", -28)
    end
    -- Broken ceremonial rings remain in the stone; tiny gaps keep them from
    -- reading as doors or solid collision surfaces.
    for _, center in ipairs({{209, 105}, {599, 103}}) do
        for i = 0, 19 do
            if i % 7 ~= 0 then
                local angle = i * math.pi * 2 / 20
                local x = math.floor(center[1] + math.cos(angle) * 38)
                local y = math.floor(center[2] + math.sin(angle) * 35)
                stone(x, y, 4, 3, "#315056", -24)
            end
        end
        stone(center[1] - 1, center[2] - 5, 2, 11, "#628c83", -23, 23)
        stone(center[1] - 5, center[2] - 1, 10, 2, "#486e69", -23)
    end
    -- Repeat a few small botanical shapes with authored, reproducible spacing.
    -- They are visual entities, so adding them cannot change the replay route.
    for i = 0, 17 do
        local x = 25 + i * 41
        local length = 14 + (i * 17) % 47
        stone(x, 32, 1, length, "#365950", -18)
        stone(x - 2, 35 + math.floor(length * .4), 5, 2, "#4e7663", -17)
        stone(x, 33 + length, 2, 3, "#8dab77", -16, i % 3 == 0 and 18 or 0)
    end
    for i = 0, 12 do
        local x = 28 + i * 57
        local height = 5 + (i * 7) % 8
        stone(x, 184 - height, 1, height, "#52795f", -13)
        stone(x - 3, 183 - height, 4, 2, "#739673", -12)
        stone(x + 1, 185 - height, 4, 2, "#456e60", -12)
    end
    for _, x in ipairs({94, 216, 413, 625, 680}) do
        sc.spawn({x = x, y = 174, w = 2, h = 10, color = "#45695e", layer = -8})
        sc.spawn({x = x - 3, y = 172, w = 8, h = 3, color = "#609477", layer = -7})
        sc.spawn({x = x - 1, y = 171, w = 4, h = 2, color = "#a2b986", glow = 18, layer = -6})
    end
    for _, x in ipairs({181, 386, 602}) do
        sc.spawn({x = x, y = 142, w = 7, h = 42, color = "#173139", layer = -10})
        sc.spawn({x = x - 2, y = 140, w = 11, h = 3, color = "#24434a", layer = -9})
    end
end

return {
    title = "LANTERN / THE QUIET BELOW", width = 384, height = 216,
    gravity = 600, ambient = .58,
    map = {tile_size = 8, rows = cave(), color = "#203a40", accent = "#5a7c71", background = "#10232c"},
    entities = {
        {tag = "keeper", x = 40, y = 166, w = 12, h = 18, dynamic = true,
         solid = true, gravity = 1, color = "#ffffff", glow = 72,
         sprite = "assets/keeper.png", frame = 0, frame_w = 12, frame_h = 18, layer = 10},
        {tag = "door", x = 708, y = 144, w = 24, h = 40, color = "#243d40", layer = -3},
    },
    init = function()
        player, gate = sc.find("keeper"), sc.find("door")
        sc.camera.follow(player)
        decoration()
        for i, position in ipairs({{151, 164}, {337, 134}, {542, 126}}) do
            local id = sc.spawn({tag = "wisp_" .. i, x = position[1], y = position[2],
                w = 8, h = 10, color = color.gold, glow = 44, layer = 3,
                sprite = "assets/wisp.png", frame_w = 8, frame_h = 10})
            wisps[#wisps + 1] = {id = id, x = position[1], y = position[2], phase = i * 1.8}
        end
        sc.message("Find three lights. Wake the sleeping door.")
    end,
    update = function(dt)
        elapsed = elapsed + dt
        move(dt)
        for _, wisp in ipairs(wisps) do
            if wisp.id then
                sc.set(wisp.id, {y = wisp.y + math.sin(elapsed * 2 + wisp.phase) * 2,
                    frame = math.floor(elapsed * 5 + wisp.phase) % 4})
                if sc.overlap(player, wisp.id) then
                    sc.destroy(wisp.id)
                    wisp.id = nil
                    collected = collected + 1
                    sc.emit(wisp.x + 4, wisp.y + 5, 24, "#ffd486", 38, .85)
                    sc.tone(notes[collected], .18, .10)
                    if collected == 3 then
                        sc.set(gate, {color = "#806c43", glow = 64})
                        sc.message("The door remembers the light. Press E nearby.")
                    else
                        sc.message(collected .. " / 3 lights found")
                    end
                end
            end
        end
        local p = sc.get(player)
        if collected == 3 and math.abs(p.x - 712) < 31 and sc.pressed("action") and not destination then
            destination = true
            sc.tone(880, .3, .08)
            sc.scene("rooms/archive.lua")
        elseif sc.pressed("action") and math.abs(p.x - 712) < 31 then
            sc.message("Three wandering lights will open this door.")
        end
        if sc.tick() % 18 == 0 then
            local x = 40 + sc.random() * 680
            sc.emit(x, 54 + sc.random() * 100, 1, "#84b6a55a", 4, 2)
        end
    end,
    draw = function()
        -- All animated decoration reads elapsed, which only update() changes.
        -- The API intentionally keeps drawing separate from simulation.
        for _, x in ipairs({123, 263, 461, 647}) do
            local length = 15 + (x % 13)
            sc.rect(x, 32, 1, length, "#3b555250")
            sc.circle(x, 34 + length, 1.2, "#abc99580")
        end
        -- The gate is a hand-built little arch, illuminated when complete.
        local trim = collected == 3 and color.gold or "#51655b"
        sc.rect(704, 146, 4, 38, trim)
        sc.rect(732, 146, 4, 38, trim)
        sc.rect(708, 140, 24, 6, trim)
        sc.rect(712, 149, 16, 35, "#0a1a20")
        for i = 1, 3 do
            sc.circle(710 + i * 5, 144, 1.2, i <= collected and "#fff0b4" or "#30443f")
        end
        if collected == 3 then
            sc.rect(716, 153, 1, 28, "#f5c36b9c")
            sc.rect(722, 159, 1, 22, "#f5c36b50")
        end
        sc.rect(12, 10, 119, 34, "#08131bc7", true)
        sc.text("L A N T E R N", 20, 16, 10, color.pale, true)
        sc.text("THE QUIET BELOW", 20, 31, 10, color.dim, true)
        sc.rect(302, 12, 69, 22, "#08131bc7", true)
        for i = 1, 3 do
            sc.circle(315 + (i - 1) * 18, 23, 3, i <= collected and color.gold or "#3e5353", true)
            if i <= collected then sc.circle(315 + (i - 1) * 18, 22, 1, color.pale, true) end
        end
        sc.rect(12, 191, 360, 16, "#08131be6", true)
        sc.text("A/D MOVE   SPACE JUMP   E INTERACT", 20, 194, 10, color.dim, true)
        local p = sc.get(player)
        if math.abs(p.x - 712) < 42 then
            sc.text(collected == 3 and "[E] ENTER THE ARCHIVE" or "THE DOOR NEEDS THREE LIGHTS", 20, 175, 10, color.pale, true)
        elseif elapsed < 7 then
            sc.text("A small light goes a long way.", 18, 70, 10, "#a3b5a9", true)
        end
    end,
}
