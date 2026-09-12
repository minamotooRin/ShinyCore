-- DUET: one host owns simulation; the guest sends only a bounded direction.
-- Network sessions open only after input, never in init/draw or --check.
local PORT = 7777
local VERSION, HELLO, WELCOME, INPUT, STATE = 1, 1, 2, 3, 4
local header = string.pack(">c2B", "DU", VERSION)
local session, peer, role, active
local ids, positions, targets = {}, {}, nil
local address, selected = {127, 0, 0, 1}, 1
local notice = "Two lights. One shared place."
local opened, heard, input_at = 0, 0, 0
local input_x, input_y, sent, received, charge = 0, 0, 0, nil, 0
local reported_input, reported_state, completed = false, false, false
local colors = {"#efbe76", "#79d6d0"}
local rows = {}
for i = 1, 27 do rows[i] = string.rep(".", 48) end

local function clamp(value, low, high) return math.max(low, math.min(high, value)) end
local function address_text() return table.concat(address, ".") end
local function packet(kind, format, ...)
    return header .. string.char(kind) .. string.pack(format, ...)
end
local function newer(sequence)
    if received == nil then return true end
    local distance = (sequence - received) & 0xffffffff
    return distance > 0 and distance < 0x80000000
end

local function reset_players()
    positions = {{x = 80, y = 112}, {x = 296, y = 112}}
    targets, charge, completed = nil, 0, false
    for i = 1, 2 do sc.set(ids[i], positions[i]) end
end

local function leave(reason)
    if session then session:close() end
    session, peer, role, active, targets = nil, nil, nil, nil, nil
    notice = reason
    sc.message("")
end

local function send(data, channel)
    local ok, err = session:send(peer, data, channel)
    if not ok then leave("Send failed: " .. err) end
    return ok
end

local function begin(host)
    if not sc.net.available then notice = "Networking disabled in this build."; return end
    local err
    if host then session, err = sc.net.host("0.0.0.0", PORT, 1)
    else session, err = sc.net.join(address_text(), PORT) end
    if not session then notice = "Network: " .. err; return end
    role, active, peer = host and "host" or "guest", false, nil
    opened, heard, input_at = sc.time(), sc.time(), sc.time()
    input_x, input_y, sent, received = 0, 0, 0, nil
    reported_input, reported_state = false, false
    reset_players()
    notice = host and "Waiting for a guest..." or "Connecting..."
    if host then sc.log("DUET listening " .. session:port()) end
end

local function reject()
    sc.log("DUET rejected invalid protocol packet")
    leave("Incompatible or invalid peer. Z / X to retry.")
end

local function receive(event)
    local data = event.data
    if event.peer ~= peer or #data < 4 or data:sub(1, 3) ~= header then reject(); return end
    local kind = data:byte(4)
    if role == "host" and not active then
        if kind ~= HELLO or #data ~= 4 or event.channel ~= "reliable" then reject(); return end
        active, heard = true, sc.time()
        if send(packet(WELCOME, ""), "reliable") then sc.log("DUET host ready") end
    elseif role == "guest" and not active then
        -- Channels are independent: an early state can overtake WELCOME.
        if kind == STATE and event.channel == "state" then return end
        if kind ~= WELCOME or #data ~= 4 or event.channel ~= "reliable" then reject(); return end
        active, heard = true, sc.time()
        sc.log("DUET guest ready")
    elseif role == "host" then
        if kind ~= INPUT or #data ~= 10 or event.channel ~= "state" then reject(); return end
        local sequence, x, y = string.unpack(">I4bb", data, 5)
        if x < -1 or x > 1 or y < -1 or y > 1 then reject(); return end
        if not newer(sequence) then return end
        received, input_x, input_y = sequence, x, y
        input_at, heard = sc.time(), sc.time()
        if not reported_input and (x ~= 0 or y ~= 0) then
            sc.log("DUET host applied remote input")
            reported_input = true
        end
    else
        if kind ~= STATE or #data ~= 18 or event.channel ~= "state" then reject(); return end
        local sequence, ax, ay, bx, by, progress = string.unpack(">I4I2I2I2I2I2", data, 5)
        if ax < 24 * 8 or ax > 352 * 8 or bx < 24 * 8 or bx > 352 * 8
            or ay < 64 * 8 or ay > 168 * 8 or by < 64 * 8 or by > 168 * 8
            or progress > 1000 then reject(); return end
        if not newer(sequence) then return end
        received, heard, charge = sequence, sc.time(), progress / 1000
        targets = {{x = ax / 8, y = ay / 8}, {x = bx / 8, y = by / 8}}
        if not reported_state then sc.log("DUET guest received snapshot"); reported_state = true end
    end
end

local function network()
    -- Bound per-tick work even when a peer produces more traffic than expected.
    for _ = 1, 64 do
        local event, err = session:poll()
        if err then leave("Network: " .. err); return end
        if not event then break end
        if event.type == "connect" then
            if peer then session:disconnect(event.peer)
            else
                peer, heard = event.peer, sc.time()
                if role == "guest" then send(packet(HELLO, ""), "reliable") end
            end
        elseif event.type == "receive" then receive(event)
        elseif event.type == "disconnect" and event.peer == peer then
            leave("Peer left. Z / X to reconnect.")
        end
        if not session then return end
    end
    if (not active and role == "guest" and sc.time() - opened > 5)
        or (peer and sc.time() - heard > 5) then leave("Peer timed out. Z / X to retry.") end
