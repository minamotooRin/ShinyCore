---@meta
-- ShinyCore 1.0 development API: include this file in LuaLS workspace.library; do not execute it.
-- Host: C++23 with an owning Lua VM; gameplay API and scene format remain Lua 5.4.
-- Coordinates are pixels with +Y down. Map coordinates are zero-based cells.
-- Fixed update: 60 Hz. Colors: #RRGGBB or #RRGGBBAA. Angles use radians.
-- Entity IDs are generation-checked integers. Invalid inputs raise Lua errors.
-- The VM allows base, table, string, math, utf8, with no file/process API.
-- Optional sc.net is available only in SHINY_NETWORK=ON builds (see networking.md).
-- Optional sc.material requires SHINY_ADVANCED_RENDER=ON; headless checks do not compile GPU shaders.
-- Project-local require is available; dofile/loadfile/load, math.random and math.randomseed are unavailable.
-- pcall/xpcall cannot suppress the callback instruction limit. print -> stderr.
-- Ordinary metatables work; __gc finalizers are disabled because Lua disables hooks there.
-- Limit: 16 MiB Lua allocation and 1,000,000 instructions per load/init/update/draw.
-- These are authoring safeguards, not a security boundary for hostile scripts/assets.

---@alias ScEntityId integer
---@alias ScColor string # Hex color: #RRGGBB or #RRGGBBAA.
---@alias ScAction 'left'|'right'|'up'|'down'|'jump'|'action'
---@alias ScTile '.'|'#'|'=' # Empty, solid, one-way platform.

---@class ScStreamMetadata
-- Fields generated from native --api.
---@field format integer # Read-only. Built stream format 3.
---@field chunk_size integer # Read-only. 32 tiles per chunk axis.
---@field tilewidth integer # Read-only. Tile width in world pixels.
---@field tileheight integer # Read-only. Tile height in world pixels.
---@field layers table[] # Read-only. Flattened map layers in stable source order.
---@field groups? table[] # Read-only. Optional group hierarchy with one-based parent indices.
---@field properties? table[] # Read-only. Optional map custom properties.
---@field object_coverage? ScObjectCoverage[] # Read-only. Optional sparse cross-chunk object anchor dependencies.
---@field tilesets? table[] # Read-only. Optional imported tileset metadata.
---@field parallaxoriginx? number # Read-only. Optional map parallax X origin.
---@field parallaxoriginy? number # Read-only. Optional map parallax Y origin.

---@class ScObjectCoverage
-- Fields generated from native --api.
---@field x integer # Read-only. Intersected chunk column.
---@field y integer # Read-only. Intersected chunk row.
---@field anchors ScChunkCoordinate[] # Read-only. Anchor chunks holding authored objects crossing this chunk.

---@class ScChunkCoordinate
-- Fields generated from native --api.
---@field x integer # Read-only. Chunk column, -31250..31250.
---@field y integer # Read-only. Chunk row, -31250..31250.

---@class ScVolumePatch
-- Fields generated from native --api.
---@field master? number # Initial engine gain is 1; omitted patch value retains current gain. Range 0..1.
---@field music? number # Initial engine gain is 1; omitted patch value retains current gain. Range 0..1.
---@field sfx? number # Initial engine gain is 1; omitted patch value retains current gain. Range 0..1.
---@field ui? number # Initial engine gain is 1; omitted patch value retains current gain. Range 0..1.

---@class ScVolume
-- Fields generated from native --api.
---@field master number # Read-only. Initial engine gain is 1; omitted patch value retains current gain. Range 0..1.
---@field music number # Read-only. Initial engine gain is 1; omitted patch value retains current gain. Range 0..1.
---@field sfx number # Read-only. Initial engine gain is 1; omitted patch value retains current gain. Range 0..1.
---@field ui number # Read-only. Initial engine gain is 1; omitted patch value retains current gain. Range 0..1.

---@class ScSettings
-- Fields generated from native --api.
---@field width integer # Read-only. Window pixel width; initial engine default 1152. Project/persisted settings may override. Range 320..7680.
---@field height integer # Read-only. Window pixel height; initial engine default 648. Range 180..4320.
---@field mode 'windowed'|'borderless' # Read-only. Initial engine mode is windowed.
---@field scale 'integer'|'smooth' # Read-only. Initial engine scale is integer.
---@field vsync boolean # Read-only. Initial engine value is true.
---@field volume ScVolume # Read-only. Four logical gains; patch merges named buses, omitted buses retain their gain.
---@field bindings table # Read-only. Plain string-keyed object. Patch replaces the entire object; initial value is empty. Gameplay modules own binding semantics. At most 16384 bytes.

---@class ScProjectileStats
-- Fields generated from native --api.
---@field limit integer # Read-only. Project capacity ceiling; zero disables allocation. Range 0..65536.
---@field capacity integer # Read-only. Allocated capacity, zero before configure. Range 0..65536.
---@field used integer # Read-only. Active projectiles. Range 0..65536.
---@field available integer # Read-only. Allocated capacity minus active count. Range 0..65536.
---@field sprites integer # Read-only. Registered sprite regions, retained by clear. Range 0..64.

---@class ScProjectileSpec
-- Fields generated from native --api.
---@field x? number # Initial center X in world pixels. Range -1000000..1000000. Default 0.
---@field y? number # Initial center Y in world pixels. Range -1000000..1000000. Default 0.
---@field vx? number # Horizontal velocity in pixels/second. Range -1000000..1000000. Default 0.
---@field vy? number # Vertical velocity in pixels/second. Range -1000000..1000000. Default 0.
---@field ax? number # Horizontal acceleration in pixels/second squared. Range -1000000..1000000. Default 0.
---@field ay? number # Vertical acceleration in pixels/second squared. Range -1000000..1000000. Default 0.
---@field radius? number # Circular sweep radius, independent of display size. Range 0.001..256. Default 2.
---@field life? number # Lifetime in seconds. Range 0.001..3600. Default 3.
---@field mask? integer # Entity category mask; zero skips targets. Terrain uses the independent terrain flag. Range 0..4294967295. Default 4294967295.
---@field color? integer # Packed numeric 0xRRGGBBAA, not a hex string. Range 0..4294967295. Default 4294967295.
---@field sprite? integer # Registered room-local projectile sprite ID; zero is a radius-sized colored quad. Range 0..64. Default 0.
---@field terrain? boolean # Enable terrain sweep independently of mask. Default true.
---@field piercing? boolean # Continue past entity hits; blocking terrain still stops the projectile. Default false.

---@class ScProjectileHit
-- Fields generated from native --api.
---@field projectile integer # Read-only. Room-local projectile sequence number. Range 1..4503599627370495.
---@field target ScEntityId # Read-only. Target entity handle; zero means terrain. Range 0..4503599627370495.
---@field fraction number # Read-only. Sweep fraction within the completed simulation step. Range 0..1.
---@field x number # Read-only. Projectile center X at impact, in world pixels. Range -1000000..1000000.
---@field y number # Read-only. Projectile center Y at impact, in world pixels. Range -1000000..1000000.

---@class ScAttachmentOffset
-- Fields generated from native --api.
---@field x? number # Finite local offset; x/y from parent's unrotated top-left, angle in radians. Dimensions/flip are not inherited. Range -1000000..1000000. Default 0.
---@field y? number # Finite local offset; x/y from parent's unrotated top-left, angle in radians. Dimensions/flip are not inherited. Range -1000000..1000000. Default 0.
---@field angle? number # Finite local offset; x/y from parent's unrotated top-left, angle in radians. Dimensions/flip are not inherited. Range -1000000..1000000. Default 0.

---@class ScMusicOptions
-- Fields generated from native --api.
---@field volume? number # Target gain; a fade interpolates the current gain. Range 0..1. Default 1.
---@field pitch? number # Playback speed multiplier; also advances the logical clock. Range 0.25..4. Default 1.
---@field pan? number # Stereo position: -1 left, 0 center, 1 right. Range -1..1. Default 0.
---@field fade? number # Seconds remaining to reach target volume; play starts a nonzero fade at silence. Range 0..60. Default 0.
---@field loop? boolean # Repeat when duration is reached. Default false.
---@field paused? boolean # Pause playback, logical position and voice fade. Default false.
---@field priority? integer # Only equal/lower-priority voices can be stolen; then lowest priority, oldest age, lowest handle wins. Range -128..127. Default 0.
---@field bus? 'master'|'music'|'sfx'|'ui' # Routing category. Music defaults to music, sounds to sfx; master routing does not multiply master gain twice. Default "music".

---@class ScInputCompositionSegment
-- Fields generated from native --api.
---@field start integer # Read-only. One-based UTF-8 start; at a codepoint boundary.
---@field finish integer # Read-only. Exclusive end, greater than start; at a codepoint boundary.
---@field kind 'input'|'target_converted'|'converted'|'target_unconverted'|'error'|'fixed' # Read-only. IME conversion status; adjacent same-kind runs can be separate clauses.

---@class ScImageSlice
-- Fields generated from native --api.
---@field left? number # Source pixels; finite. Range 0..8192. Default 0.
---@field right? number # Source pixels; finite. Range 0..8192. Default 0.
---@field top? number # Source pixels; finite. Range 0..8192. Default 0.
---@field bottom? number # Source pixels; finite. Range 0..8192. Default 0.

---@class ScImageOptions
-- Fields generated from native --api.
---@field source_x? number # Source pixels; finite. Range 0..8192. Default 0.
---@field source_y? number # Source pixels; finite. Range 0..8192. Default 0.
---@field source_w? number # Source pixels; finite. Range 0..8192. Default 0.
---@field source_h? number # Source pixels; finite. Range 0..8192. Default 0.
---@field flip_x? boolean # Diagonal exchanges source axes after flips; screen selects logical viewport coordinates. Default false.
---@field flip_y? boolean # Diagonal exchanges source axes after flips; screen selects logical viewport coordinates. Default false.
---@field diagonal? boolean # Diagonal exchanges source axes after flips; screen selects logical viewport coordinates. Default false.
---@field screen? boolean # Diagonal exchanges source axes after flips; screen selects logical viewport coordinates. Default false.
---@field color? string # #RRGGBB or #RRGGBBAA tint, including opacity. Default "#FFFFFFFF".
---@field layer? integer # Omitted preserves submission order; present joins stable scene sorting and forbids nesting in UI clips. Range -32768..32767.
---@field material? integer|false # Requires materials capability. Live surface handle or false for builtin shader; omitted inherits image binding.
---@field slice? ScImageSlice # Nine-slice source insets. Omitted draws one stretched image; consumes one draw command either way.

---@class ScImageRequestStatus
-- Fields generated from native --api.
---@field request integer # Read-only. Room transaction ID.
---@field status 'pending'|'ready'|'failed' # Read-only. Host-observed preparation state.
---@field count integer # Read-only. Candidate resource names; 0..128.
---@field error? string # Read-only. Present only when failed.

---@class ScImageCacheStats
-- Fields generated from native --api.
---@field capacity integer # Read-only. Application CPU cache counter; pinned counts distinct referenced images, pending includes reserved jobs not yet queued.
---@field budget_bytes integer # Read-only. Application CPU cache counter; pinned counts distinct referenced images, pending includes reserved jobs not yet queued.
---@field resident_bytes integer # Read-only. Application CPU cache counter; pinned counts distinct referenced images, pending includes reserved jobs not yet queued.
---@field resident integer # Read-only. Application CPU cache counter; pinned counts distinct referenced images, pending includes reserved jobs not yet queued.
---@field pinned integer # Read-only. Application CPU cache counter; pinned counts distinct referenced images, pending includes reserved jobs not yet queued.
---@field pending integer # Read-only. Application CPU cache counter; pinned counts distinct referenced images, pending includes reserved jobs not yet queued.

---@class ScSaveReadResult
-- Fields generated from native --api.
---@field record? ScSaveRecord # Read-only. Complete checkpoint summary; absent only when both slot and backup are missing.
---@field chunks table<string,table<string,ScData>> # Read-only. Requested chunks from that same snapshot; missing keys omitted. Combined encoded size at most 1 MiB including key/container allowance.

---@class ScSaveStatus
-- Fields generated from native --api.
---@field request integer # Read-only. Live request ID, 1..2^52-1.
---@field operation 'read'|'write' # Read-only. Operation accepted by the application IO worker.
---@field status 'pending'|'complete'|'failed' # Read-only. Published by the host at a fixed boundary; Lua never observes raw worker timing.
---@field error? string # Read-only. Present only on failure; retry preserves the original frozen payload.

---@class ScStreamFailure
-- Fields generated from native --api.
---@field sequence integer # Read-only. Original ordered request sequence, 1..2^52-1; pass to retry.
---@field frame integer # Read-only. Original scheduled commit frame, 0..2^52-1; retry does not move it.
---@field x integer # Read-only. Chunk column, -31250..31250.
---@field y integer # Read-only. Chunk row, -31250..31250.
---@field message string # Read-only. Read/decode/validation diagnostic from the failed chunk.

---@class ScJointControlPatch
-- Fields generated from native --api.
---@field limit? boolean # Initially true only for prismatic joints.
---@field motor? boolean # Initially false. Enabling with zero max_effort produces no drive.
---@field lower? number # Distance .16..4096 pixels (initial .16); prismatic -4096..4096 pixels (initial 0); revolute -pi..pi radians (initial -pi). Range -4096..4096.
---@field upper? number # Same kind-dependent range as lower. Initial distance 4096, prismatic creation length, revolute pi. Range -4096..4096.
---@field speed? number # Initial 0. Pixels/second for distance and prismatic; radians/second for revolute. Range -1000000..1000000.
---@field max_effort? number # Initial 0. Mass*pixel/second^2 for linear joints; mass*pixel^2/second^2 for revolute. Range 0..1000000000.

