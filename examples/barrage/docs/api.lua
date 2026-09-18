---@meta
-- ShinyCore 1.0 development API: include this file in LuaLS workspace.library; do not execute it.
-- Host: C++23 with an owning Lua VM; gameplay API and scene format remain Lua 5.4.
-- Coordinates are pixels with +Y down. Map coordinates are zero-based cells.
-- Fixed update: 60 Hz. Colors: #RRGGBB or #RRGGBBAA. Angles use radians.
-- Entity IDs are generation-checked integers. Invalid inputs raise Lua errors.
-- The VM allows base, table, string, math, utf8, with no file/process API.
-- Optional sc.net is available only in SHINY_NETWORK=ON builds (see networking.md).
-- Project-local require is available; dofile/loadfile/load, math.random and math.randomseed are unavailable.
-- pcall/xpcall cannot suppress the callback instruction limit. print -> stderr.
-- Ordinary metatables work; __gc finalizers are disabled because Lua disables hooks there.
-- Limit: 16 MiB Lua allocation and 1,000,000 instructions per load/init/update/draw.
-- These are authoring safeguards, not a security boundary for hostile scripts/assets.

---@alias ScEntityId integer
---@alias ScColor string # Hex color: #RRGGBB or #RRGGBBAA.
---@alias ScAction 'left'|'right'|'up'|'down'|'jump'|'action'
---@alias ScTile '.'|'#'|'=' # Empty, solid, one-way platform.

---@class ScEntityPatch
---@field tag? string # Exact-match label, at most 47 UTF-8 bytes. Default ''.
---@field x? number # Left world position, -1e6..1e6. Default 0.
---@field y? number # Top world position, -1e6..1e6. Default 0.
---@field w? number # Width, 0.001..4096. Default 8.
---@field h? number # Height, 0.001..4096. Default 8.
---@field vx? number # Velocity in pixels/second, -1e6..1e6. Default 0.
---@field vy? number # Velocity in pixels/second, -1e6..1e6. Default 0.
---@field dynamic? boolean # Legacy shorthand for a dynamic Box2D body. Prefer body. Default false.
---@field solid? boolean # Enable body collision filtering. Default true.
---@field gravity? number # Multiplier of scene gravity, -100..100. Default 1.
---@field color? ScColor # Default #FFFFFFFF.
---@field glow? number # Glow radius in pixels, 0..1024. Default 0.
---@field sprite? string # Image resource name or project-relative image path, at most 127 bytes. Default ''.
---@field frame? integer # Zero-based row-major spritesheet frame, 0..65535. Default 0.
---@field frame_w? integer # Source frame width, 1..4096; 0 means whole image. Default 0.
---@field frame_h? integer # Source frame height, 1..4096; set together with frame_w. Default 0.
---@field layer? integer # Ascending draw order, -32768..32767. Default 0.
---@field body? ScBody|false # Explicit Box2D body; false removes the body. Default absent.
---@field angle? number # Radians, -1e6..1e6. Default 0.
---@field angular_velocity? number # Radians/second, -1000..1000. Default 0.
---@field flip_x? boolean # Sprite horizontal flip. Default false.
---@field flip_y? boolean # Sprite vertical flip. Default false.

---@class ScEntity: ScEntityPatch
---@field id ScEntityId # Read-only. Never include in sc.set() patches.
---@field grounded boolean # Read-only result of the previous physics step.
---@field support ScEntityId # Read-only supporting entity; 0 for map or none.
---@field normal_x number # Read-only ground normal, +Y down.
---@field normal_y number # Read-only ground normal; grounded normals have y < -.5.
---@field tag string
---@field x number
---@field y number
---@field w number
---@field h number
---@field vx number
---@field vy number
---@field dynamic boolean
---@field solid boolean
---@field gravity number
---@field color ScColor # Canonical #RRGGBBAA.
---@field glow number
---@field sprite string
---@field frame integer
---@field frame_w integer
---@field frame_h integer
---@field layer integer

---@class ScMap
---@field rows string[] # Required dense array of equal-length ASCII rows; only '.', '#', '='. At most 16384 cells.
---@field tile_size? integer # Pixels per cell, 1..256. Default 8.
---@field color? ScColor # Tile fill.
---@field accent? ScColor # Tile edge/accent.
---@field background? ScColor # Scene background.