end

local function direction()
    return (sc.down("right") and 1 or 0) - (sc.down("left") and 1 or 0),
        (sc.down("down") and 1 or 0) - (sc.down("up") and 1 or 0)
end

local function move(index, x, y, dt)
    local distance = 76 * dt / ((x ~= 0 and y ~= 0) and math.sqrt(2) or 1)
    local p = positions[index]
    p.x, p.y = clamp(p.x + x * distance, 24, 352), clamp(p.y + y * distance, 64, 168)
end

local function update_game(dt)
    local x, y = direction()
    if role == "host" then
        move(1, x, y, dt)
        if sc.time() - input_at > .25 then input_x, input_y = 0, 0 end
        move(2, input_x, input_y, dt)
        local a, b = positions[1], positions[2]
        local together = math.abs(a.x + 4 - 164) < 12 and math.abs(a.y + 4 - 116) < 12
            and math.abs(b.x + 4 - 220) < 12 and math.abs(b.y + 4 - 116) < 12
        if not completed then charge = clamp(charge + (together and dt or -dt), 0, 1) end
        if sc.tick() % 3 == 0 then
            sent = (sent + 1) & 0xffffffff
            local function quantize(value) return math.floor(value * 8 + .5) end
            send(packet(STATE, ">I4I2I2I2I2I2", sent, quantize(a.x), quantize(a.y),
                quantize(b.x), quantize(b.y), math.floor(charge * 1000 + .5)), "state")
        end
    else
        if sc.tick() % 3 == 0 then
            sent = (sent + 1) & 0xffffffff
            send(packet(INPUT, ">I4bb", sent, x, y), "state")
        end
        if targets then
            -- Exponential visual smoothing; no client physics or prediction.
            local blend = 1 - math.exp(-20 * dt)
            for i = 1, 2 do
                positions[i].x = positions[i].x + (targets[i].x - positions[i].x) * blend
                positions[i].y = positions[i].y + (targets[i].y - positions[i].y) * blend
            end
        end
    end
    for i = 1, 2 do sc.set(ids[i], positions[i]) end
    if charge >= 1 and not completed then
        completed = true
        sc.log("DUET complete")
        sc.tone(659.25, .25, .08)
    end
end

return {
    title = "DUET / A SHARED SIGNAL", width = 384, height = 216, gravity = 0, ambient = .7,
    map = {background = "#0a171f", rows = rows, tile_size = 8},
    init = function()
        for i = 1, 2 do
            ids[i] = sc.spawn({tag = i == 1 and "host" or "guest", w = 8, h = 8,
                color = colors[i], glow = 56, solid = false})
        end
        reset_players()
    end,
    update = function(dt)
        if not session then
            if sc.pressed("left") then selected = (selected - 2) % 4 + 1 end
            if sc.pressed("right") then selected = selected % 4 + 1 end
            if sc.pressed("up") then address[selected] = (address[selected] + 1) % 256 end
            if sc.pressed("down") then address[selected] = (address[selected] - 1) % 256 end
            if sc.pressed("jump") then begin(true)
            elseif sc.pressed("action") then begin(false) end
        elseif sc.pressed("jump") then leave("Session closed. Z / X to reconnect.")
        else
            network()
            if session and active then update_game(dt) end
            if session then session:flush() end
        end
    end,
    draw = function()
        sc.text("D U E T", 18, 15, 20, "#eee5cf", true)
        sc.text("A SHARED SIGNAL", 18, 39, 10, "#637e89", true)
        sc.rect(18, 59, 348, 118, "#132832")
        sc.rect(20, 61, 344, 114, "#0e202a")
        for i = 1, 2 do
            local x = i == 1 and 164 or 220
            sc.circle(x, 116, 16, "#223944")
            sc.circle(x, 116, 12, "#10232d")
            sc.rect(x - 3, 115, 6, 2, colors[i])
            local p = positions[i]
            sc.circle(p.x + 4, p.y + 4, 6, colors[i])
            sc.rect(p.x + 2, p.y + 1, 3, 3, "#fff3da")
        end
        sc.rect(164, 148, 56, 2, "#29414a", true)
        sc.rect(164, 148, 56 * charge, 2, "#e9ddb9", true)
        if session then
            sc.text(active and (completed and "TWO LIGHTS, IN TUNE." or "Stand on your matching ring.")
                or notice, 20, 185, 10, "#dfd7c2", true)
            sc.text((role == "host" and "HOST : GOLD" or "GUEST : TEAL") .. "   ARROWS MOVE   Z LEAVE",
                20, 200, 10, "#728c94", true)
        else
            sc.rect(44, 68, 296, 101, "#10222cf5", true)
            sc.text("Z HOST    X JOIN", 66, 78, 20, "#e9ddb9", true)
            sc.text("JOIN " .. address_text() .. ":" .. PORT, 66, 109, 10, "#81c5c5", true)
            sc.text("ARROWS EDIT IP   OCTET " .. selected, 66, 127, 10, "#728c94", true)
            sc.text(sc.net.available and "Native UDP / two players" or "Build with SHINY_NETWORK=ON",
                66, 150, 10, "#728c94", true)
            sc.text(notice:sub(1, 55), 20, 187, 10, "#dfd7c2", true)
        end
    end,
}