---@class ScSaveSlot
-- Fields generated from native --api.
---@field slot string # Read-only. ASCII slot name, 1..128 bytes; list sorts lexicographically.
---@field valid boolean # Read-only. True if either the primary or complete backup validates.
---@field scene? string # Read-only. Present only for valid slots.
---@field data_version? integer # Read-only. Present only for valid slots.
---@field frame? integer # Read-only. Saved simulation frame, 0..9007199254740991; absent in records written without metadata.
---@field saved_at? integer # Read-only. Unix seconds, 0..9007199254740991; not deterministic gameplay state. Optional for native-authored records.
---@field error? string # Read-only. Present only for invalid slots; no partial state is returned.

---@class ScSaveRecord
-- Fields generated from native --api.
---@field format integer # Read-only. Current native save format; see --api save.format.
---@field project string # Read-only. Matches project.id.
---@field data_version integer # Read-only. Must match project.data_version (default 1); no migration.
---@field scene string # Read-only. Project-relative .lua entry to reconstruct.
---@field state table<string,ScData> # Read-only. Explicit shared state, at most 256 KiB and depth 16.
---@field frame? integer # Read-only. Saved simulation frame, 0..9007199254740991; absent in records written without metadata.
---@field saved_at? integer # Read-only. Unix seconds, 0..9007199254740991; not deterministic gameplay state. Optional for native-authored records.
---@field chunk_count integer # Read-only. 0..16384; native chunk references are not exposed.

---@class ScInputSnapshot
-- Fields generated from native --api.
---@field keys ScKey[] # Read-only. Held keyboard keys in catalog order.
---@field key_pressed ScKey[] # Read-only. Press edges, including taps between fixed updates.
---@field key_released ScKey[] # Read-only. Release edges, including focus-loss releases.
---@field gamepad ScInputSelectedGamepad # Read-only. Selected controller; use pads for explicit stable slots.
---@field button_pressed ScGamepadButton[] # Read-only. Selected-controller press edges.
---@field button_released ScGamepadButton[] # Read-only. Selected-controller release edges.
---@field pads ScInputPad[] # Read-only. Exactly four records; Lua indices 1..4 are stable slots.
---@field mouse ScInputMouse # Read-only. Pointer, button and wheel snapshot.
---@field composition_edit ScInputCompositionEdit # Read-only. Resolved positions within composition, even if replay omitted metadata.
---@field composition_segments ScInputCompositionSegment[] # Read-only. Ordered nonempty contiguous runs covering preedit; an empty array means unavailable metadata.
---@field composition_segments_truncated boolean # Read-only. Native segmentation exceeded 128 runs; preserves preedit but clears segments.
---@field text string # Read-only. UTF-8 committed text, preedit text or recorded paste payload; empty when absent. At most 4095 bytes.
---@field composition string # Read-only. UTF-8 committed text, preedit text or recorded paste payload; empty when absent. At most 4095 bytes.
---@field clipboard string # Read-only. UTF-8 committed text, preedit text or recorded paste payload; empty when absent. At most 4095 bytes.

---@class ScInputSelectedGamepad
-- Fields generated from native --api.
---@field connected boolean # Read-only. Connected in this snapshot.
---@field buttons ScGamepadButton[] # Read-only. Held button names in catalog order.
---@field axes ScInputAxes # Read-only. All six axes, including zero values.

---@class ScInputPad
-- Fields generated from native --api.
---@field connected boolean # Read-only. Connected in this snapshot.
---@field buttons ScGamepadButton[] # Read-only. Held button names in catalog order.
---@field axes ScInputAxes # Read-only. All six axes, including zero values.
---@field pressed ScGamepadButton[] # Read-only. Press edges; a quick tap may be both pressed and released.
---@field released ScGamepadButton[] # Read-only. Release edges, including disconnection releases.

---@class ScInputMouse
-- Fields generated from native --api.
---@field x number # Read-only. Logical viewport position or movement delta.
---@field y number # Read-only. Logical viewport position or movement delta.
---@field dx number # Read-only. Logical viewport position or movement delta.
---@field dy number # Read-only. Logical viewport position or movement delta.
---@field wheel_x number # Read-only. Accumulated wheel delta.
---@field wheel_y number # Read-only. Accumulated wheel delta.
---@field inside boolean # Read-only. False outside the viewport, including black bars.
---@field buttons ScMouseButton[] # Read-only. Mouse button names in catalog order.
---@field pressed ScMouseButton[] # Read-only. Mouse button names in catalog order.
---@field released ScMouseButton[] # Read-only. Mouse button names in catalog order.

---@class ScInputCompositionEdit
-- Fields generated from native --api.
---@field cursor integer # Read-only. One-based UTF-8 insertion position.
---@field start integer # Read-only. Start of first contiguous target range.
---@field finish integer # Read-only. Exclusive target end.

---@class ScInputAxes
-- Fields generated from native --api.
---@field left_x number # Read-only. Raw normalized axis, before any deadzone. Range -1..1.
---@field left_y number # Read-only. Raw normalized axis, before any deadzone. Range -1..1.
---@field right_x number # Read-only. Raw normalized axis, before any deadzone. Range -1..1.
---@field right_y number # Read-only. Raw normalized axis, before any deadzone. Range -1..1.
---@field left_trigger number # Read-only. Raw normalized axis, before any deadzone. Range 0..1.
---@field right_trigger number # Read-only. Raw normalized axis, before any deadzone. Range 0..1.

---@class ScNavigationPoint
-- Fields generated from native --api.
---@field x integer # Read-only. Zero-based grid column.
---@field y integer # Read-only. Zero-based grid row.

---@class ScNavigationPath
-- Fields generated from native --api.
---@field status 'ok'|'unreachable'|'budget_exhausted' # Read-only. Search outcome.
---@field visited integer # Read-only. Expanded node count; blocked endpoints visit zero nodes.
---@field points ScNavigationPoint[] # Read-only. Inclusive start-to-goal sequence on success; empty for unreachable or exhausted queries.

---@class ScAudioBusState
-- Fields generated from native --api.
---@field volume number # Read-only. Current bus gain during interpolation. Range 0..1.
---@field fade number # Read-only. Seconds remaining to reach the bus target volume. Range 0..60.
---@field paused boolean # Read-only. Pause affected voices; bus fades continue advancing.
---@field target number # Read-only. Target bus gain; preferences multiply independently. Range 0..1.

---@class ScAudioBusOptions
-- Fields generated from native --api.
---@field volume? number # Target gain; a fade interpolates the current gain. Range 0..1. Default 1.
---@field fade? number # Seconds remaining to reach the bus target volume. Range 0..60. Default 0.
---@field paused? boolean # Pause affected voices; bus fades continue advancing. Default false.

---@class ScDisplayPose
-- Fields generated from native --api.
---@field x number # Read-only display geometry; pixels, angle in radians. Position/angle interpolate only in draw.
---@field y number # Read-only display geometry; pixels, angle in radians. Position/angle interpolate only in draw.
---@field angle number # Read-only display geometry; pixels, angle in radians. Position/angle interpolate only in draw.
---@field w number # Read-only display geometry; pixels, angle in radians. Position/angle interpolate only in draw.
---@field h number # Read-only display geometry; pixels, angle in radians. Position/angle interpolate only in draw.

---@class ScCameraState
-- Fields generated from native --api.
---@field x number # Unscaled viewport top-left anchor; -1e6..1e6, default 0. Zoom and rotation pivot about anchor + half viewport. Default 0.
---@field y number # Unscaled viewport top-left anchor; -1e6..1e6, default 0. Zoom and rotation pivot about anchor + half viewport. Default 0.
---@field zoom number # Magnification about the logical viewport center. Range 0.125..8. Default 1.
---@field rotation number # Clockwise screen rotation in radians; normalized on commit. Range -1000..1000. Default 0.
---@field smoothing number # Follow fraction per fixed update; 1 snaps, 0 holds. Range 0..1. Default 0.159999996423721.
---@field pixel_snap boolean # Floor effective camera origin and drawn object origins; disable for fractional motion. Default true.
---@field bounds 'map'|false|ScCameraRect # Map bounds, unbounded coordinates or a custom rectangle. Undersized axes align the visible top/left edge; shake may extend outside bounds. Default "map".
---@field target number # Read-only effective camera state; center, anchor and view_* values interpolate in draw.
---@field center_x number # Read-only effective camera state; center, anchor and view_* values interpolate in draw.
---@field center_y number # Read-only effective camera state; center, anchor and view_* values interpolate in draw.
---@field anchor_x number # Read-only effective camera state; center, anchor and view_* values interpolate in draw.
---@field anchor_y number # Read-only effective camera state; center, anchor and view_* values interpolate in draw.
---@field view_zoom number # Read-only effective camera state; center, anchor and view_* values interpolate in draw.
---@field view_rotation number # Read-only effective camera state; center, anchor and view_* values interpolate in draw.
---@field visible ScCameraRect # Conservative world AABB of rotated viewport, including shake.

---@class ScCameraRect
-- Fields generated from native --api.
---@field x number # X coordinate.
---@field y number # Y coordinate, positive down.
---@field w number # Positive extent; custom bounds .001..2e6, endpoints within -1e6..1e6.
---@field h number # Positive extent; custom bounds .001..2e6, endpoints within -1e6..1e6.

---@class ScCameraPoint
-- Fields generated from native --api.
---@field x number # X coordinate.
---@field y number # Y coordinate, positive down.

---@class ScCameraPatch
-- Fields generated from native --api.
---@field x? number # Unscaled viewport top-left anchor; -1e6..1e6, default 0. Zoom and rotation pivot about anchor + half viewport. Default 0.
---@field y? number # Unscaled viewport top-left anchor; -1e6..1e6, default 0. Zoom and rotation pivot about anchor + half viewport. Default 0.
---@field zoom? number # Magnification about the logical viewport center. Range 0.125..8. Default 1.
---@field rotation? number # Clockwise screen rotation in radians; normalized on commit. Range -1000..1000. Default 0.
---@field smoothing? number # Follow fraction per fixed update; 1 snaps, 0 holds. Range 0..1. Default 0.159999996423721.
---@field pixel_snap? boolean # Floor effective camera origin and drawn object origins; disable for fractional motion. Default true.
---@field bounds? 'map'|false|ScCameraRect # Map bounds, unbounded coordinates or a custom rectangle. Undersized axes align the visible top/left edge; shake may extend outside bounds. Default "map".

---@class ScPointLight
-- Fields generated from native --api.
---@field x number # World-space light center X. Range -1000000..1000000.
---@field y number # World-space light center Y; positive down. Range -1000000..1000000.
---@field radius? number # Attenuation radius, pixels. Range 0.001..1024. Default 128.
---@field height? number # Height above the 2D surface in pixels, used only for normal-map diffuse shading; does not change shadow geometry. Range 0.001..4096. Default 32.
---@field intensity? number # Linear intensity multiplied by color alpha; zero does not consume a visible-light slot. Range 0..1. Default 1.
---@field softness? number # Source radius, pixels; omitted inherits lighting.configure at submission. Ignored for non-shadow lights. Range 0..32.
---@field samples? integer # 1..8; omitted inherits lighting.configure. One sample is centered; ignored when shadows=false.
---@field shadows? boolean # Enable geometry occlusion and area samples; false draws an unoccluded point light. Default true.
---@field color? ScColor # RGB light color with alpha intensity. This does not whiten the supplied RGB. Default "#FFFFFFFF".
---@field ignore? integer # Live entity handle excluded from this light's occlusion; 0 means no exclusion. Default 0.

---@class ScParticleTexture
-- Fields generated from native --api.
---@field resource string # Declared PNG resource name, not a file path.
---@field x integer # Source left in pixels, 0..8192.
---@field y integer # Source top in pixels, 0..8192.
---@field w integer # Source width in pixels, 1..8192; entire region must fit image.
---@field h integer # Source height in pixels, 1..8192; entire region must fit image.

---@class ScParticleEmitterSpec
-- Fields generated from native --api.
---@field speed_min? number # Minimum speed, pixels/second; must not exceed speed_max. Range 0..1000000. Default 30.
---@field speed_max? number # Maximum speed, pixels/second. Range 0..1000000. Default 30.
---@field life_min? number # Minimum lifetime in seconds; must not exceed life_max. Range 0.001..60. Default 1.
---@field life_max? number # Maximum lifetime in seconds. Range 0.001..60. Default 1.
---@field angle_min? number # Minimum radians; 0 points right, pi/2 down; must not exceed angle_max. Range -1000000..1000000. Default 0.
---@field angle_max? number # Maximum radians. Range -1000000..1000000. Default 6.28318548202515.
---@field gravity? number # World gravity multiplier. Range -10..10. Default 0.150000005960464.
---@field curve? ScParticleCurveKey[] # 2..8 keys from time 0 to 1, strictly increasing. Default white size 2 at 0 to transparent white size 0 at 1.
---@field texture? ScParticleTexture # Declared PNG region; omitted means a white square. Curve size sets longest side, preserving source aspect ratio. Position anchors top-left.
---@field blend? 'alpha'|'additive' # Default alpha. Additive uses source alpha for color intensity; draw order is never regrouped by blend or texture.

---@class ScParticleCurveKey
-- Fields generated from native --api.
---@field time number # Normalized lifetime 0..1; endpoint keys must be 0 and 1.
---@field size number # Square side length in pixels, 0..4096.
---@field color integer # Numeric RGBA 0..4294967295; each channel interpolates linearly with byte rounding.

---@class ScIdentityResolution
-- Fields generated from native --api.
---@field status 'absent'|'unloaded'|'active'|'deleted'|'room_inactive' # Current room registry state; another room's object existence is not inferred.
---@field id? ScEntityId # Present only for an active entity in the current room. Never persist this handle.