---@class ScScene
---@field title? string # At most 127 UTF-8 bytes.
---@field width? integer # Logical viewport width, 64..4096. Default 384.
---@field height? integer # Logical viewport height, 64..4096. Default 216.
---@field gravity? number # Pixels/second², -1e6..1e6. Default 600.
---@field ambient? number # Ambient light, 0..1. Default 0.4.
---@field map? ScMap|string # Tiled .tmj path or ASCII map. Default empty 48×27 map with solid virtual bounds.
---@field entities? ScEntityPatch[] # Dense array, at most 256 living entities.
---@field init? fun() # Runs once after config/entities are loaded.
---@field update? fun(dt: number) # Runs before physics, exactly dt=1/60 seconds.
---@field draw? fun(alpha: number) # Queue drawing only; alpha=0..1. Keep Lua state unchanged.
-- A .lua scene file MUST return a ScScene. Unknown scene/map/entity keys error.
-- Scene, map and entity configs/patches must be plain tables without metatables.
-- Seed belongs to the host (--seed), not the scene table. Each scene is self-contained.
-- Entity/world mutation belongs in init/update. Use sc.state for cross-room data; Lua locals reset.
-- Scene loads are transactional at the host: a failed scene never replaces the old one.

---@class ScAPI
sc = {}

---Create an entity from explicitly supplied fields and defaults.
---Default capacity 4096; project.limits.entities configures 1..65536. Errors at capacity. Available in init/update, forbidden in draw.
---@param entity ScEntityPatch
---@return ScEntityId
function sc.spawn(entity) end

---Return an independent snapshot. Editing the table does not update the entity.
---A stale or unknown ID raises an error.
---@param id ScEntityId
---@return ScEntity
function sc.get(id) end

---Atomically validate and apply only the supplied writable fields.
---Unknown fields, id, and grounded raise an error; the entity remains unchanged.
---Available in init/update, forbidden in draw.
---@param id ScEntityId
---@param patch ScEntityPatch
function sc.set(id, patch) end

---Destroy a living entity. Its previous ID remains permanently invalid.
---A stale/unknown ID raises an error. Available in init/update, forbidden in draw.
---@param id ScEntityId
---@return boolean success # Always true after successful validation.
function sc.destroy(id) end

---Find the first live exact tag match. Tags need not be unique.
---@param tag string # Nonempty, at most 47 UTF-8 bytes.
---@return ScEntityId|nil
function sc.find(tag) end

---Test overlapping axis-aligned bounds. Touching edges alone do not overlap.
---Both IDs must be alive.
---@param a ScEntityId
---@param b ScEntityId
---@return boolean
function sc.overlap(a, b) end

---Whether the action is held during this fixed tick.
---@param action ScAction
---@return boolean
function sc.down(action) end

---Whether the action transitioned from up to down this fixed tick.
---@param action ScAction
---@return boolean
function sc.pressed(action) end

---Whether the action transitioned from down to up this fixed tick.
---@param action ScAction
---@return boolean
function sc.released(action) end

---Read or replace a map cell. Out-of-bounds reads return '#'; writes error.
---Coordinates are zero-based cell indices, not pixels. Writes forbidden in draw.
---@param x integer
---@param y integer
---@param tile? ScTile
---@return ScTile
function sc.tile(x, y, tile) end

---Deterministic host-seeded RNG. No arguments returns a float in [0,1).
---Two integer bounds return an integer in inclusive [min,max]. Otherwise float range.
---Bounds must lie within -1e6..1e6 and min<=max. Forbidden in draw.
---@overload fun(): number
---@overload fun(minimum: integer, maximum: integer): integer
---@param minimum number
---@param maximum number
---@return number
function sc.random(minimum, maximum) end

---Emit particles; when the shared pool is full additional particles are omitted.
---Available in init/update, forbidden in draw.
---@param x number
---@param y number
---@param count integer # 0..1024.
---@param color ScColor
---@param speed? number # Pixels/second, 0..1e6. Default 30.
---@param life? number # Seconds, 0.001..60. Default 0.5.
function sc.emit(x, y, count, color, speed, life) end

