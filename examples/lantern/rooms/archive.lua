-- A second, self-contained scene. sc.scene replaces the complete world;
-- returning to main.lua intentionally starts a fresh collection run.
local keeper
local elapsed, coyote, buffer, facing = 0, 0, 0, 1
local leaving = false
local rows = {}
for y = 0, 26 do
    local cells = {}
    for x = 0, 47 do
        local roof = (x < 8 or x > 40) and 4 or 2
        cells[#cells + 1] = (x == 0 or x == 47 or y <= roof or y >= 23) and "#" or "."
    end
    rows[#rows + 1] = table.concat(cells)
end

return {
    title = "LANTERN / THE STILL ARCHIVE", width = 384, height = 216,
    gravity = 600, ambient = .58,
    map = {tile_size = 8, rows = rows, color = "#20393d", accent = "#718a73", background = "#10262d"},
    entities = {
        {tag = "keeper", x = 63, y = 166, w = 12, h = 18, dynamic = true,
         solid = true, gravity = 1, color = "#ffffff", glow = 72,
         sprite = "assets/keeper.png", frame_w = 12, frame_h = 18, layer = 10},
        {tag = "return", x = 26, y = 144, w = 23, h = 40, color = "#7c7752", glow = 64, layer = -5},
        {tag = "memory", x = 225, y = 92, w = 12, h = 15, color = "#eac984", glow = 112,
         sprite = "assets/wisp.png", frame_w = 8, frame_h = 10, layer = -2},
    },
    init = function()
        keeper = sc.find("keeper")
        sc.camera(0, 0)
        sc.message("Some things grow where no sun has ever been.")
        local function detail(x, y, w, h, color, layer, glow)
            sc.spawn({x = x, y = y, w = w, h = h, color = color, layer = layer, glow = glow or 0})
        end
        for _, x in ipairs({84, 167, 280, 343}) do
            detail(x, 52, 13, 132, "#234047", -30)
            detail(x - 3, 49, 19, 4, "#345459", -29)
            detail(x + 2, 56, 1, 126, "#3a5858", -28)
        end
        for i = 0, 27 do
            if i % 9 ~= 0 then
                local angle = i * math.pi * 2 / 28
                detail(math.floor(232 + math.cos(angle) * 53), math.floor(113 + math.sin(angle) * 52),
                    3, 3, "#3b5c5d", -25)
            end
        end
        for i = 0, 8 do
            local x = 62 + i * 33
            local length = 15 + (i * 13) % 31
            detail(x, 39, 1, length, "#446b5c", -20)
            detail(x - 2, 43 + math.floor(length * .5), 5, 2, "#769a75", -19)
            detail(x, 40 + length, 2, 2, "#c0c690", -18, 14)
        end
        for i = 0, 10 do
            local x = 70 + i * 27
            local height = 5 + i % 5
            detail(x, 184 - height, 1, height, "#6d8e6b", -16)
            detail(x - 2, 182 - height, 5, 2, "#8ca476", -15)
        end
        for _, x in ipairs({112, 143, 294, 322}) do
            sc.spawn({x = x, y = 174, w = 2, h = 10, color = "#638674", layer = -4})
            sc.spawn({x = x - 3, y = 171, w = 8, h = 4, color = "#a3b686", glow = 18, layer = -3})
        end
        -- Pixel branches are ordinary data, useful for teaching composition.
        for _, branch in ipairs({
            {228, 130, 7, 54}, {218, 142, 13, 4}, {232, 124, 16, 4},
            {216, 126, 4, 19}, {245, 110, 4, 18}, {211, 123, 10, 4},
            {227, 110, 4, 23}, {248, 109, 10, 3}, {233, 153, 22, 4},
            {251, 145, 4, 11}, {204, 166, 27, 4},
        }) do
            sc.spawn({x = branch[1], y = branch[2], w = branch[3], h = branch[4],
                color = "#587466", layer = -5})
        end
        for _, leaf in ipairs({{211, 119}, {225, 106}, {252, 105}, {249, 141}}) do
            sc.spawn({x = leaf[1], y = leaf[2], w = 6, h = 4,
                color = "#c5be85", glow = 24, layer = -4})
        end
    end,
    update = function(dt)
        elapsed = elapsed + dt
        local p = sc.get(keeper)
        coyote = p.grounded and .1 or math.max(0, coyote - dt)
        buffer = sc.pressed("jump") and .12 or math.max(0, buffer - dt)
        local axis = (sc.down("right") and 1 or 0) - (sc.down("left") and 1 or 0)
        if axis ~= 0 then facing = axis end
        local vy = p.vy
        if buffer > 0 and coyote > 0 then vy, coyote, buffer = -235, 0, 0 end
        if sc.released("jump") and vy < -100 then vy = -100 end
        local frame = facing < 0 and 4 or 0
        if not p.grounded then frame = frame + 3
        elseif axis ~= 0 then frame = frame + math.floor(elapsed * 10) % 3 end
        sc.set(keeper, {vx = axis * 104, vy = vy, frame = frame})
        sc.set(sc.find("memory"), {y = 91 + math.sin(elapsed * 1.6) * 3,
            frame = math.floor(elapsed * 5) % 4})
        if sc.tick() % 16 == 0 then
            sc.emit(207 + sc.random() * 54, 100 + sc.random() * 66, 1, "#dfce9177", 5, 2)
        end
        if elapsed > .5 and p.x < 66 and sc.pressed("action") and not leaving then
            leaving = true
            sc.scene("main.lua")
        end
    end,
    draw = function()
        sc.rect(22, 141, 4, 43, "#ac9c67")
        sc.rect(49, 141, 4, 43, "#ac9c67")
        sc.rect(26, 137, 23, 6, "#ac9c67")
        sc.rect(30, 148, 15, 36, "#0a1a20")
        sc.rect(34, 151, 1, 28, "#e5c88999")
        sc.rect(12, 10, 169, 34, "#08131bc7", true)
        sc.text("THE STILL ARCHIVE", 20, 17, 10, "#ecdfb3", true)
        sc.text("A PLACE FOR LOST LIGHT", 20, 31, 10, "#8ca394", true)
        sc.rect(12, 191, 360, 16, "#08131be6", true)
        sc.text("A/D MOVE   SPACE JUMP   E RETURN", 20, 194, 10, "#8ca394", true)
        sc.text("You carried the dark a little further.", 18, 70, 10, "#a9b7a0", true)
        local p = sc.get(keeper)
        if p.x < 72 then sc.text("[E] BEGIN AGAIN", 20, 122, 10, "#e6d8ad", true) end
    end,
}