---@class ScIdentityReference
-- Fields generated from native --api.
---@field room string # Project-relative .lua room entry path.
---@field persistent_id string # Nonempty immutable persistent object ID within that room.

---@class ScEntityEdit
-- Fields generated from native --api.
---@field id ScEntityId # Live runtime handle to patch.
---@field patch ScEntityPatch # Fields to replace; unspecified fields remain unchanged.

---@class ScEntityPatch
-- Fields generated from native --api.
---@field x? number # Left world position in pixels. Range -1000000..1000000. Default 0.
---@field y? number # Top world position in pixels; positive Y points down. Range -1000000..1000000. Default 0.
---@field w? number # Unrotated width in pixels. Range 0.001..4096. Default 8.
---@field h? number # Unrotated height in pixels. Range 0.001..4096. Default 8.
---@field vx? number # Horizontal velocity in pixels/second. Range -1000000..1000000. Default 0.
---@field vy? number # Vertical velocity in pixels/second. Range -1000000..1000000. Default 0.
---@field gravity? number # Multiplier of scene gravity. Range -100..100. Default 1.
---@field glow? number # Glow radius in pixels. Range 0..1024. Default 0.
---@field angle? number # Rotation about bounds center, in radians. Range -1000000..1000000. Default 0.
---@field angular_velocity? number # Angular velocity in radians/second. Range -1000..1000. Default 0.
---@field frame? integer # Zero-based sprite frame; validated against the image grid when loading assets. Range 0..65535. Default 0.
---@field frame_w? integer # Sprite frame width; zero uses the whole image. Range 0..4096. Default 0.
---@field frame_h? integer # Sprite frame height; zero uses the whole image. Range 0..4096. Default 0.
---@field layer? integer # Stable draw layer. Range -32768..32767. Default 0.
---@field dynamic? boolean # Shorthand for a dynamic box body; explicit body configuration takes precedence. Default false.
---@field solid? boolean # Enable body collision filtering and projectile targeting. Default true.
---@field flip_x? boolean # Flip the sprite horizontally. Default false.
---@field flip_y? boolean # Flip the sprite vertically. Default false.
---@field tag? string # Exact-match label; NUL bytes are forbidden. At most 47 bytes. Default "".
---@field sprite? string # Image resource name or project-relative image path; NUL bytes are forbidden. At most 127 bytes. Default "".
---@field persistent_id? string # Stable room-local persistent object ID. Empty is unnamed; nonempty uses ASCII letters, digits, underscore, dash, dot, slash or colon without empty/dot segments. Immutable after spawn. At most 127 bytes. Default "".
---@field color? ScColor # Hexadecimal #RRGGBB or #RRGGBBAA; reads return #RRGGBBAA. Default "#FFFFFFFF".
---@field body? ScBody|false # Explicit body configuration; false removes the body and clears dynamic. Omitted patches retain the previous body. Default false.

---@class ScEntity
-- Fields generated from native --api.
---@field x number # Left world position in pixels. Range -1000000..1000000.
---@field y number # Top world position in pixels; positive Y points down. Range -1000000..1000000.
---@field w number # Unrotated width in pixels. Range 0.001..4096.
---@field h number # Unrotated height in pixels. Range 0.001..4096.
---@field vx number # Horizontal velocity in pixels/second. Range -1000000..1000000.
---@field vy number # Vertical velocity in pixels/second. Range -1000000..1000000.
---@field gravity number # Multiplier of scene gravity. Range -100..100.
---@field glow number # Glow radius in pixels. Range 0..1024.
---@field angle number # Rotation about bounds center, in radians. Range -1000000..1000000.
---@field angular_velocity number # Angular velocity in radians/second. Range -1000..1000.
---@field frame integer # Zero-based sprite frame; validated against the image grid when loading assets. Range 0..65535.
---@field frame_w integer # Sprite frame width; zero uses the whole image. Range 0..4096.
---@field frame_h integer # Sprite frame height; zero uses the whole image. Range 0..4096.
---@field layer integer # Stable draw layer. Range -32768..32767.
---@field dynamic boolean # Shorthand for a dynamic box body; explicit body configuration takes precedence.
---@field solid boolean # Enable body collision filtering and projectile targeting.
---@field flip_x boolean # Flip the sprite horizontally.
---@field flip_y boolean # Flip the sprite vertically.
---@field tag string # Exact-match label; NUL bytes are forbidden. At most 47 bytes.
---@field sprite string # Image resource name or project-relative image path; NUL bytes are forbidden. At most 127 bytes.
---@field persistent_id string # Stable room-local persistent object ID. Empty is unnamed; nonempty uses ASCII letters, digits, underscore, dash, dot, slash or colon without empty/dot segments. Immutable after spawn. At most 127 bytes.
---@field color ScColor # Hexadecimal #RRGGBB or #RRGGBBAA; reads return #RRGGBBAA.
---@field body ScBody|false # Explicit body configuration; false removes the body and clears dynamic. Omitted patches retain the previous body.
---@field id ScEntityId # Read-only. Generation-checked runtime handle; cannot be persisted or patched.
---@field grounded boolean # Read-only. Ground contact from the preceding completed physics step.
---@field support ScEntityId # Read-only. Supporting entity; zero for map or none.
---@field normal_x number # Read-only. Ground normal X from the preceding completed physics step.
---@field normal_y number # Read-only. Ground normal Y; positive Y points down.

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
---@field entities? ScEntityPatch[] # Dense array bounded by project.limits.entities; default 4096 living entities.
---@field preload_images? string[] # Initial image names/paths, at most 128 distinct entries; default none. Requires streaming. Prepared after init and committed before first draw; includes bound normal maps.
---@field init? fun() # Runs once after config/entities are loaded.
---@field update? fun(dt: number) # Runs before physics, exactly dt=1/60 seconds.
---@field draw? fun(alpha: number) # Queue drawing only; alpha=0..1. Keep Lua state unchanged.
---@field ui_update? fun(dt: number) # UI before gameplay; live once per host frame, replay/headless before each fixed update, including waits. dt=0..0.25; gameplay mutations forbidden. See docs/ui-lifecycle.md.
-- A .lua scene file MUST return a ScScene. Unknown scene/map/entity keys error.
-- Scene, map and entity configs/patches must be plain tables without metatables.
-- Seed belongs to the host (--seed), not the scene table. Each scene is self-contained.
-- Entity/world mutation belongs in init/update. Use sc.state for cross-room data; Lua locals reset.
-- Scene loads are transactional at the host: a failed scene never replaces the old one.

---@class ScAPI
sc = {}

---Create an entity; unspecified fields use defaults.
---Phases: load, init, update.
---Capacity: project.limits.entities.
---@param entity ScEntityPatch
---@return ScEntityId
function sc.spawn(entity) end

---Return an independent entity snapshot; stale or unknown handles raise errors.
---Phases: load, init, update, draw, ui_update.
---@param id ScEntityId
---@return ScEntity
function sc.get(id) end

---Atomically apply writable fields; invalid IDs or fields raise errors.
---Phases: load, init, update.
---@param id ScEntityId
---@param patch ScEntityPatch
function sc.set(id, patch) end

---Destroy a live generation-checked entity; returns true on success.
---Phases: load, init, update.
---@param id ScEntityId
---@return boolean
function sc.destroy(id) end

---Find the first living entity with an exact nonempty tag, at most 47 bytes.
---Phases: load, init, update, draw, ui_update.
---@param tag string
---@return ScEntityId|nil
function sc.find(tag) end

---Test axis-aligned entity bounds.
---Phases: load, init, update, draw, ui_update.
---@param a ScEntityId
---@param b ScEntityId
---@return boolean
function sc.overlap(a, b) end

---Read held input.
---Phases: load, init, update, draw, ui_update.
---Capacity: Built-in action snapshot; press/release bits are zero in ui_update.
---@param action ScAction # Built-in left/right/up/down/jump/action bit; distinct from Lua named actions.
---@return boolean
function sc.down(action) end

---Read this tick's press edge.
---Phases: load, init, update, draw, ui_update.
---Capacity: Built-in action snapshot; press/release bits are zero in ui_update.
---@param action ScAction # Built-in left/right/up/down/jump/action bit; distinct from Lua named actions.
---@return boolean
function sc.pressed(action) end

---Read this tick's release edge.
---Phases: load, init, update, draw, ui_update.
---Capacity: Built-in action snapshot; press/release bits are zero in ui_update.
---@param action ScAction # Built-in left/right/up/down/jump/action bit; distinct from Lua named actions.
---@return boolean
function sc.released(action) end

---Read or write a zero-based ASCII map cell; outside reads return # in bounded rooms and . in unbounded worlds.
---Phases: load, init, update, draw, ui_update.
---Capacity: Out-of-map reads: # for bounded rooms, . for unbounded streamed worlds.
---When tile is supplied (including nil), phases: load, init, update.
---@param x integer # Zero-based cell column. Range -1000000..1000000.
---@param y integer # Zero-based cell row. Range -1000000..1000000.
---@param tile? ScTile # Supplying the third argument writes an in-bounds cell; nil is rejected.
---@return ScTile
function sc.tile(x, y, tile) end

---Seeded randomness: [0,1), inclusive integer bounds, or float range.
---Phases: load, init, update.
---Capacity: Exactly zero or two arguments; advances gameplay RNG, independent of particle/visual RNG.
---@param min? number # Both bounds required together; no arguments uses [0,1). Range -1000000..1000000.
---@param max? number # Must be >= min. Both Lua integers select an inclusive integer interval; otherwise floating interpolation. Range -1000000..1000000.
---@return number
function sc.random(min, max) end

---Atomically emit 0..32768 particles within project.limits.particles. Exhaustion reports used/requested/capacity without advancing visual RNG. Defaults speed30 life0.5.
---Phases: load, init, update.
---Capacity: project.limits.particles; failure preserves live particles and visual RNG.
---@param x number # Finite world/screen X in pixels. Range -1000000..1000000.
---@param y number # Finite world/screen Y in pixels. Range -1000000..1000000.
---@param count integer # Atomic burst, limited further by remaining project particle capacity. Range 0..32768.
---@param color ScColor # #RRGGBB or #RRGGBBAA string, not packed integer.
---@param speed? number # Omitted uses 30; explicit nil rejected. Range 0..1000000. Default 30.
---@param life? number # Seconds; omitted uses 0.5, explicit nil rejected. Range 0.001..60. Default 0.5.
function sc.emit(x, y, count, color, speed, life) end

---Queue a synthesized tone; defaults duration0.1 volume0.2.
---Phases: load, init, update.
---Capacity: 32 queued tones per tick; no audio device required for validation.
---@param frequency number # Hertz. Range 20..20000.
---@param duration? number # Seconds; omitted uses 0.1, explicit nil rejected. Range 0.001..10. Default 0.1.
---@param volume? number # Gain; omitted uses 0.2, explicit nil rejected. Range 0..1. Default 0.2.
function sc.tone(frequency, duration, volume) end

sc.camera = {}

---Set the persistent HUD message (191 UTF-8 bytes maximum).
---Phases: load, init, update.
---@param text string # At most 191 bytes without NUL; empty clears the room HUD message.
function sc.message(text) end

---Request a transactional scene switch after this callback; release any asynchronous save request first.
---Phases: load, init, update.
---Capacity: Optional state: 256 KiB, depth 16. Release async save and commit/cancel pending image changes first.
---@param path string # Project-relative .lua path, 1..511 bytes; no empty, dot or parent segments, backslashes or NUL.
---@param state? table<string,ScData> # Omitted inherits current shared state; supplied plain object replaces it on successful commit. Explicit nil rejected.
function sc.scene(path, state) end

---Queue a filled rectangle during draw; screen defaults false.
---Phases: draw.
---Capacity: project.limits.draws.
---@param x number # Finite world/screen X in pixels. Range -1000000..1000000.
---@param y number # Finite world/screen Y in pixels. Range -1000000..1000000.
---@param w number # Filled width. Range 0..1000000.
---@param h number # Filled height. Range 0..1000000.
---@param color ScColor # #RRGGBB or #RRGGBBAA string, not packed integer.
---@param screen? boolean # Logical viewport coordinates when true; omitted uses world coordinates. Explicit nil is rejected. Default false.
function sc.rect(x, y, w, h, color, screen) end

---Queue a filled circle during draw.
---Phases: draw.
---Capacity: project.limits.draws.
---@param x number # Finite world/screen X in pixels. Range -1000000..1000000.
---@param y number # Finite world/screen Y in pixels. Range -1000000..1000000.
---@param radius number # Circle radius. Range 0..4096.
---@param color ScColor # #RRGGBB or #RRGGBBAA string, not packed integer.
---@param screen? boolean # Logical viewport coordinates when true; omitted uses world coordinates. Explicit nil is rejected. Default false.
function sc.circle(x, y, radius, color, screen) end

---Queue UTF-8 text (511 bytes) during draw; options font, wrap=0 (0..4096), align=0 (0 left/1 center/2 right).
---Phases: draw.
---Capacity: project.limits.draws; font metrics shared with measure.
---@param text string # Valid UTF-8, at most 511 bytes; no NUL.
---@param x number # Finite world/screen X in pixels. Range -1000000..1000000.
---@param y number # Finite world/screen Y in pixels. Range -1000000..1000000.
---@param size number # Logical pixel size. Range 1..512.
---@param color ScColor # #RRGGBB or #RRGGBBAA string, not packed integer.
---@param screen? boolean # Logical viewport coordinates when true; omitted uses world coordinates. Explicit nil is rejected. Default false.
---@param options? ScTextOptions|nil # Omitted/nil uses defaults. To pass options, provide an explicit screen boolean first.
function sc.text(text, x, y, size, color, screen, options) end