---Queue a synthesized tone. At most 32 tones per tick; exceeding this errors.
---Available in init/update, forbidden in draw. No audio device is needed in headless mode.
---@param frequency number # Hertz, 20..20000.
---@param duration? number # Seconds, 0.001..10. Default 0.1.
---@param volume? number # 0..1. Default 0.2.
function sc.tone(frequency, duration, volume) end

---Follow an entity, or set the camera's top-left world position.
---The core clamps the view to map bounds during physics.
---Available in init/update, forbidden in draw.
---@overload fun(id: ScEntityId)
---@param x number
---@param y number
function sc.camera(x, y) end

---Set the persistent HUD message; '' clears it.
---Available in init/update, forbidden in draw.
---@param text string # At most 191 UTF-8 bytes.
function sc.message(text) end

---Request a scene switch after this callback. Paths are relative to project root.
---No absolute paths, '.', '..', empty path segments, backslashes, or colons.
---The suffix must be '.lua'. Available in init/update, forbidden in draw.
---@param relative_lua_path string
---@param state? table<string,ScData> # Replace explicit state on successful transition; omitted inherits it.
function sc.scene(relative_lua_path, state) end

---Queue a filled rectangle. Only allowed in draw(alpha).
---A configurable shared maximum of 4096 drawing commands by default applies to rect/circle/text per frame.
---@param x number
---@param y number
---@param w number # Nonnegative pixels.
---@param h number # Nonnegative pixels.
---@param color ScColor
---@param screen? boolean # true bypasses the camera; default false.
function sc.rect(x, y, w, h, color, screen) end

---Queue a filled circle centered at (x,y). Only allowed in draw(alpha).
---@param x number
---@param y number
---@param radius number # Pixels, 0..4096.
---@param color ScColor
---@param screen? boolean # true bypasses the camera; default false.
function sc.circle(x, y, radius, color, screen) end

---Queue text. Only allowed in draw(alpha).
---@param text string # At most 511 UTF-8 bytes; missing glyphs use ? with a once-per-room warning.
---@param x number
---@param y number
---@param size number # Pixel height, 1..512.
---@param color ScColor
---@param screen? boolean # true bypasses the camera; default false.
---@param options? ScTextOptions
function sc.text(text, x, y, size, color, screen, options) end

---Number of completed fixed simulation steps, starting at 0.
---@return integer
function sc.tick() end

---Deterministic simulation seconds, exactly tick()/60.
---@return number
function sc.time() end

---Write a diagnostic line to stderr. print(text) uses the same function.
---@param text string # At most 4096 UTF-8 bytes.
function sc.log(text) end

---@alias ScNetChannel 'reliable'|'state'
---@class ScNetEvent
---@field type 'connect'|'receive'|'disconnect'
---@field peer integer # Endpoint-local connection ID; never a scene entity ID. Failed join uses 0.
---@field data? string # Receive only; binary-safe, at most 1200 bytes.
---@field channel? ScNetChannel # Receive only.
---@field reason? integer # Disconnect only; application reason or 0 for transport loss.

---@class ScNetSession
local session = {}

---Poll one event without blocking; call repeatedly within an explicit per-tick budget.
---nil alone means no event. nil,error means transport failure. Forbidden in draw.
---@return ScNetEvent|nil event
---@return string|nil error
function session:poll() end

---Queue a binary message; success means queued, not acknowledged.
---Reliable is ordered/retransmitted; state is unreliable/sequenced on a separate channel.
---peer=0 broadcasts to established peers and fails when none exist. Forbidden in draw.
---@param peer integer
---@param data string # 0..1200 bytes, including NUL bytes.
---@param channel? ScNetChannel # Default 'reliable'.
---@return boolean|nil queued
---@return string|nil error
function session:send(peer, data, channel) end

---Submit queued packets without waiting for delivery. Forbidden in draw.
---@return boolean|nil submitted
---@return string|nil error
function session:flush() end

---Start a graceful disconnect; poll until disconnect. The peer stops accepting sends immediately.
---@param peer integer # Positive live peer ID.
---@param reason? integer # Unsigned 32-bit application reason; default 0.
---@return boolean|nil started
---@return string|nil error
function session:disconnect(peer, reason) end

---Release the socket and discard pending messages. Idempotent; forbidden in draw.
---Also called automatically on collection or scene/VM teardown.
function session:close() end

---Bound local UDP port (useful after hosting on port 0).
---@return integer|nil port
---@return string|nil error
function session:port() end

---Estimated round-trip time of a connected peer.
---@param peer integer
---@return integer|nil milliseconds
---@return string|nil error
function session:rtt(peer) end

sc.net = {}
---@type boolean # False in the default build; true with -DSHINY_NETWORK=ON.
sc.net.available = false

---Create a native UDP listener; at most four live sessions per Lua VM.
---Open during update after an explicit game action to keep --check/reload side-effect free.
---Invalid argument types/ranges raise Lua errors; operational failures return nil,error.
---All mutable network calls are forbidden in draw. Session resources are outside the Lua heap.
---@param bind_ipv4 string # Numeric IPv4; '127.0.0.1' for local only, '0.0.0.0' for all interfaces.
---@param port integer # 0..65535; 0 chooses a free port.
---@param max_peers? integer # 1..32; default 8.
---@return ScNetSession|nil session
---@return string|nil error
function sc.net.host(bind_ipv4, port, max_peers) end

---Begin an asynchronous connection; poll both endpoints until connect or disconnect.
---Numeric IPv4 only (no DNS, authentication, encryption, relay or NAT traversal).
---@param server_ipv4 string
---@param port integer # 1..65535.
---@return ScNetSession|nil session
---@return string|nil error
function sc.net.join(server_ipv4, port) end

---@alias ScData boolean|number|string|ScData[]|table<string,ScData>
---@class ScBodyShape
---@field shape? 'box'|'circle'|'capsule'|'polygon' # Default box. Capsules follow their longer axis.
---@field x? number # Local top-left offset, -4096..4096. Default 0.
---@field y? number # Local top-left offset, -4096..4096. Default 0.
---@field w? number # 0.001..4096. Default 8.
---@field h? number # 0.001..4096. Default 8.
---@field vertices? number[] # Polygon: 3..8 distinct convex x,y pairs, each -4096..4096.

---@class ScBody
---@field type? 'static'|'kinematic'|'dynamic' # Default dynamic when body is supplied.
---@field shape? 'box'|'circle'|'capsule'|'polygon' # Default box; uses entity dimensions.
---@field vertices? number[] # Local to entity top-left; required for polygon.
---@field shapes? ScBodyShape[] # 1..4 shapes override the single shape, sharing material/filter.
---@field density? number # .001..10000. Default 1; authored mass scales with area/32².
---@field friction? number # 0..10. Default .3 (legacy dynamic shorthand: 0).
---@field restitution? number # 0..1. Default 0.
---@field fixed_rotation? boolean # Default true.
---@field sensor? boolean # Default false; overlap events without collision response.
---@field bullet? boolean # Default false; Box2D continuous collision for dynamic bodies.
---@field one_way? boolean # Default false; top-face platform, intended unrotated.
---@field category? integer # Unsigned 32-bit bits; default 1.
---@field mask? integer # Unsigned 32-bit bits; default 4294967295.

---@class ScResource
---@field type 'image'|'sound'|'music'|'font'
---@field path string # Project-relative, at most 127 UTF-8 bytes. Image/WAV/Ogg Vorbis/TTF or OTF.
---@field size? integer # Font rasterization height, 1..128, default 16.
---@field characters? string # Optional preloaded repertoire plus ASCII. Other glyph metrics load on demand; cache limit 8192.

---@class ScProject
---@field display? ScSettingsPatch # Application defaults; valid persisted preferences take precedence.
---@field id? string # Required for saves. Stable 1..128 byte alnum/._- identity; dot-only . and .. are invalid.
---@field entry? string # Default main.lua. Project-relative .lua path.
---@field rooms? string[] # --check-all validates these, at most 256; each must work with empty state.
---@field resources? table<string,ScResource> # At most 128 resource declarations.
---@field data_version? integer # Positive integer, default 1.
---@field modules? string[] # Required native capabilities; --api reports availability.
---@field limits? table<string,integer> # entities, particles, draws, contacts, sound_voices.
-- Optional project.lua returns ScProject; unknown fields error. It contains data, not callbacks.