---Return completed fixed simulation steps.
---Phases: load, init, update, draw, ui_update.
---Capacity: Completed room ticks; advances while app.pause is true, resets on room replacement.
---@return integer
function sc.tick() end

---Return fixed simulation time in seconds.
---Phases: load, init, update, draw, ui_update.
---Capacity: Room tick / 60; not wall-clock or network time.
---@return number
function sc.time() end

---Write diagnostics to stderr, preserving headless JSON on stdout.
---Phases: load, init, update, draw, ui_update.
---@param text string # At most 4096 bytes without NUL; exactly one string, not Lua print-style varargs.
function sc.log(text) end

---@alias ScNetChannel 'reliable'|'state'
---@class ScNetEvent
-- Fields generated from native --api.
---@field type 'connect'|'receive'|'disconnect' # Read-only. Event discriminator; no uniform ordering across channels.
---@field peer integer # Read-only. Endpoint-local connection ID. Failed connection attempts may disconnect with peer zero. Range 0..4294967295.
---@field data? string # Read-only. Receive only; binary-safe, including NUL. At most 1200 bytes.
---@field channel? ScNetChannel # Read-only. Receive only.
---@field reason? integer # Read-only. Disconnect only; application reason or zero for transport loss. Range 0..4294967295.

---@class ScNetSession
local session = {}

---Read one event without waiting. Named sessions expose at most 64 queued events per fixed update; local sessions service transport directly. Forbidden in draw/ui_update, check mode and candidate initialization.
---Phases: load, init, update.
---Capacity: Named receive FIFO 256; at most 64 readable events per fixed update. Nil alone means empty; nil,error means failure.
---@return ScNetEvent|nil
---@return string|nil
function session:poll() end

---Queue 0..1200 binary bytes; reliable is ordered, state is unreliable sequenced. Peer 0 broadcasts; forbidden in draw/ui_update, check mode and candidate initialization.
---Phases: load, init, update.
---Capacity: Named session: 64 successful sends / 65536 bytes per tick; broadcast charges one payload. Transport: 256 queued commands per peer.
---@param peer integer # Zero broadcasts to established peers; fails when none exist. Range 0..4294967295.
---@param data string # Binary string, 0..1200 bytes including NUL; never a Lua table.
---@param channel? ScNetChannel|nil # Omitted/nil uses reliable. State is unreliable sequenced on a separate channel. Default "reliable".
---@return boolean|nil
---@return string|nil
function session:send(peer, data, channel) end

---Send queued outgoing packets without waiting; forbidden in draw/ui_update, check mode and candidate initialization.
---Phases: load, init, update.
---Capacity: Network side effects require load/init/update outside check mode and candidate initialization; forbidden in draw/ui_update.
---@return boolean|nil
---@return string|nil
function session:flush() end

---Begin graceful disconnect; keep polling for disconnect. Forbidden in draw/ui_update, check mode and candidate initialization.
---Phases: load, init, update.
---Capacity: Network side effects require load/init/update outside check mode and candidate initialization; forbidden in draw/ui_update.
---@param peer integer # Live endpoint-local peer ID, not an entity handle. Range 1..4294967295.
---@param reason? integer|nil # Omitted/nil uses zero. Range 0..4294967295. Default 0.
---@return boolean|nil
---@return string|nil
function session:disconnect(peer, reason) end

---Immediately release the socket and pending packets; idempotent. Local sessions also close on collection; named sessions survive VM close. Forbidden in draw/ui_update, check mode and candidate initialization.
---Phases: load, init, update.
---Capacity: Network side effects require load/init/update outside check mode and candidate initialization; forbidden in draw/ui_update.
function session:close() end

---Read a copy or atomically replace named session protocol state (64 KiB JSON, depth 16). Nil clears it; excluded from saves and automatic trace. Writes forbidden in draw/ui_update, check mode and candidate initialization.
---Phases: load, init, update, draw, ui_update.
---Capacity: Named sessions only; 64 KiB JSON, depth 16; excluded from checkpoints/automatic trace.
---When value is supplied (including nil), phases: load, init, update.
---@param value? table<string,ScData>|nil # Omitted reads a copy; explicit nil clears; a plain object atomically replaces protocol state.
---@return table<string,ScData>|boolean|nil
---@return string|nil
function session:state(value) end

---@class ScNetStats
-- Fields generated from native --api.
---@field queued integer # Read-only. Buffered events, including the readable prefix. Range 0..256.
---@field readable integer # Read-only. Events remaining in the current fixed-update batch. Range 0..64.
---@field receive_capacity integer # Read-only. Allocated FIFO event capacity. Range 256..256.
---@field service_budget integer # Read-only. Maximum transport events read per host service iteration. Range 64..64.
---@field receive_tick_budget integer # Read-only. Maximum events published per fixed update. Range 64..64.
---@field send_remaining integer # Read-only. Successful sends remaining this tick; failed calls do not consume budget. Range 0..64.
---@field send_bytes_remaining integer # Read-only. Application payload bytes remaining this tick. Range 0..65536.
---@field state_bytes integer # Read-only. Serialized protocol object size; zero when cleared. Range 0..65536.
---@field state_capacity integer # Read-only. Protocol-state JSON byte limit. Range 65536..65536.
---@field open boolean # Read-only. False for a retained terminal entry; stats/state remain readable until close.
---@field error? string # Read-only. Terminal queue/transport diagnostic, omitted when healthy.

---Read named application session queue, remaining tick budgets and terminal error; unavailable on local sessions.
---Phases: load, init, update, draw, ui_update.
---Capacity: Named sessions only; terminal entries remain inspectable until explicit close.
---@return ScNetStats|nil
---@return string|nil
function session:stats() end

---Read the local UDP port; a closed session returns nil and an error.
---Phases: load, init, update, draw, ui_update.
---Capacity: Local bound UDP port, 1..65535.
---@return integer|nil
---@return string|nil
function session:port() end

---Read round-trip milliseconds for a connected local peer ID.
---Phases: load, init, update, draw, ui_update.
---Capacity: Nonnegative round-trip estimate in milliseconds.
---@param peer integer # Live endpoint-local peer ID, not an entity handle. Range 1..4294967295.
---@return integer|nil
---@return string|nil
function session:rtt(peer) end

sc.net = {}
---@type boolean # False in the default build; true with -DSHINY_NETWORK=ON.
sc.net.available = false

---Read monotonic application seconds sampled at the last fixed-update boundary; room changes do not reset it. Not replay time.
---Phases: load, init, update, draw, ui_update.
---@return number|nil
---@return string|nil
function sc.net.time() end

---Issue 128 OS-random bits as 32 lowercase hex characters; 64 attempts per application fixed update. Independent of gameplay RNG. Forbidden in draw/ui_update, check mode and candidate initialization.
---Phases: load, init, update.
---Capacity: 64 OS-random token attempts per application fixed update; each success is 32 lowercase hex characters.
---@return string|nil
---@return string|nil
function sc.net.token() end

---Bind numeric IPv4; port 0 selects a free port. Maximum 4 combined local/named sessions per VM context and 32 peers per host; forbidden in draw/ui_update, check mode and candidate initialization.
---Phases: load, init, update.
---Capacity: 4 combined live local/named sessions per VM context; host peers 1..32.
---@param bind_ipv4 string # Numeric dotted IPv4 only; 0.0.0.0 binds all interfaces, 127.0.0.1 loopback.
---@param port integer # Local port; zero asks the OS for a free port. Range 0..65535.
---@param max_peers? integer|nil # Omitted/nil uses 8. Range 1..32. Default 8.
---@return ScNetSession|nil
---@return string|nil
function sc.net.host(bind_ipv4, port, max_peers) end

---Start an asynchronous IPv4 connection; poll both endpoints until connect. Forbidden in draw/ui_update, check mode and candidate initialization.
---Phases: load, init, update.
---Capacity: 4 combined live local/named sessions per VM context.
---@param ipv4 string # Numeric dotted IPv4 only; no DNS.
---@param port integer # Remote port. Range 1..65535.
---@return ScNetSession|nil
---@return string|nil
function sc.net.join(ipv4, port) end

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
---@field stream? boolean # Image-only, default false. Requires streaming; prepare/commit through sc.images before use.
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
-- Fields generated from native --api.
---@field width? integer # Window pixel width; initial engine default 1152. Project/persisted settings may override. Range 320..7680.
---@field height? integer # Window pixel height; initial engine default 648. Range 180..4320.
---@field mode? 'windowed'|'borderless' # Initial engine mode is windowed.
---@field scale? 'integer'|'smooth' # Initial engine scale is integer.
---@field vsync? boolean # Initial engine value is true.
---@field volume? ScVolumePatch # Four logical gains; patch merges named buses, omitted buses retain their gain.
---@field bindings? table # Plain string-keyed object. Patch replaces the entire object; initial value is empty. Gameplay modules own binding semantics. At most 16384 bytes.

sc.settings = {}
---Copy current application settings, including nested volume and bindings. Omitted patch fields retain these values.
---Phases: load, init, update, draw, ui_update.
---@return ScSettings
function sc.settings.get() end
---Apply a settings patch, optionally persist it. Semantic/device/I/O failure returns nil,error and retains logical settings; success returns true and clears error. Bad arity/types, unconvertible data or forbidden phase raise. Update outside check mode only.
---Phases: update.
---Capacity: bindings: 16 KiB serialized plain object; all input: 256 KiB, depth 16.
---@param patch ScSettingsPatch # Partial plain table. Missing fields retain current values; volume merges by bus, bindings replaces the whole object.
---@param persist? boolean|nil # Omitted/nil means true. Without project.id/save root, changes remain in memory. Default true.
---@return boolean|nil
---@return string|nil
function sc.settings.apply(patch, persist) end
---Read last service load/application error; empty when none. Rejected Lua call boundaries do not replace it.
---Phases: load, init, update, draw, ui_update.
---@return string
function sc.settings.error() end

---Cached project-local module; nil return becomes true. Cycles fail; failed loads clear their loading marker for retry. Cached reads preserve returned object identity.
---Phases: load, init, update, draw, ui_update.
---Capacity: First load only in load/init/update; cached reads also allowed in draw/ui_update.
---@param module string # 1..128 bytes: letters, digits, underscore and dots; must resolve to a project-relative Lua path. shiny.* resolves under lib/.
---@return any
function require(module) end

sc.state = {}
---Read explicit cross-room state by key. Returns an independent nested copy, or nil when absent.
---Phases: load, init, update, draw, ui_update.
---@param key string # 1..128 bytes without NUL.
---@return boolean|number|string|table|nil
function sc.state.get(key) end
---Atomically replace a key, or delete it with explicit nil. Failure retains all prior state. Candidate rooms mutate their own copy, published only on room commit. Forbidden in draw/ui_update/migration.
---Phases: load, init, update.
---Capacity: Total state: 256 KiB serialized UTF-8 and depth 16; conversion also bounded.
---@param key string # 1..128 bytes without NUL.
---@param value boolean|number|string|table|nil # Copied plain UTF-8 data; nil deletes. Finite numbers, dense arrays or string-keyed objects; no metatables.
function sc.state.set(key, value) end

sc.save = {}
---Atomic versioned checkpoint of state and scene; update only, disabled in check. Requires project.id. Headless defaults to 16 in-memory slots. Existing chunks survive; disk calls are synchronous.
---Phases: update.
---Capacity: 16 in-memory slots; 4 MiB disk index; 256 KiB state.
---@param slot string # 1..128 ASCII letters, digits, underscores or hyphens; project.id supplies the namespace.
---@return boolean|nil ok # True on success, nil on an expected save/load failure.
---@return string|nil error # Diagnostic on failure; absent on success.
function sc.save.write(slot) end
---Validate and request room reconstruction with restored state, using only the current explicit format/data version. Update only, disabled in check; requires project.id. True means transition requested, not committed. Does not restore VM or solver state.
---Phases: update.
---Capacity: 16 in-memory slots; 4 MiB disk index; 256 KiB state.
---@param slot string # 1..128 ASCII letters, digits, underscores or hyphens; project.id supplies the namespace.
---@return boolean|nil ok # True on success, nil on an expected save/load failure.
---@return string|nil error # Diagnostic on failure; absent on success.
function sc.save.load(slot) end

sc.physics = {}
---Queue center force for the next sync; repeated calls accumulate. Only dynamic bodies respond.
---Phases: load, init, update.
---@param id ScEntityId # Live room entity handle; never persist runtime handles. Range 1..4503599627370495.
---@param x number # Center force X, mass*pixel/second^2. Range -1000000..1000000.
---@param y number # Center force Y, mass*pixel/second^2. Range -1000000..1000000.
function sc.physics.force(id, x, y) end
---Queue center impulse for the next sync; repeated calls accumulate. Only dynamic bodies respond.
---Phases: load, init, update.
---@param id ScEntityId # Live room entity handle; never persist runtime handles. Range 1..4503599627370495.
---@param x number # Center impulse X, mass*pixel/second. Range -1000000..1000000.
---@param y number # Center impulse Y, mass*pixel/second. Range -1000000..1000000.
function sc.physics.impulse(id, x, y) end
---Ignore one-way contacts for a bounded time; zero cancels the exclusion.
---Phases: load, init, update.
---@param id ScEntityId # Live room entity handle; never persist runtime handles. Range 1..4503599627370495.
---@param seconds? number # Replace the remaining one-way exclusion time; nil uses the default. Range 0..10. Default 0.2.
function sc.physics.drop(id, seconds) end

---@class ScContact
-- Fields generated from native --api.
---@field a ScEntityId # Read-only. First runtime entity ID; 0 denotes terrain. May already be destroyed when reading. Range 0..4503599627370495.
---@field b ScEntityId # Read-only. Second runtime entity ID; 0 denotes terrain. Range 0..4503599627370495.
---@field nx number # Read-only. Normal X from a to b; sensors use zero. Range -1..1.
---@field ny number # Read-only. Normal Y from a to b; sensors use zero. Range -1..1.
---@field sensor boolean # Read-only. True for sensor overlap edges.
---@field phase 'contact'|'begin'|'end' # Read-only. Solids use contact; sensors use begin/end.
---Independent snapshot of previous completed step contacts, sorted by a,b,phase. Does not synchronize bodies. Destroyed shapes may omit sensor end events.
---Phases: load, init, update, draw, ui_update.
---Capacity: project.limits.contacts; at most 64 contacts per body.
---@return ScContact[]
function sc.physics.contacts() end

---@class ScRayHit
-- Fields generated from native --api.
---@field id ScEntityId # Read-only. Hit entity, or 0 for terrain and map boundaries. Range 0..4503599627370495.
---@field x number # Read-only. World-pixel contact point X, subject to solver tolerance.
---@field y number # Read-only. World-pixel contact point Y.
---@field nx number # Read-only. Surface normal X; zero for an initially overlapping sweep. Range -1..1.
---@field ny number # Read-only. Surface normal Y; zero for an initially overlapping sweep. Range -1..1.
---@field fraction number # Read-only. Fraction along the supplied translation, not distance in pixels. Range 0..1.
---Closest Box2D hit; initial overlap ignored, ties by ID then point/normal. Includes sensors, excludes bodyless art, solid=false and zero category/mask. One-way platforms use full geometry. Syncs bodies without advancing time.
---Phases: load, init, update.
---@param x number # World-pixel X coordinate. Range -1000000..1000000.
---@param y number # World-pixel Y coordinate. Range -1000000..1000000.
---@param dx number # World-pixel translation, not an endpoint. Range -1000000..1000000.
---@param dy number # World-pixel translation, not an endpoint. Range -1000000..1000000.
---@return ScRayHit|nil
function sc.physics.ray(x, y, dx, dy) end
---Actual overlap against a center-rotated rectangle. Independent sorted unique IDs, terrain 0. Same filters as ray; syncs bodies without advancing time, except zero-size queries return empty immediately.
---Phases: load, init, update.
---Capacity: project.limits.entities + one terrain record.
---@param x number # World-pixel X coordinate. Range -1000000..1000000.
---@param y number # World-pixel Y coordinate. Range -1000000..1000000.
---@param w number # Unrotated rectangle width; zero returns an empty array. Range 0..1000000.
---@param h number # Unrotated rectangle height; zero returns an empty array. Range 0..1000000.
---@param angle? number # Radians about the rectangle center; transformed corners must remain within +/-1e6. Range -1000000..1000000. Default 0.
---@return ScEntityId[]
function sc.physics.query(x, y, w, h, angle) end
---Actual convex shape overlap. Independent sorted unique IDs, terrain 0. Same filters as ray; rejects concave/degenerate shapes and syncs bodies without advancing time.
---Phases: load, init, update.
---Capacity: project.limits.entities + one terrain record.
---@param points number[] # Plain dense 1..8 world x,y pairs, each coordinate -1e6..1e6; circle, distinct capsule endpoints, or cyclic convex polygon.
---@param radius number # World-pixel rounding radius; positive for circles/capsules, may be zero for polygons. Range 0..4096.
---@return ScEntityId[]
function sc.physics.overlap(points, radius) end
---Translation-only convex sweep, closest hit with ties by ID then point/normal. Initial overlap returns fraction=0 and zero normal. Same filters as ray; syncs bodies without advancing time.
---Phases: load, init, update.
---@param points number[] # Plain dense 1..8 world x,y pairs, each coordinate -1e6..1e6; circle, distinct capsule endpoints, or cyclic convex polygon.
---@param radius number # World-pixel rounding radius; positive for circles/capsules, may be zero for polygons. Range 0..4096.
---@param dx number # World-pixel translation, not an endpoint. Range -1000000..1000000.
---@param dy number # World-pixel translation, not an endpoint. Range -1000000..1000000.
---@return ScRayHit|nil
function sc.physics.sweep(points, radius, dx, dy) end
---@alias ScJointId integer # Room and generation checked; exact in Lua/JSON, at most 2^52-1. Not persistent.
---Create a distance, revolute or local-vertical prismatic joint after synchronizing bodies. Body destruction/rebuild and room replacement invalidate attached joints. Missing bodies or exhausted capacity raise an error.
---Phases: load, init, update.
---Capacity: 256 room joints; generation exhaustion retires a slot.
---@param type 'distance'|'revolute'|'prismatic' # Prismatic axis is A's local vertical axis.
---@param a ScEntityId # First live entity with a body. Range 1..4503599627370495.
---@param b ScEntityId # Distinct second live entity with a body. Range 1..4503599627370495.
---@param x number # World-pixel X coordinate. Range -1000000..1000000.
---@param y number # World-pixel Y coordinate. Range -1000000..1000000.
---@param length? number # Distance rest length (clamped to .16 minimum) or initial prismatic upper limit; unused for revolute. Range 0.01..4096. Default 8.
---@param bx? number # Distance-only second anchor X; supply bx/by together, otherwise use x/y. Range -1000000..1000000.
---@param by? number # Distance-only second anchor Y. Range -1000000..1000000.
---@return ScJointId
function sc.physics.joint(type, a, b, x, y, length, bx, by) end
---Synchronize bodies, then destroy a generation-checked joint; stale handles error.
---Phases: load, init, update.
---@param id ScJointId # Live room joint handle; rebuilding either body invalidates it. Range 1..4503599627370495.
function sc.physics.unjoint(id) end
---@class ScJointControl
-- Fields generated from native --api.
---@field limit boolean # Read-only. Initially true only for prismatic joints.
---@field motor boolean # Read-only. Initially false. Enabling with zero max_effort produces no drive.
---@field lower number # Read-only. Distance .16..4096 pixels (initial .16); prismatic -4096..4096 pixels (initial 0); revolute -pi..pi radians (initial -pi). Range -4096..4096.
---@field upper number # Read-only. Same kind-dependent range as lower. Initial distance 4096, prismatic creation length, revolute pi. Range -4096..4096.
---@field speed number # Read-only. Initial 0. Pixels/second for distance and prismatic; radians/second for revolute. Range -1000000..1000000.
---@field max_effort number # Read-only. Initial 0. Mass*pixel/second^2 for linear joints; mass*pixel^2/second^2 for revolute. Range 0..1000000000.
---Read or atomically patch joint controls; returns all six fields as an independent snapshot. Errors retain controls; unchanged patches do not wake bodies. Even reads synchronize bodies and require load/init/update.
---Phases: load, init, update.
---@param id ScJointId # Live room joint handle; rebuilding either body invalidates it. Range 1..4503599627370495.
---@param patch? ScJointControlPatch # Plain partial object; omitted fields retain current values. Nil reads without changing controls.
---@return ScJointControl
function sc.physics.joint_control(id, patch) end

---Read or replace a Tiled layer cell.
---Phases: load, init, update, draw, ui_update.
---Capacity: Finite Tiled cell access; failed terrain/navigation rebuild retains previous map.
---When gid is supplied (including nil), phases: load, init, update.
---@param layer string # Existing finite Tiled tile-layer name, 1..128 bytes without NUL.
---@param x integer # Zero-based cell column, inside map width. Range 0..16383.
---@param y integer # Zero-based cell row, inside map height. Range 0..16383.
---@param gid? integer # Supplying a fourth argument writes; nil is rejected. Zero clears, otherwise a known Tiled GID with supported flip flags. Range 0..4294967295.
---@return integer
function sc.map(layer, x, y, gid) end
---Copy Tiled objects with layer offsets applied; explicit collision objects already belong to static terrain.
---Phases: load, init, update, draw, ui_update.
---Capacity: At most 16384 finite Tiled objects; raw fields retained, layer offsets applied to x/y. Empty without an imported map.
---@return table<string,ScData>[]
function sc.objects() end

---@class ScTextOptions
-- Fields generated from native --api.
---@field font? string # Declared font name; empty uses default font. No NUL. At most 127 bytes. Default "".
---@field wrap? number # Logical maximum line width; zero disables wrapping. Range 0..4096. Default 0.
---@field align? integer # 0 left, 1 center, 2 right. Range 0..2. Default 0.
---Measure UTF-8 text using the same lazy font metrics as native rendering.
---Phases: load, init, update, draw, ui_update.
---@param text string # Valid UTF-8, at most 4096 bytes; no NUL.
---@param size number # Logical pixel size. Range 1..512.
---@param font? string|nil # Declared font resource; empty/omitted/nil uses default font. Default "".
---@param wrap? number|nil # Maximum line width; zero/omitted/nil disables wrapping. Range 0..4096. Default 0.
---@return number width # Logical width in pixels.
---@return number height # Logical height in pixels.
function sc.measure(text, size, font, wrap) end

---@class ScAudioOptions
-- Fields generated from native --api.
---@field volume? number # Target gain; a fade interpolates the current gain. Range 0..1. Default 1.
---@field pitch? number # Playback speed multiplier; also advances the logical clock. Range 0.25..4. Default 1.
---@field pan? number # Stereo position: -1 left, 0 center, 1 right. Range -1..1. Default 0.
---@field fade? number # Seconds remaining to reach target volume; play starts a nonzero fade at silence. Range 0..60. Default 0.
---@field loop? boolean # Repeat when duration is reached. Default false.
---@field paused? boolean # Pause playback, logical position and voice fade. Default false.
---@field persistent? boolean # Only music is retained across room changes; sound voices always expire. Default false.
---@field priority? integer # Only equal/lower-priority voices can be stolen; then lowest priority, oldest age, lowest handle wins. Range -128..127. Default 0.
---@field bus? 'master'|'music'|'sfx'|'ui' # Routing category. Music defaults to music, sounds to sfx; master routing does not multiply master gain twice. Default by resource: {"music": "music", "sound": "sfx"}.
sc.audio = {}
---Allocate WAV sound or Ogg Vorbis music. Steal equal/lower priority voices by priority, age and handle; return nil,error when no voice is eligible. Invalid arguments raise an error.
---Phases: load, init, update.
---Capacity: project.limits.sound_voices (0..32); 2 music streams.
---@param resource string # Declared sound or music resource name; embedded NUL is rejected.
---@param options? ScAudioOptions # Defaults apply only to newly allocated voices.
---@return integer|nil
---@return string|nil
function sc.audio.play(resource, options) end
---Atomically patch a live voice; invalid options or stale handles raise an error without changing the voice.
---Phases: load, init, update.
---@param handle integer # Live generation-checked voice handle; 1..4294967295.
---@param options? ScAudioOptions # Atomic patch; nil leaves the voice unchanged.
function sc.audio.set(handle, options) end
---Stop immediately or fade to zero, then expire the voice; stale handles raise an error.
---Phases: load, init, update.
---@param handle integer # Live generation-checked voice handle.
---@param fade? number # Finite seconds; zero stops immediately. Range 0..60. Default 0.
function sc.audio.stop(handle, fade) end

-- Device queries use a fixed-tick snapshot, not text/IME input.
---@alias ScKey 'apostrophe'|'comma'|'minus'|'period'|'slash'|'0'|'1'|'2'|'3'|'4'|'5'|'6'|'7'|'8'|'9'|'semicolon'|'equal'|'a'|'b'|'c'|'d'|'e'|'f'|'g'|'h'|'i'|'j'|'k'|'l'|'m'|'n'|'o'|'p'|'q'|'r'|'s'|'t'|'u'|'v'|'w'|'x'|'y'|'z'|'left_bracket'|'backslash'|'right_bracket'|'grave'|'space'|'escape'|'enter'|'tab'|'backspace'|'insert'|'delete'|'right'|'left'|'down'|'up'|'page_up'|'page_down'|'home'|'end'|'caps_lock'|'scroll_lock'|'num_lock'|'print_screen'|'pause'|'f1'|'f2'|'f3'|'f4'|'f5'|'f6'|'f7'|'f8'|'f9'|'f10'|'f11'|'f12'|'left_shift'|'left_control'|'left_alt'|'left_super'|'right_shift'|'right_control'|'right_alt'|'right_super'|'kb_menu'|'kp_0'|'kp_1'|'kp_2'|'kp_3'|'kp_4'|'kp_5'|'kp_6'|'kp_7'|'kp_8'|'kp_9'|'kp_decimal'|'kp_divide'|'kp_multiply'|'kp_subtract'|'kp_add'|'kp_enter'|'kp_equal'
---@alias ScGamepadButton 'dpad_up'|'dpad_right'|'dpad_down'|'dpad_left'|'north'|'east'|'south'|'west'|'left_shoulder'|'left_trigger'|'right_shoulder'|'right_trigger'|'back'|'guide'|'start'|'left_thumb'|'right_thumb'
---@alias ScGamepadAxis 'left_x'|'left_y'|'right_x'|'right_y'|'left_trigger'|'right_trigger'
---@alias ScMouseButton 'left'|'right'|'middle'|'side'|'extra'

---Read the fixed-tick device input snapshot.
---Phases: load, init, update, draw, ui_update.
---@param name ScKey # Exact key name from --api keys.
---@return boolean
function sc.key_down(name) end

---Read the fixed-tick device input snapshot.
---Phases: load, init, update, draw, ui_update.
---@param name ScKey # Exact key name from --api keys.
---@return boolean
function sc.key_pressed(name) end

---Read the fixed-tick device input snapshot.
---Phases: load, init, update, draw, ui_update.
---@param name ScKey # Exact key name from --api keys.
---@return boolean
function sc.key_released(name) end