---@class ScSettingsPatch
---@field width? integer # Windowed width 320..7680; default 1152.
---@field height? integer # Windowed height 180..4320; default 648.
---@field mode? 'windowed'|'borderless' # Default windowed; borderless uses the monitor dimensions.
---@field scale? 'integer'|'smooth' # Default integer; fractional fit when the window is smaller than logical output.
---@field vsync? boolean # Default true.
---@field volume? table<'master'|'music'|'sfx'|'ui',number> # User gains 0..1, default 1; multiplied with game bus gains.
---@field bindings? table<string,ScData> # Complete replacement object, at most 16 KiB; shiny.input uses named profiles.

sc.settings = {}
---Deep copy of application preferences; available across room changes.
---@return ScSettingsPatch
function sc.settings.get() end
---Update only, outside checks. Validates before applying; failures preserve previous preferences.
---Persists separately from save slots; a persistence failure also rolls back native window settings.
---@param patch ScSettingsPatch
---@param persist? boolean # Default true; false applies only for this application session.
---@return true|nil ok
---@return string? error
function sc.settings.apply(patch,persist) end
---Latest load/apply error, or an empty string. Corrupt settings use project defaults.
---@return string
function sc.settings.error() end

---Load a project-local Lua module; dots become directories. Cached per VM.
---Circular imports and first loads in draw error. Paths use letters/digits/underscore/dots.
---@param module string # 1..128 bytes.
---@return any
function require(module) end

sc.state = {}
---Return a deep copy. Plain objects, dense arrays, finite numbers and valid UTF-8 only.
---@param key string # Nonempty, at most 128 bytes; no NUL.
---@return ScData|nil
function sc.state.get(key) end
---Atomic update; nil deletes. Total serialized state <=256 KiB, depth <=16.
---Empty Lua tables represent objects. No metatables, mixed/sparse arrays or cycles.
---State survives F5 and room switches. Entity/audio/joint IDs must not be persisted.
---@param key string
---@param value? ScData
function sc.state.set(key, value) end

sc.save = {}
---Update only; disabled in --check/--check-all. Operational failures return nil,error.
---Writes explicit state and current room, not the VM/physics/animation/audio runtime.
---Native default: OS user-data directory; headless default: 16 in-memory slots.
---@param slot string # Alnum, underscore, hyphen; 1..128 bytes.
---@return boolean|nil success
---@return string|nil error
function sc.save.write(slot) end
---Validate current format/data version, recover a valid backup if needed, then request room reconstruction.
---Unsupported versions and invalid data leave the active state unchanged.
---A room load failure is reported by the host; graphical mode keeps the current room.
---@param slot string
---@return boolean|nil requested
---@return string|nil error
function sc.save.load(slot) end

sc.physics = {}
---@param id ScEntityId
---@param x number # Center force in authored mass*pixel/second², -1e6..1e6.
---@param y number
function sc.physics.force(id, x, y) end
---@param id ScEntityId
---@param x number # Center impulse in authored mass*pixel/second, -1e6..1e6.
---@param y number
function sc.physics.impulse(id, x, y) end
---@param id ScEntityId
---@param seconds? number # Ignore one-way platforms for 0..10 seconds, default .2.
function sc.physics.drop(id, seconds) end

---@class ScContact
---@field a ScEntityId # 0 represents terrain.
---@field b ScEntityId
---@field nx number # Normal points from a toward b. Sensors use zero.
---@field ny number
---@field sensor boolean
---@field phase 'contact'|'begin'|'end' # Solids: current contacts; sensors: overlap edges.
---Previous completed step; stable by a,b,phase. Destroyed shapes may omit sensor end.
---@return ScContact[]
function sc.physics.contacts() end