---Read the fixed-tick device input snapshot.
---Phases: load, init, update, draw, ui_update.
---@param name ScGamepadButton # Exact button name from --api gamepad_buttons; selected controller.
---@return boolean
function sc.gamepad_down(name) end

---Read the fixed-tick device input snapshot.
---Phases: load, init, update, draw, ui_update.
---@param name ScGamepadButton # Exact button name from --api gamepad_buttons; selected controller.
---@return boolean
function sc.gamepad_pressed(name) end

---Read the fixed-tick device input snapshot.
---Phases: load, init, update, draw, ui_update.
---@param name ScGamepadButton # Exact button name from --api gamepad_buttons; selected controller.
---@return boolean
function sc.gamepad_released(name) end

---Read the fixed-tick device input snapshot.
---Phases: load, init, update, draw, ui_update.
---@return boolean
function sc.gamepad_connected() end

---Read the fixed-tick device input snapshot.
---Phases: load, init, update, draw, ui_update.
---Capacity: Selected controller; use sc.input.gamepad_axis for explicit slots.
---@param axis ScGamepadAxis # Selected controller axis; disconnected returns zero.
---@param deadzone? number|nil # Finite, representable as float below 1; rescales the remaining magnitude. Omitted/nil uses 0.2. Range 0..1. Maximum is exclusive. Default 0.2.
---@return number
function sc.gamepad_axis(axis, deadzone) end

sc.images = {}
---@type boolean # True when this engine includes the streaming module.
sc.images.available = false