---@class ScRayHit
---@field id ScEntityId # 0 represents terrain.
---@field x number
---@field y number
---@field nx number
---@field ny number
---@field fraction number # Along translation, 0..1.
---Closest Box2D hit. Syncs pending changes; forbidden in draw/migration.
---@param x number
---@param y number
---@param dx number # Translation, not endpoint.
---@param dy number
---@return ScRayHit|nil
function sc.physics.ray(x, y, dx, dy) end
---Entity AABB overlap in stable slot order; includes artwork, ignores rotation.
---@param x number
---@param y number
---@param w number # Nonnegative.
---@param h number # Nonnegative.
---@return ScEntityId[]
function sc.physics.query(x, y, w, h) end
---At most 256 joints. Body destruction/rebuild destroys attached joints.
---Prismatic axis is local vertical, limits 0..length; motor controls are not yet implemented.
---@param type 'distance'|'revolute'|'prismatic'
---@param a ScEntityId
---@param b ScEntityId
---@param x number # Shared world-space anchor.
---@param y number
---@param length? number # .01..4096 pixels, default 8.
---@return integer joint
function sc.physics.joint(type, a, b, x, y, length) end
---@param joint integer # Generation-checked; stale IDs error.
function sc.physics.unjoint(joint) end

---Read/write a Tiled layer GID including H/V/diagonal flip bits. Writes forbidden in draw.
---Zero-based cell coordinates. Unknown layers/GIDs and unsupported hex rotation error.
---@param layer string
---@param x integer
---@param y integer
---@param gid? integer
---@return integer
function sc.map(layer, x, y, gid) end
---Return an independent array of raw Tiled object records for Lua factories.
---@return table[]
function sc.objects() end

---@class ScTextOptions
---@field font? string # Font resource name; fallback tries other declared fonts in name order, then ASCII ?.
---@field wrap? number # Per-codepoint wrap width, 0..4096; default 0 (none).
---@field align? integer # 0 left / 1 center / 2 right within wrap width; default 0.
---Same CPU metrics as rendering, including fallback and line breaks. No shaping/kerning.
---@param text string # Valid UTF-8, at most 4096 bytes.
---@param size number # 1..512 pixels.
---@param font? string
---@param wrap? number # 0..4096.
---@return number width
---@return number height
function sc.measure(text, size, font, wrap) end

---@class ScAudioOptions
---@field volume? number # 0..1, default 1.
---@field pitch? number # .25..4, default 1.
---@field fade? number # Seconds to reach target volume, 0..60, default 0.
---@field loop? boolean # Default false.
---@field paused? boolean # Default false; pauses playback clock and fade.
---@field persistent? boolean # Default false; only music carries across rooms/F5.
sc.audio = {}
---WAV sound (32 voices) or streamed Ogg Vorbis music (2 streams).
---Headless uses the same logical clock/handles; audio operations forbidden in draw/migration.
---@param resource string
---@param options? ScAudioOptions
---@return integer|nil handle
---@return string|nil error # Capacity failure.
function sc.audio.play(resource, options) end
---Atomic patch; a stale handle or invalid option errors.
---@param handle integer
---@param options ScAudioOptions
function sc.audio.set(handle, options) end
---@param handle integer
---@param fade? number # 0..60 seconds, default 0 (immediate).
function sc.audio.stop(handle, fade) end

-- Device queries use a fixed-tick snapshot, not text/IME input.
---@alias ScKey 'apostrophe'|'comma'|'minus'|'period'|'slash'|'0'|'1'|'2'|'3'|'4'|'5'|'6'|'7'|'8'|'9'|'semicolon'|'equal'|'a'|'b'|'c'|'d'|'e'|'f'|'g'|'h'|'i'|'j'|'k'|'l'|'m'|'n'|'o'|'p'|'q'|'r'|'s'|'t'|'u'|'v'|'w'|'x'|'y'|'z'|'left_bracket'|'backslash'|'right_bracket'|'grave'|'space'|'escape'|'enter'|'tab'|'backspace'|'insert'|'delete'|'right'|'left'|'down'|'up'|'page_up'|'page_down'|'home'|'end'|'caps_lock'|'scroll_lock'|'num_lock'|'print_screen'|'pause'|'f1'|'f2'|'f3'|'f4'|'f5'|'f6'|'f7'|'f8'|'f9'|'f10'|'f11'|'f12'|'left_shift'|'left_control'|'left_alt'|'left_super'|'right_shift'|'right_control'|'right_alt'|'right_super'|'kb_menu'|'kp_0'|'kp_1'|'kp_2'|'kp_3'|'kp_4'|'kp_5'|'kp_6'|'kp_7'|'kp_8'|'kp_9'|'kp_decimal'|'kp_divide'|'kp_multiply'|'kp_subtract'|'kp_add'|'kp_enter'|'kp_equal'
---@alias ScGamepadButton 'dpad_up'|'dpad_right'|'dpad_down'|'dpad_left'|'north'|'east'|'south'|'west'|'left_shoulder'|'left_trigger'|'right_shoulder'|'right_trigger'|'back'|'guide'|'start'|'left_thumb'|'right_thumb'
---@alias ScGamepadAxis 'left_x'|'left_y'|'right_x'|'right_y'|'left_trigger'|'right_trigger'