-- BEGIN GENERATED COMPLETE API
-- Regenerate with tools/api_docs.py using full and lightweight builds.
sc.app = {}
sc.debug = {}
sc.identity = {}
sc.input = {}
sc.lighting = {}
sc.material = {}
sc.navigation = {}
sc.particles = {}
sc.presentation = {}
sc.projectiles = {}
sc.stream = {}
---Pause native entity motion, physics, projectiles and particles. Lua update/ui_update, tick, camera and application services continue; scripts must gate their own gameplay.
---Phases: load, init, update, ui_update.
---@param paused boolean # Exact boolean; room-local, initially false.
function sc.app.pause(paused) end
---Read the current room simulation pause flag, initially false.
---Phases: load, init, update, draw, ui_update.
---@return boolean
function sc.app.paused() end
---Request orderly application shutdown when this room is active; candidate requests remain room-local until commit.
---Phases: load, init, update, ui_update.
function sc.app.quit() end
---Read or atomically patch a persistent application bus; patching is forbidden in draw/ui_update.
---Phases: load, init, update, draw, ui_update.
---When options is non-nil, phases: load, init, update.
---@param name 'master'|'music'|'sfx'|'ui'
---@param options? ScAudioBusOptions # Omit or pass nil to read in any phase. A table, including an empty one, is a mutation.
---@return ScAudioBusState
function sc.audio.bus(name, options) end
---Acquire persistent music by declared resource path, patching an existing voice without restarting it; candidate changes remain isolated until commit.
---Phases: load, init, update.
---Capacity: 2 music streams; existing matching music reuses its slot.
---@param resource string # Declared music name. Reuses the first live persistent, non-stopping voice with the same resource path.
---@param options? ScMusicOptions # New voices use defaults; reused voices retain omitted fields and playback position.
---@return integer|nil
---@return string|nil
function sc.audio.music(resource, options) end
---Attach and snap to a live entity center, or detach with nil; subsequent fixed updates use smoothing.
---Phases: load, init, update.
---@param id ScEntityId|nil
function sc.camera.follow(id) end
---Copy fixed camera settings and effective view including pixel snapping and shake; draw uses display interpolation. anchor_x/y expose the interpolated unshaken origin for parallax. target=0 means detached.
---Phases: load, init, update, draw, ui_update.
---@return ScCameraState
function sc.camera.read() end
---Atomically patch camera settings; x/y detach follow. Bounds clamp immediately.
---Phases: load, init, update.
---@param patch ScCameraPatch
function sc.camera.set(patch) end
---Replace shake: amplitude 0..256 world pixels, duration 0..60 seconds, uint32 seed defaults to 1. Zero cancels; no shared RNG consumed.
---Phases: load, init, update.
---@param amplitude number
---@param duration number
---@param seed? integer
function sc.camera.shake(amplitude, duration, seed) end
---Convert world coordinates to logical viewport pixels using the current view (interpolated during draw, fixed elsewhere). Coordinates accept finite -1e7..1e7.
---Phases: load, init, update, draw, ui_update.
---@param x number
---@param y number
---@return ScCameraPoint
function sc.camera.to_screen(x, y) end
---Convert logical viewport pixels (e.g. sc.input.mouse coordinates) to world coordinates. Check the separate inside result; black bars are outside the viewport.
---Phases: load, init, update, draw, ui_update.
---@param x number
---@param y number
---@return ScCameraPoint
function sc.camera.to_world(x, y) end
---Push or pop a screen-space clipping rectangle; nesting depth 32.
---Phases: draw.
---Capacity: Exactly zero or four arguments; 32 nested clips. Balanced stack validated after draw; each push/pop consumes a draw command.
---@param x? number # All four coordinates required together; no arguments pops. Range -1000000..1000000.
---@param y? number # Screen Y. Range -1000000..1000000.
---@param w? number # Clip width. Range 0..1000000.
---@param h? number # Clip height. Range 0..1000000.
function sc.clip(x, y, w, h) end
---Register a weak shiny.ui tree for inspection, or remove it with nil; devtools only.
---Phases: load, init, update, ui_update.
---Capacity: 64 weakly held trees; names 1..128 bytes.
---@param name string
---@param tree? table|nil
function sc.debug.ui(name, tree) end
---Expose a copied, bounded observation. Invalid input preserves existing watches; Lua metatables are rejected. Available without interactive devtools.
---Phases: load, init, update.
---Capacity: 64 names; each value at most 256 KiB and depth 16.
---@param name string # 1..128 UTF-8 bytes without NUL; exact string type.
---@param value boolean|number|string|table|nil # Copied plain UTF-8 data; finite numbers, dense arrays or string-keyed objects. Nil is a retained null observation, not deletion.
function sc.debug.watch(name, value) end
---Find every entity with a matching tag in stable slot order.
---Phases: load, init, update, draw, ui_update.
---@param tag string
---@return ScEntityId[]
function sc.find_all(tag) end
---Read entity copies in requested order.
---Phases: load, init, update, draw, ui_update.
---Capacity: 65536 input IDs.
---@param ids ScEntityId[]
---@return ScEntity[]
function sc.get_many(ids) end
---Register an unloaded persistent object ID; existing active/deleted names retain their status.
---Phases: load, init, update.
---Capacity: project.limits.identities.
---@param persistent_id string
---@return boolean
function sc.identity.declare(persistent_id) end
---Copy room path and persistent object ID; rejects unnamed or stale entities.
---Phases: load, init, update, draw, ui_update.
---@param id ScEntityId
---@return ScIdentityReference
function sc.identity.reference(id) end
---Mark a persistent object deleted, releasing its entity if active; explicit spawn with the same name restores it.
---Phases: load, init, update.
---Capacity: project.limits.identities.
---@param persistent_id string
---@return boolean
function sc.identity.remove(persistent_id) end
---Resolve a persistent object in this room; other rooms report room_inactive without guessing object existence.
---Phases: load, init, update, draw, ui_update.
---@param reference string|ScIdentityReference
---@return ScIdentityResolution
function sc.identity.resolve(reference) end
---Release a persistent object and invalidate its handle while retaining unloaded status.
---Phases: load, init, update.
---@param id ScEntityId
---@return boolean
function sc.identity.unload(id) end
---Draw an image region or nine-slice panel with stable order, clipping, tint and optional material.
---Phases: draw.
---Capacity: One command; at most nine quads with slice.
---@param resource string # Declared image resource name; streamed images must be committed.
---@param x number # Destination coordinate. Range -1000000..1000000.
---@param y number # Destination coordinate. Range -1000000..1000000.
---@param w number # Destination width. Range 0..4096.
---@param h number # Destination height. Range 0..4096.
---@param options? ScImageOptions|boolean # Image options, or screen boolean. Default false.
function sc.image(resource, x, y, w, h, options) end
---Discard candidate images, preserving the current set; cancels the host wait.
---Phases: update, ui_update.
---@param request integer # Room-local image transaction ID; invalid after commit, cancel or room exit. Range 1..4503599627370495.
---@return boolean
function sc.images.cancel(request) end
---Publish a ready set and release the previous room references.
---Phases: update.
---@param request integer # Room-local image transaction ID; invalid after commit, cancel or room exit. Range 1..4503599627370495.
---@return boolean
function sc.images.commit(request) end
---Stage a replacement image set. The next fixed boundary waits for CPU decode and native upload, preserving current images.
---Phases: update.
---Capacity: 128 names; one room request; shared cache 128 images / 128 MiB.
---@param names string[] # Distinct declared image names or declared paths (names take precedence). Includes bound normal maps. Eager images are validated but not cached; aliases normalize by resource name. Replaces the complete streamed-image set; empty unloads it.
---@return integer
function sc.images.prepare(names) end
---Reread the complete committed streamed-image set into private revisions of the same dimensions. Commit publishes; cancel/failure preserves old pixels and future cache lookups.
---Phases: update.
---Capacity: Current and replacement revisions share the 128-image / 128 MiB cache budget.
---@return integer
function sc.images.reload() end
---Retry a failed decode/upload without changing request ID or current images.
---Phases: update, ui_update.
---@param request integer # Room-local image transaction ID; invalid after commit, cancel or room exit. Range 1..4503599627370495.
---@return boolean
function sc.images.retry(request) end
---Read application CPU cache reservations and references; excludes GPU/workspace overhead.
---Phases: load, init, update, draw, ui_update.
---@return ScImageCacheStats
function sc.images.stats() end
---Read host-observed pending/ready/failed state; no worker timing is published to gameplay.
---Phases: load, init, update, draw, ui_update.
---@param request integer # Room-local image transaction ID; invalid after commit, cancel or room exit. Range 1..4503599627370495.
---@return ScImageRequestStatus
function sc.images.status(request) end
---UTF-8 grapheme starts plus end position; Lua one-based byte offsets.
---Phases: load, init, update, draw, ui_update.
---Capacity: 65536 UTF-8 bytes.
---@param text string # Valid UTF-8, including an empty string; byte positions include the end sentinel.
---@return integer[]
function sc.input.boundaries(text) end
---Queue a clipboard write or read the recorded paste input for this tick.
---Phases: load, init, update, draw, ui_update.
---Capacity: 4095 UTF-8 bytes; last successful queued write wins.
---When text is non-nil, phases: load, init, update, ui_update.
---@param text? string # Omitted/nil reads the snapshot; a UTF-8 string without NUL queues a write and returns no values.
---@return string|nil
function sc.input.clipboard(text) end
---Set logical-screen IME candidate position or end text focus.
---Phases: load, init, update, ui_update.
---@param x number|false # Logical viewport X, or false as the only argument to end focus. Range -1000000..1000000.
---@param y? number # Required with numeric x; forbidden with false. Range -1000000..1000000.
function sc.input.focus_text(x, y) end
---Read normalized axis with deadzone in [0,1); default .2.
---Phases: load, init, update, draw, ui_update.
---Capacity: 4 stable slots.
---@param name ScGamepadAxis
---@param deadzone? number # Rescale magnitude outside the deadzone to 0..1; disconnected pads return zero. Range 0..1. Maximum is exclusive. Default 0.2.
---@param slot? integer # Omitted or nil reads the selected controller, not necessarily slot 1. Range 1..4. Default nil.
---@return number
function sc.input.gamepad_axis(name, deadzone, slot) end
---Read connection of selected controller or explicit slot 1..4.
---Phases: load, init, update, draw, ui_update.
---Capacity: 4 stable slots.
---@param slot? integer # Omitted or nil reads the selected controller, not necessarily slot 1. Range 1..4. Default nil.
---@return boolean
function sc.input.gamepad_connected(slot) end
---Held button; optional stable slot 1..4, default selected controller.
---Phases: load, init, update, draw, ui_update.
---Capacity: 4 stable slots.
---@param name ScGamepadButton
---@param slot? integer # Omitted or nil reads the selected controller, not necessarily slot 1. Range 1..4. Default nil.
---@return boolean
function sc.input.gamepad_down(name, slot) end
---Press edge, including taps between ticks.
---Phases: load, init, update, draw, ui_update.
---Capacity: 4 stable slots.
---@param name ScGamepadButton
---@param slot? integer # Omitted or nil reads the selected controller, not necessarily slot 1. Range 1..4. Default nil.
---@return boolean
function sc.input.gamepad_pressed(name, slot) end
---Release edge, including device disconnection.
---Phases: load, init, update, draw, ui_update.
---Capacity: 4 stable slots.
---@param name ScGamepadButton
---@param slot? integer # Omitted or nil reads the selected controller, not necessarily slot 1. Range 1..4. Default nil.
---@return boolean
function sc.input.gamepad_released(name, slot) end
---Fixed snapshot keyboard held state.
---Phases: load, init, update, draw, ui_update.
---@param name ScKey
---@return boolean
function sc.input.key_down(name) end
---Fixed snapshot keyboard press edge.
---Phases: load, init, update, draw, ui_update.
---@param name ScKey
---@return boolean
function sc.input.key_pressed(name) end
---Fixed snapshot keyboard release edge.
---Phases: load, init, update, draw, ui_update.
---@param name ScKey
---@return boolean
function sc.input.key_released(name) end
---Pointer in logical viewport coordinates.
---Phases: load, init, update, draw, ui_update.
---@return number x # Logical viewport X; may be outside its bounds.
---@return number y # Logical viewport Y; may be outside its bounds.
---@return boolean inside # False for black bars and positions outside the viewport.
function sc.input.mouse() end
---Mouse held state.
---Phases: load, init, update, draw, ui_update.
---@param name ScMouseButton
---@return boolean
function sc.input.mouse_down(name) end
---Mouse press edge.
---Phases: load, init, update, draw, ui_update.
---@param name ScMouseButton
---@return boolean
function sc.input.mouse_pressed(name) end
---Mouse release edge.
---Phases: load, init, update, draw, ui_update.
---@param name ScMouseButton
---@return boolean
function sc.input.mouse_released(name) end
---Fixed-tick input data including four pad slots, pointer and text.
---Phases: load, init, update, draw, ui_update.
---Capacity: 4 pad slots; 4095 UTF-8 bytes per text field; 128 composition segments.
---@return ScInputSnapshot
function sc.input.snapshot() end
---Snapshot text, IME positions and bounded clause/conversion segments.
---Phases: load, init, update, draw, ui_update.
---Capacity: 4095 UTF-8 bytes per string; 128 composition segments.
---@return string committed # UTF-8 text committed in this snapshot.
---@return string composition # Uncommitted UTF-8 preedit text.
---@return integer cursor # One-based UTF-8 insertion position within composition; unavailable metadata defaults to end.
---@return integer selection_start # Start of the IME target segment, one-based UTF-8 position.
---@return integer selection_end # Exclusive end of first contiguous target range; an empty range means no target selection.
---@return ScInputCompositionSegment[] segments # Ordered preedit clause/attribute runs; at most 128. Empty when metadata is unavailable or exceeds capacity.
---@return boolean segments_truncated # True when native segmentation exceeds capacity; text and cursor remain intact, segments is empty.
function sc.input.text() end
---Accumulated fixed-tick wheel deltas.
---Phases: load, init, update, draw, ui_update.
---@return number x # Accumulated horizontal wheel delta.
---@return number y # Accumulated vertical wheel delta.
function sc.input.wheel() end
---Atomically patch area-light softness and sample count; glows cast geometry shadows excluding their own body. Settings are presentation-only.
---Phases: load, init, update.
---Capacity: softness 0..32 pixels (default 0); samples 1..8 (default 1).
---@param options {softness?:number,samples?:integer}
function sc.lighting.configure(options) end
---Bind a same-size normal image/atlas during initialization; shared by world images, sprites and tiles. Rebinding a source path replaces its previous mapping atomically.
---Phases: load, init.
---Capacity: 64 image/atlas bindings; matching dimensions; 64 MiB normal-target budget.
---@param image string
---@param normal_map string
function sc.lighting.normal(image, normal_map) end
---Choose body (default physical filters), bounds (rotated visual rectangle), shape (stored geometry regardless of physical flags), or none. Does not create/change physics. Generation checks prevent slot reuse from inheriting overrides.
---Phases: load, init, update.
---Capacity: project.limits.entities; storage allocated while loading.
---@param id ScEntityId
---@param mode 'body'|'bounds'|'shape'|'none'
function sc.lighting.occluder(id, mode) end
---Read the current live entity's presentation-only occlusion mode; defaults to body.
---Phases: load, init, update, draw, ui_update.
---@param id ScEntityId
---@return 'body'|'bounds'|'shape'|'none'
function sc.lighting.occluder_mode(id) end
---Submit an explicit world point light during draw. Commands clear before every draw; no entity or persistent handle is created. Per-light color, intensity, shadows and quality override inherited settings.
---Phases: draw.
---Capacity: 32 commands per draw; combined visible budget 32 lights / 16 shadow lights.
---@param spec ScPointLight
function sc.lighting.point(spec) end
---Read settings, budgets, current/last draw point_commands and last native submission usage. Headless status stays pending and native usage counts stay zero.
---Phases: load, init, update, draw, ui_update.
---@return {softness:number,samples:integer,effective_samples:integer,occluders:integer,required_occluders:integer,occluder_capacity:integer,lights:integer,light_capacity:integer,shadow_lights:integer,shadow_capacity:integer,point_commands:integer,normal_maps:integer,normal_capacity:integer,normal_target_bytes:integer,normal_budget_bytes:integer,occluder_overrides:integer,override_capacity:integer,status:string,error:string}
function sc.lighting.stats() end
---Bind a live surface material to a live entity. 0 clears the override and inherits its image default; false forces the builtin shader. Create a material first. Old entity generations never inherit overrides.
---Phases: load, init, update.
---Capacity: One generation-checked override per configured entity slot.
---@param entity ScEntityId
---@param material integer|false
function sc.material.bind_entity(entity, material) end
---Bind a default surface material to a declared image path, shared by images, sprites, tiles, atlas projectiles and textured particles. 0 clears. Aliases of one path share the binding. Create a material first.
---Phases: load, init, update.
---Capacity: 64 resolved image paths.
---@param image string
---@param material integer
function sc.material.bind_image(image, material) end
---Read material, uniform and texture limits without allocating the pool.
---Phases: load, init, update, draw, ui_update.
---@return table
function sc.material.capacity() end
---Create a room material from a declared fragment shader; uniforms map names to {type,value}. postprocess defaults false; true reserves sc_scene/sc_resolution and permits 3 auxiliary textures.
---Phases: load, init, update.
---Capacity: 64 materials; 32 uniforms; auxiliary textures: image 4, postprocess 3.
---@param spec table
---@return integer
function sc.material.create(spec) end
---Release a room material; generation-checked handles become invalid. Clear image, live entity and postprocess references first; dead entity bindings do not retain materials.
---Phases: load, init, update.
---@param id integer
function sc.material.destroy(id) end
---Read the effective entity material: override, image default, then builtin (0). The entity must be alive.
---Phases: load, init, update, draw, ui_update.
---@param entity ScEntityId
---@return integer
function sc.material.entity_material(entity) end
---Read a declared image's default material, or 0 for builtin rendering.
---Phases: load, init, update, draw, ui_update.
---@param image string
---@return integer
function sc.material.image_material(image) end
---Read source revision, GPU compilation state/error and current uniform values; headless materials stay pending.
---Phases: load, init, update, draw, ui_update.
---@param id integer
---@return table
function sc.material.info(id) end
---Read requested passes, target color-byte budget/allocation and presentation status. Headless chains stay pending.
---Phases: load, init, update, draw, ui_update.
---@return table
function sc.material.pipeline() end
---Atomically replace the bounded postprocess chain; empty clears it. Live postprocess=true handles required; referenced materials cannot be destroyed.
---Phases: load, init, update.
---Capacity: 0..4 passes; default 64 MiB color-target budget; at most 1 GiB.
---@param passes integer[]
---@param budget_bytes? integer
function sc.material.postprocess(passes, budget_bytes) end
---Read replacement shader source; GPU compilation occurs before presentation and keeps the old program on failure.
---Phases: load, init, update.
---@param id integer
function sc.material.reload(id) end
---Atomically patch values of existing typed uniforms; types and names remain fixed.
---Phases: load, init, update.
---@param id integer
---@param values table
function sc.material.set(id, values) end
---Read world-pixel steering without advancing the search. Stale or incomplete fields, blocked/unreachable cells and outside positions return zero direction; stale handles error.
---Phases: load, init, update, draw, ui_update.
---@param handle integer # Live flow handle returned by flow, not a slot number.
---@param x number # World-pixel position, including a selected region's origin. Range -1000000..1000000.
---@param y number # World-pixel position. Range -1000000..1000000.
---@return number dx # Normalized X direction, or zero if no published direction exists.
---@return number dy # Normalized Y direction.
---@return 'ok'|'unreachable'|'budget_exhausted'|'stale' status # Global field status; ok may still yield zero at the goal, outside the grid, or in a disconnected cell.
function sc.navigation.direction(handle, x, y) end
---Build a shared target field with optional circular clearance. Each successful slot replacement creates a new handle; invalid arguments or failed preparation retain the old field.
---Phases: load, init, update.
---Capacity: 16 flow slots; 134217727 generations per slot per room, no wrap.
---@param gx integer # Zero-based goal column within the selected grid.
---@param gy integer # Zero-based goal row within the selected grid.
---@param budget? integer # Maximum initial BFS expansions. Range 1..1048576. Default 16384.
---@param slot? integer # Replace this room-owned slot; successful replacement invalidates its old handle. Range 1..16. Default 1.
---@param radius? number # Circular body clearance in world pixels, retained by refresh; share fields between same-size agents. Range 0..4096. Default 0.
---@return integer handle # Generation-checked room handle, lossless within 52 bits; never persist it.
---@return 'ok'|'unreachable'|'budget_exhausted' status # A blocked goal produces unreachable with zero visits.
---@return integer visited # Cumulative nodes expanded in this build.
function sc.navigation.flow(gx, gy, budget, slot, radius) end
---Deterministic four-neighbor A-star with optional circular clearance from blocked cells and grid edges. Blocked endpoints return unreachable; out-of-bounds coordinates error. Returned paths are snapshots.
---Phases: load, init, update, draw, ui_update.
---Capacity: 16384 cells.
---@param sx integer # Zero-based start column within the selected grid.
---@param sy integer # Zero-based start row within the selected grid.
---@param gx integer # Zero-based goal column within the selected grid.
---@param gy integer # Zero-based goal row within the selected grid.
---@param budget? integer # Maximum expanded nodes in this query. Range 1..1048576. Default 16384.
---@param radius? number # Circular body clearance in world pixels at path cell centers. Zero uses point navigation. Range 0..4096. Default 0.
---@return ScNavigationPath
function sc.navigation.path(sx, sy, gx, gy, budget, radius) end
---Restart after passability changes or resume bounded BFS. Only a complete field publishes directions. Node budget is not a CPU time limit.
---Phases: load, init, update.
---@param handle integer # Live flow handle; refresh preserves its generation.
---@param budget integer # Maximum additional expansions this call. Range 1..1048576.
---@return 'ok'|'unreachable'|'budget_exhausted' status # A stale field restarts; a partial build resumes; an unchanged finished build does no work.
---@return integer visited # Cumulative since the last restart, at most the number of grid cells.
function sc.navigation.refresh(handle, budget) end
---Atomically select a local navigation grid; terrain overlaps block cells. No arguments restores the room grid. Every successful replacement invalidates existing flow handles.
---Phases: load, init, update.
---Capacity: 16384 cells.
---@param x? number # World-pixel origin; x,y,rows are required together. No arguments restores the room grid. Range -1000000..1000000.
---@param y? number # World-pixel origin. Range -1000000..1000000.
---@param rows? string[] # Nonempty equal-width . / # rows, at most 16384 cells; endpoints must stay within +/-1000000 pixels.
---@param cell_size? integer # World pixels per cell. Range 1..256. Default 8.
function sc.navigation.region(x, y, rows, cell_size) end
---Validate the entire distinct-entity batch, then steer each entity center. Unpublished fields set velocities to zero. Apply each update; invalidation alone does not stop entities.
---Phases: load, init, update.
---Capacity: project.limits.entities.
---@param handle integer # Live flow handle.
---@param entities ScEntityId[] # Dense plain array of distinct live entities. Empty is valid; errors change no velocity.
---@param speed number # World pixels per second; both velocity components are replaced. Range 0..1000000.
---@return integer
function sc.navigation.steer(handle, entities, speed) end
---Borrow an application session by name, including during candidate initialization.
---Phases: load, init, update, draw, ui_update.
---Capacity: Borrowed bindings do not consume additional session slots.
---@param name string # Exact string, 1..63 bytes without NUL. Case-sensitive application-local name.
---@return ScNetSession|nil
---@return string|nil
function sc.net.bind(name) end
---Atomically emit 0..65536 particles; no gameplay RNG consumption. Stale room handles fail.
---Phases: load, init, update.
---Capacity: project.limits.particles.
---@param emitter integer
---@param x number
---@param y number
---@param count integer
---@return integer
function sc.particles.burst(emitter, x, y, count) end
---Register speed/lifetime ranges, angle sector, gravity factor and linear size/RGBA lifetime curve; room-qualified handle.
---Phases: load, init.
---Capacity: 64 immutable templates per room; 2..8 curve keys.
---@param spec ScParticleEmitterSpec
---@return integer
function sc.particles.define(spec) end
---Read allocated capacity, active count, available slots and emitter templates.
---Phases: load, init, update, draw, ui_update.
---@return {capacity:integer,used:integer,available:integer,emitters:integer}
function sc.particles.stats() end
---Set visual parent and local top-left/angle offset. Atomically reject stale handles, bodies, velocity, cycles, depth or invalid pose. Synchronize after physics; preserve child dimensions/flip/layer. Destroying a parent detaches direct children at their current world poses.
---Phases: load, init, update.
---Capacity: At most 32 parent edges; bodyless child with zero velocity; no allocation..
---@param child ScEntityId
---@param parent ScEntityId
---@param offset ScAttachmentOffset
function sc.presentation.attach(child, parent, offset) end
---Copy runtime parent handle and local offset; nil for roots. Not a save format. sc.get continues to return fixed world coordinates.
---Phases: load, init, update, draw, ui_update.
---@param id ScEntityId
---@return {parent:ScEntityId,x:number,y:number,angle:number}|nil
function sc.presentation.attachment(id) end
---Remove a visual parent while preserving fixed world pose; an already detached live entity is unchanged.
---Phases: load, init, update.
---@param id ScEntityId
function sc.presentation.detach(id) end
---Default false. Reserve fixed history during load/init; update may toggle already-reserved interpolation. Storage remains reserved when disabled.
---Phases: load, init, update.
---Capacity: Startup entity/particle/projectile capacities; enabling for the first time requires load/init..
---@param enabled boolean
function sc.presentation.interpolate(enabled) end
---Read display position/shortest-arc angle in draw, fixed pose elsewhere. Dimensions use current geometry. sc.get always remains fixed state.
---Phases: load, init, update, draw, ui_update.
---@param id ScEntityId
---@return ScDisplayPose
function sc.presentation.pose(id) end
---Skip interpolation for this live entity during the current tick, e.g. after a teleport. Does not change simulation position or follow camera.
---Phases: load, init, update.
---@param id ScEntityId
function sc.presentation.snap(id) end
---Skip camera interpolation for this tick, e.g. after a camera cut. Does not change camera settings.
---Phases: load, init, update.
function sc.presentation.snap_camera() end
---Read history capacities and payload bytes, excluding allocator overhead. No trace or state hash is required.
---Phases: load, init, update, draw, ui_update.
---@return {enabled:boolean,reserved:boolean,ready:boolean,entities:integer,particles:integer,projectiles:integer,bytes:integer}
function sc.presentation.stats() end
---Remove bullets and hits without recycling IDs or sprite registrations; no-op before configuration.
---Phases: load, init, update.
function sc.projectiles.clear() end
---Allocate once during initialization, up to project.limits.projectiles (default 32768, 0 disables). Omitted capacity uses that limit; no storage before configuration.
---Phases: load, init.
---Capacity: project.limits.projectiles: default 32768; range 0..65536.
---@param capacity? integer|nil # Omitted/nil uses project.limits.projectiles; must not exceed that limit. Range 1..65536.
function sc.projectiles.configure(capacity) end
---Current live projectile count, or zero when unconfigured.
---Phases: load, init, update, draw, ui_update.
---@return integer
function sc.projectiles.count() end
---Copy last simulated-step hits in projectile/fraction/target order. Target zero is terrain; clear removes hits too. Empty when unconfigured.
---Phases: load, init, update, draw, ui_update.
---@return ScProjectileHit[]
function sc.projectiles.hits() end
---Validate a dense plain-table batch within allocated capacity, using preallocated staging. Empty batch returns an empty array. Numeric RGBA/mask, strict field types; Lua result allocation completes before native commit.
---Phases: load, init, update.
---Capacity: Configured projectile capacity; monotonically increasing room-local IDs below 2^52, not entity handles..
---@param specs ScProjectileSpec[] # Dense plain array; whole batch is preflighted before commit.
---@return integer[]
function sc.projectiles.spawn(specs) end
---Register a declared PNG atlas region during init, after configure. Up to 64; display size defaults to source size, max 4096. Spawn sprite=0 uses the radius-sized color quad. Draws retain projectile ID order.
---Phases: load, init.
---Capacity: 64 registered sprites after configuration; return ID 1..64..
---@param resource string # Declared PNG resource name, 1..127 bytes without NUL.
---@param x integer # Source left pixel. Range 0..8191.
---@param y integer # Source top pixel. Range 0..8191.
---@param w integer # Source width; x+w must fit the PNG and 8192. Range 1..8192.
---@param h integer # Source height; y+h must fit the PNG and 8192. Range 1..8192.
---@param width? number|nil # Finite and strictly positive, at most 4096; omitted/nil defaults to w. Range 0..4096.
---@param height? number|nil # Finite and strictly positive, at most 4096; omitted/nil defaults to h. Range 0..4096.
---@return integer
function sc.projectiles.sprite(resource, x, y, w, h, width, height) end
---Read configured project budget, allocated pool capacity, live count, free slots and registered sprite count; safe in draw.
---Phases: load, init, update, draw, ui_update.
---@return ScProjectileStats
function sc.projectiles.stats() end
---Delete a slot, backup and owned chunks; update only, disabled in check. Missing slots are harmless; invalid arguments/context and I/O failures raise Lua errors.
---Phases: update.
---@param slot string # 1..128 ASCII letters, digits, underscores or hyphens; project.id supplies the namespace.
---@return boolean
function sc.save.delete(slot) end
---Sorted distinct slot metadata, including backup-only slots and invalid record diagnostics. Synchronous; requires project.id. Invalid arguments, directory errors and capacity overflow raise Lua errors.
---Phases: load, init, update, draw, ui_update.
---Capacity: 128 distinct disk slots; 16 in-memory slots.
---@return ScSaveSlot[]
function sc.save.list() end
---Read current-format checkpoint data and chunk_count without exposing the native chunk index; recover the complete previous valid backup when needed. Synchronous, pins the chosen snapshot for chunk reads; requires project.id.
---Phases: load, init, update, draw, ui_update.
---Capacity: 256 KiB state; native chunk index is not returned.
---@param slot string # 1..128 ASCII letters, digits, underscores or hyphens; project.id supplies the namespace.
---@return ScSaveRecord|nil record # Independent checkpoint summary, or nil on missing/invalid save.
---@return string|nil error # Diagnostic on failure; absent on success.
function sc.save.read(slot) end
---Read one disk chunk from a pinned complete snapshot; absent chunk or missing slot and backup returns nil without error. Invalid existing saves retain diagnostics. Load/init/update only, disabled in check. Keys use persistent-ID syntax.
---Phases: load, init, update.
---Capacity: 256 KiB per chunk.
---@param slot string # 1..128 ASCII letters, digits, underscores or hyphens; project.id supplies the namespace.
---@param key string # 1..127 bytes using persistent-ID syntax; no empty, leading/trailing slash or . / .. path segment.
---@return table<string,ScData>|nil state # Independent chunk state; nil for a missing slot/chunk or failure.
---@return string|nil error # Diagnostic on failure; absent for a missing slot/chunk.
function sc.save.read_chunk(slot, key) end
---Read selected chunks and a checkpoint summary on the application IO worker, using the pinned snapshot when available or selecting a complete valid snapshot. Missing slot/keys are absent data, corruption is a failure. No state or scene mutation. Host gates the next fixed update.
---Phases: update.
---Capacity: One unreleased transaction; 1024 keys; 1 MiB combined encoded chunks plus 4 MiB internal index.
---@param slot string # 1..128 ASCII letters, digits, underscores or hyphens; project.id supplies the namespace.
---@param keys string[] # 0..1024 unique chunk keys using persistent-ID syntax; an empty array reads only the checkpoint summary.
---@return integer|nil request # Accepted request ID; acceptance is not disk completion.
---@return string|nil error # Preflight/submission error; no request was accepted.
function sc.save.read_chunks_async(slot, keys) end
---Release an observed result and invalidate its ID; never cancel pending IO or undo a commit. Successful reads pin their selected index; writes, failed or missing reads clear it. A released failure permits continuing the old world.
---Phases: update, ui_update.
---@param request integer # Live request returned by an asynchronous read or write. Range 1..4503599627370495.
---@return boolean
function sc.save.release(request) end
---Copy a successfully completed read result with record summary and requested chunks. Errors for pending, failed, write or expired requests; does not release or modify state.
---Phases: load, init, update, draw, ui_update.
---@param request integer # Live request returned by an asynchronous read or write. Range 1..4503599627370495.
---@return ScSaveReadResult
function sc.save.result(request) end
---Retry an observed failed read/write using its original payload, selected snapshot and request ID. Update/ui_update only; candidate/check/draw forbidden.
---Phases: update, ui_update.
---@param request integer # Live request returned by an asynchronous read or write. Range 1..4503599627370495.
---@return boolean
function sc.save.retry(request) end
---Copy the host-observed request status; never poll worker timing from Lua. Pending until the next fixed boundary; invalid/expired request errors.
---Phases: load, init, update, draw, ui_update.
---@param request integer # Live request returned by an asynchronous read or write. Range 1..4503599627370495.
---@return ScSaveStatus
function sc.save.status(request) end
---Submit a frozen checkpoint to the application writer; disk required. Host waits before the next fixed update, keeping UI/devices alive. Submission is not success; inspect status then release. No other save operation or scene change while unreleased.
---Phases: update.
---Capacity: One unreleased transaction per application; 1 MiB native encoded payload; 256 KiB state.
---@param slot string # 1..128 ASCII letters, digits, underscores or hyphens; project.id supplies the namespace.
---@return integer|nil request # Accepted request ID; acceptance is not disk completion.
---@return string|nil error # Preflight/submission error; no request was accepted.
function sc.save.write_async(slot) end
---Atomically save scene, shared state and chunk changes; a key maps to a state object or false to delete. Combined changes bounded to 256 KiB; update only, disk required.
---Phases: update.
---Capacity: 256 KiB combined changes; 16384 chunks; 1 GiB world.
---@param slot string # 1..128 ASCII letters, digits, underscores or hyphens; project.id supplies the namespace.
---@param changes table<string,table<string,ScData>|false> # Plain object mapping persistent-ID chunk keys to state objects or false for deletion; empty object checkpoints without changing chunks.
---@return boolean|nil ok # True on success, nil on an expected save/load failure.
---@return string|nil error # Diagnostic on failure; absent on success.
function sc.save.write_chunks(slot, changes) end
---Asynchronously commit a frozen checkpoint plus chunk changes with the same format and atomicity as write_chunks. Changes are copied at submission; false deletes a chunk. Host gates the next fixed update.
---Phases: update.
---Capacity: One unreleased transaction; 256 KiB combined Lua changes; 1 MiB native encoded payload.
---@param slot string # 1..128 ASCII letters, digits, underscores or hyphens; project.id supplies the namespace.
---@param changes table<string,table<string,ScData>|false> # Plain object mapping persistent-ID chunk keys to state objects or false for deletion; empty object checkpoints without changing chunks.
---@return integer|nil request # Accepted request ID; acceptance is not disk completion.
---@return string|nil error # Preflight/submission error; no request was accepted.
function sc.save.write_chunks_async(slot, changes) end
---Validate all {id,patch} records before commit; duplicate IDs rejected.
---Phases: load, init, update.
---Capacity: project.limits.entities.
---@param items ScEntityEdit[]
function sc.set_many(items) end
---Atomically create a dense batch and optional visual hierarchy in input order. Preflight fields, persistent IDs, capacities, child body/velocity, cycles, depth (32) and composed world poses before any creation. Empty batches return an empty array.
---Phases: load, init, update.
---Capacity: project.limits.entities.
---@param entities ScEntityPatch[]
---@param parents? integer[]|nil # Dense array matching entities: 0 for a root, otherwise a one-based index in this batch (forward references allowed). Child x/y/angle become local offsets. Omitted/nil creates independent roots.
---@return ScEntityId[]
function sc.spawn_many(entities, parents) end
---Copy the first failure observed at a scheduled boundary; nil while healthy or retrying. Does not expose background completion timing or change pins/cache order.
---Phases: load, init, update, draw, ui_update.
---@return ScStreamFailure|nil
function sc.stream.failure() end
---Read committed chunk data without waiting; nil until its planned boundary. Absent sparse chunks are empty. Load/init/update only.
---Phases: load, init, update.
---@param x integer
---@param y integer
---@return table|nil
function sc.stream.get(x, y) end
---Copy format, chunk_size, tilewidth/height, layers, groups, map properties, sparse object_coverage, tilesets and parallaxoriginx/y without chunk directory or object payloads. Load/init/update only.
---Phases: load, init, update.
---@return ScStreamMetadata
function sc.stream.metadata() end
---Open a built map index with a single worker and 128 MiB bounded cache; load/init only.
---Phases: load, init.
---Capacity: 128 MiB cache; 16 MiB index; 65536 chunks.
---@param index_path string
function sc.stream.open(index_path) end
---Release one chunk reference; zero references cancel visibility. Pending cancellation drains at its planned boundary.
---Phases: load, init, update.
---@param x integer
---@param y integer
function sc.stream.release(x, y) end
---Pin and prefetch a 32x32 chunk for a planned simulation tick. Deadlines follow request order; sparse empty regions return sequence 0.
---Phases: load, init, update.
---Capacity: 1024 pending requests; coordinates -31250..31250.
---@param x integer
---@param y integer
---@param commit_frame integer
---@return integer
function sc.stream.request(x, y, commit_frame) end
---Queue the current failed read again without waiting. Retains request sequence, deadline, references, cache reservation and old visible world. Clears failure until the next boundary attempt. Allowed in ui_update; draw forbidden.
---Phases: load, init, update, ui_update.
---@param sequence integer # Current failure().sequence; stale, pending, successful or unknown requests raise an error. Range 1..4503599627370495.
---@return boolean
function sc.stream.retry(sequence) end
---Read reserved cache bytes, visible/pinned chunks and scheduled request counts; worker completion timing is not exposed.
---Phases: load, init, update, draw, ui_update.
---@return table
function sc.stream.stats() end
---Atomically replace imported terrain with {x=0,y=0,w,h,one_way=false,vertices?} shapes in world pixels. Optional vertices are 3..8 local convex x,y pairs. Retain previous terrain on failure; remove finite room borders and camera clamping on success. Empty array clears imported terrain. Optional navigation {x,y,rows,cell_size=8} replaces the local grid in the same transaction and discards previous flow fields. Optional entering entity batch commits with terrain; returns IDs as second result. Invalid entities or capacity failure retain prior terrain/navigation. Load/init/update only.
---Phases: load, init, update.
---Capacity: 16384 shapes; Lua data conversion budget 256 KiB.
---@param shapes table[]
---@param navigation table|nil
---@param entities ScEntityPatch[]|nil
---@return boolean,ScEntityId[]|nil
function sc.stream.terrain(shapes, navigation, entities) end
---Transfer socket ownership to the application; room teardown no longer closes it. Mutating operation; forbidden in draw/ui_update, check mode and candidate initialization.
---Phases: load, init, update.
---Capacity: At most 4 named sessions; duplicate names fail without transferring socket ownership.
---@param name string # Exact string, 1..63 bytes without NUL. Case-sensitive application-local name.
---@return boolean|nil
---@return string|nil
function session:persist(name) end