---Read this tick; unknown names raise errors. Release also occurs on disconnect.
---@param name ScKey
---@return boolean
function sc.key_down(name) end

---Read this tick; unknown names raise errors. Release also occurs on disconnect.
---@param name ScKey
---@return boolean
function sc.key_pressed(name) end

---Read this tick; unknown names raise errors. Release also occurs on disconnect.
---@param name ScKey
---@return boolean
function sc.key_released(name) end

---Read this tick; unknown names raise errors. Release also occurs on disconnect.
---@param name ScGamepadButton
---@return boolean
function sc.gamepad_down(name) end

---Read this tick; unknown names raise errors. Release also occurs on disconnect.
---@param name ScGamepadButton
---@return boolean
function sc.gamepad_pressed(name) end

---Read this tick; unknown names raise errors. Release also occurs on disconnect.
---@param name ScGamepadButton
---@return boolean
function sc.gamepad_released(name) end

---Connection state of the selected single-player gamepad in this tick.
---@return boolean
function sc.gamepad_connected() end

---Sticks [-1,1], positive right/down; triggers [0,1]. Missing devices/axes return 0.
---Deadzone defaults to 0.2, per axis; outside values rescale linearly. Pass 0 to disable.
---@param axis ScGamepadAxis
---@param deadzone? number # Finite [0,1), representable below 1 as float.
---@return number
function sc.gamepad_axis(axis, deadzone) end

-- BEGIN GENERATED COMPLETE API
-- Regenerate with tools/api_docs.py using full and lightweight builds.
sc.app = {}
sc.debug = {}
sc.input = {}
sc.navigation = {}
sc.projectiles = {}
sc.stream = {}
---Pause physics and particles while Lua UI and input keep updating.
---pause(boolean)
function sc.app.pause(...) end
---Read simulation pause state.
---paused() -> boolean
function sc.app.paused(...) end
---Request orderly application shutdown.
---quit()
function sc.app.quit(...) end
---Read or patch master/music/sfx/ui volume, fade and pause; buses persist across rooms.
---bus(name,options?) -> state
function sc.audio.bus(...) end
---Push or pop a screen-space clipping rectangle; nesting depth 32.
---clip(x,y,w,h) / clip()
function sc.clip(...) end
---Expose explicit bounded Lua data for diagnostic snapshots.
---watch(name,value)
function sc.debug.watch(...) end
---Find every entity with a matching tag in stable slot order.
---find_all(tag) -> ids
function sc.find_all(...) end
---Read entity copies in requested order.
---get_many(ids) -> entities
function sc.get_many(...) end
---Draw a declared image resource.
---image(resource,x,y,w,h,screen?)
function sc.image(...) end
---UTF-8 grapheme starts plus end position; Lua one-based byte offsets.
---boundaries(text) -> byte_offsets
function sc.input.boundaries(...) end
---Queue a clipboard write or read the recorded paste input for this tick.
---clipboard(text?) -> text?
function sc.input.clipboard(...) end
---Set logical-screen IME candidate position or end text focus.
---focus_text(x,y) / focus_text(false)
function sc.input.focus_text(...) end
---Read normalized axis with deadzone in [0,1); default .2.
---gamepad_axis(name,deadzone?,slot?) -> number
function sc.input.gamepad_axis(...) end
---Read connection of selected controller or explicit slot 1..4.
---gamepad_connected(slot?) -> boolean
function sc.input.gamepad_connected(...) end
---Held button; optional stable slot 1..4, default selected controller.
---gamepad_down(name,slot?) -> boolean
function sc.input.gamepad_down(...) end
---Press edge, including taps between ticks.
---gamepad_pressed(name,slot?) -> boolean
function sc.input.gamepad_pressed(...) end
---Release edge, including device disconnection.
---gamepad_released(name,slot?) -> boolean
function sc.input.gamepad_released(...) end
---Fixed snapshot keyboard held state.
---key_down(name) -> boolean
function sc.input.key_down(...) end
---Fixed snapshot keyboard press edge.
---key_pressed(name) -> boolean
function sc.input.key_pressed(...) end
---Fixed snapshot keyboard release edge.
---key_released(name) -> boolean
function sc.input.key_released(...) end
---Pointer in logical viewport coordinates.
---mouse() -> x,y,inside
function sc.input.mouse(...) end
---Mouse held state.
---mouse_down(name) -> boolean
function sc.input.mouse_down(...) end
---Mouse press edge.
---mouse_pressed(name) -> boolean
function sc.input.mouse_pressed(...) end
---Mouse release edge.
---mouse_released(name) -> boolean
function sc.input.mouse_released(...) end
---Fixed-tick input data including four pad slots, pointer and text.
---snapshot() -> input
function sc.input.snapshot(...) end
---UTF-8 text for this fixed tick.
---text() -> committed,composition
function sc.input.text(...) end
---Accumulated fixed-tick wheel deltas.
---wheel() -> x,y
function sc.input.wheel(...) end
---Read a normalized shared-field direction at a world position.
---direction(flow,x,y) -> dx,dy
function sc.navigation.direction(...) end
---Build one shared target field in slot 1..16; rebuild after terrain changes.
---flow(gx,gy,budget?,slot?) -> id,status,visited
function sc.navigation.flow(...) end
---Deterministic A-star in tile coordinates; ok, unreachable or budget_exhausted.
---path(sx,sy,gx,gy,budget?) -> result
function sc.navigation.path(...) end
---Atomically validate and steer an entity batch without per-unit Lua callbacks.
---steer(flow,entities,speed) -> count
function sc.navigation.steer(...) end
---Borrow an application session by name, including during candidate initialization.
---sc.net.bind(name) -> session|nil,error
function sc.net.bind(...) end
---Remove all bullets without recycling their IDs.
---clear()
function sc.projectiles.clear(...) end
---Allocate 1..65536 bullets once during init; no Box2D bodies.
---configure(capacity)
function sc.projectiles.configure(...) end
---Current live projectile count.
---count() -> integer
function sc.projectiles.count(...) end
---Previous tick hits ordered by projectile, fraction and target.
---hits() -> hits
function sc.projectiles.hits(...) end
---Validate a complete batch before spawning; numeric RGBA and collision mask.
---spawn(specs) -> ids
function sc.projectiles.spawn(...) end
---Delete a slot and its backup; update only. Missing slots are harmless.
---delete(slot) -> true
function sc.save.delete(...) end
---Sorted slot metadata; bounded to 128 disk slots, including invalid record diagnostics.
---list() -> slots
function sc.save.list(...) end
---Read current-format data without changing rooms; recover from previous valid backup when needed.
---read(slot) -> record|nil,error
function sc.save.read(...) end
---Validate all {id,patch} records before commit; duplicate IDs rejected.
---set_many(items)
function sc.set_many(...) end
---Wait for a requested chunk without advancing simulation; absent sparse chunks are empty.
---get(x,y) -> chunk
function sc.stream.get(...) end
---Open a built map index with a single worker and 128 MiB bounded cache; init only.
---open(index_path)
function sc.stream.open(...) end
---Release one chunk reference; unpinned chunks may be evicted.
---release(x,y)
function sc.stream.release(...) end
---Pin and prefetch a 32x32 chunk.
---request(x,y)
function sc.stream.request(...) end
---Read cache budget, charged bytes, pinned and queued chunks.
---stats() -> counters
function sc.stream.stats(...) end
---Transfer socket ownership to the application; room teardown no longer closes it.
---session:persist(name) -> true|nil,error
function session:persist(...) end
