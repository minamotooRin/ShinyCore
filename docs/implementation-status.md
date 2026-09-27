# Complete edition implementation ledger

This ledger records implementation evidence, not release claims. The approved
complete-edition specification is the acceptance contract. All work belongs to
one delivery. Unchecked requirements remain outstanding.

Current priority (2026-09-26, user direction): finish missing functionality before
further performance optimization, with only necessary change-related tests. Sample
development now includes native visual review through hidden, unfocused captures;
interactive device/window tests still need a suitable desktop period. See the updated
[execution order](continuation-plan.md).

`sc.navigation.mask(radius?)` now exposes a bounded read-only snapshot of the
selected grid's exact path/flow clearance mask and world origin. It updates after
Tiled collision edits and enables one consistent source for future offline
streamed-chunk connectivity summaries. It does not yet route across unloaded
chunks. See [navigation mask verification](verification-navigation-mask.md).

The streamed-world helper now accepts world-pixel path and flow goals against its
published navigation window. Wayfarer's courier no longer reads internal chunk
tables or hardcodes their size. Focused negative-chunk/transition checks, the full
Wayfarer integration run and an inspected hidden native frame pass; see
[stream-world navigation](verification-stream-world-navigation.md). Routes across
unloaded sparse chunks remain open.

Wayfarer's checkpoint-gated ending now has a minimal streaming/graphics Release
package with network, advanced rendering and dev tools disabled. The relocated
package passed its full 3,245-frame scenario with only system PATH entries; a
hidden native dialogue capture was inspected. See
[minimal runtime release](verification-wayfarer-runtime-release.md). This is not
clean-system or cross-platform acceptance.

`ScScene` and `ScMap` now have structured native `--api` fields, defaults,
ranges and table constraints. The generated LuaLS/reference copies and 18
example annotations are synchronized; 11 affected manifested SDKs are now dev.75.
Full/lightweight builds and focused contract/generation checks pass. This
changes authoring metadata only; remaining work and release gates stay open.

Barrage now probes `last_result` on the application save worker after its title
initializes and writes victory/defeat results asynchronously. The result screen
holds until write completion, offers retry on failure and labels an unsaved new
challenge. A focused disk/restore/failure check, the full six-wave scenario and
inspected hidden native success/failure captures pass; see
[Barrage result saves](verification-barrage-result-async.md). Physical devices,
slow-disk behavior and final packages remain open.

`ScBody`, `ScBodyShape`, `ScProject`, `ScProjectLimits` and `ScResource` now have
structured native `--api` fields, defaults, ranges and constraints. Generated
LuaLS declarations include shader resources and streamed index declarations.
The full/lightweight contract checks pass, and project-local annotation SDKs are
pinned to dev.71. Runtime behavior is unchanged; remaining handwritten types and
final acceptance remain open.

Dragging a selection beyond a long input widget now clamps hit testing to the
visible viewport and advances multiline vertical or single-line horizontal
scroll in bounded UI-time steps. Release and zero-delta updates add no timed
overflow step; ordinary caret visibility adjustment remains active.
Focused replays and an inspected hidden native capture pass; five UI-using
project-local SDKs and the scaffold source are pinned to dev.60. Physical mouse
behavior remains unaccepted; see [drag verification](verification-ui-text-drag.md).

Native shared flow fields now treat a grid cell-size change as stale even when
dimensions and passability revision are unchanged. Refresh recomputes body
clearance using the new pixel size. The full build, focused navigation systems
test and Lua navigation contract check pass;
see [flow-field behavior](navigation-refresh.md). This does not add local shortest-
path repair or establish the large-unit performance target.

The three samples now use distinct, reproducible 20/16/15-second original music
loops. A shared offline builder reproduces each bundled Ogg byte-for-byte in the
current encoder environment; focused host runs cross all three loop boundaries
with one active voice. Crossing/Barrage lightweight packages contain the tracks,
and their hidden native captures were inspected. Speaker playback and final
subjective quality remain open; see [sample music](verification-sample-music.md).

Prefab merge now replaces any numerically keyed table as a whole, including sparse
arrays cleared with `{}`; string-keyed maps still merge recursively. The failing
real-host case was reproduced before the fix. Focused prefab and streamed-object
checks, five project SDK/content audits and inspected hidden Crossing/Wayfarer
captures pass; Wayfarer's pixels match its prior capture. Local SDKs and new-project
scaffolding are pinned to dev.59. See [prefab](prefab.md).

Wayfarer's title and forest now acquire the same application-owned theme voice.
The original 0.3-second placeholder has been replaced by a reproducible 20-second
Ogg melody. The full quest integration test checks unchanged voice identity and
continuous clock through ending, title and new journey; a 1201-frame title run
crosses the loop boundary. Hidden, muted native title/world captures were inspected.
Audible quality and physical-device playback remain unaccepted; see [audio](audio.md).

Crossing now has five small reproducible PCM cues for jump, landing, mechanism
success/failure and rescue. Focused headless playback states, deterministic cue
bytes, the unchanged three-room gameplay snapshot, a relocated development
package and an inspected hidden native aqueduct frame pass. Device listening and
final game quality remain open; see [Crossing audio](verification-crossing-audio.md).

Streamed prefab descendants now have plain room/root-ID/name-path references.
Resolution returns a fresh handle after chunk reload and distinguishes unloaded,
deleted, missing-path and stale states. A real two-process save test and the
focused streaming/image suites pass; Wayfarer's local SDK is dev.58. Dynamic
cross-chunk ownership remains open; see [streamed objects](stream-objects.md).

Streamed authored objects can now own a nested prefab tree. Native terrain and
entity publication preflight the full parent batch; chunk unload and explicit
deletion release descendants before the persistent root. The async world scans
child sprites for image residency and restores the group from root-owned explicit
state. Focused full-build host tests cover rollback, revisit, deletion, world
transitions and child image residency; full/lightweight builds pass. Annotated
project SDKs are pinned to dev.57. Per-child persistent identity and dynamic
cross-chunk ownership remain open; see [streamed objects](stream-objects.md).

Lua prefab composition now accepts nested named children with per-node component
data. It preserves direct leaf specs and sends the sorted tree through one atomic
native spawn batch; destruction releases descendants before ancestors. A focused
host test covers transforms, rollback and stale handles, and an inspected hidden
native capture shows nested visual attachments. Five project SDKs were pinned to
dev.56 at that checkpoint. Compound ownership in streamed object chunks now uses this module; see
[prefab](prefab.md).

Material create/uniform definitions and the info, capacity and pipeline results
now have structured native field metadata, generated LuaLS classes and reference
documentation. Material functions reject extra arguments instead of ignoring
them. Full/lightweight builds, the focused headless material test, generated-doc
check and local SDK audit pass; annotated projects are pinned to dev.55. GPU
failure/lifetime acceptance and the broader API metadata work remain open. See
[materials](materials.md).

Crossing now accepts horizontal left-stick displacement through its named actions,
while D-pad remains full speed. Its walk animation scales with speed, and the
controller HUD names the stick. The shipped 100-frame analog replay and focused
test verify half-speed travel, neutral release, reversal, disconnect and reconnect;
content checking and an inspected hidden native frame pass. This is simulated
controller input, not physical controller acceptance. See
[Crossing controls](verification-crossing-controls.md).

Streamed Tiled tile objects now inherit tileset collision shapes or solid/one-way
properties unless the object explicitly sets `collision=empty`. Offline baking
uses the same GID flips, alignment, tile offset, scale and rotation as drawing;
unsupported one-way transforms fail before publication. A focused built-map
headless test checks native rays and draw coordinates, and existing static object
coverage still passes. The builder cache version is 17; the chunk format stays 3.
This does not turn decorative images into physics bodies or complete platform
acceptance.

Crossing now draws small world-space details on its ferry/ledge surfaces, crate,
switches, gate and floor edge. Aqueduct ripples follow simulation time without
gameplay RNG. Three hidden native captures were inspected; an occluded first ripple
placement and plain mill ledge were corrected. Their gameplay snapshot fields match
the previous captures. See [Crossing presentation](verification-crossing-presentation.md).
Dedicated jump art, human play duration and final sample quality remain open.

Streamed Tiled object layers now bake `collision=solid/one_way` static shapes offline.
The world publishes them with tile terrain, retains cross-chunk owner anchors,
and preserves them when tile cells change; unloaded anchors remove their shapes.
Focused builder and headless runtime checks cover ray, navigation, patch and unload.
Builder cache version 15 leaves the two checked-in sample map payloads byte-identical;
the Wayfarer/TSX local Lua SDKs are dev.51. Static collision remains independent of
object deletion markers. See [streamed terrain](stream-tiles.md). Full acceptance
remains open.

Offline Tiled imports now write sparse cross-chunk object coverage into the stream
index. Region requests pin intersecting objects' anchor chunks while loading walls
and `contains` retain the true player/camera area. The ordinary stream-world
transaction creates and restores those objects. Focused builder, region and world
tests pass in Release; the two runtime cases also pass under headless ASan/UBSan.
Wayfarer's rebuilt map contains one such dependency. Its 550-frame travel replay
and inspected hidden native frame pass, with twelve SDK audits. Cache version is
14 and the eleven annotated project SDKs are dev.50. Static authored footprints
are covered; moving objects beyond them still need explicit anchor ownership.
See [coverage contract](stream-object-coverage.md). Full streaming and platform
acceptance remain open.

Map and nested group custom properties now survive the offline Tiled JSON build in
the readable chunk index and are exposed by `sc.stream.metadata()` with one-based
group ancestry. The package closure follows their `file` references. Focused asset
and package tests, full/lightweight builds, 30 headless ASan/UBSan frames, content
checking, all twelve SDK audits and an inspected hidden native sample capture pass.
The shared builder cache was version 13 at that checkpoint; eleven projects with
LuaLS annotations were pinned at dev.48, while the separate Snapshot SDK remains dev.1. See the
[map/group property contract](tiled-map-properties.md). Inputs are hand-authored
fixtures; editor-export, platform and complete-game acceptance remain open.

Named input bindings now validate every source and native device name before
publication; malformed keyboard/mouse/pad entries and mixed source fields fail
without replacing the active binding. New/bind/save copy their data, so caller
mutation cannot silently alter live controls. Four focused replay cases and five
project content checks pass; a hidden native UI capture was inspected. The five
projects bundling input.lua are pinned at dev.47; see [input actions](input-actions.md).
Physical controllers and full UI/device acceptance remain outstanding.

XML `.tx` object templates now join the JSON map import path, including inherited
GID remapping to a map TSX tileset or automatic import of a template-only tileset.
Effective `file` properties resolve from the template or map owner and enter the
package closure. Five targeted resource checks, a 30-frame headless example and
inspected native capture provide evidence; see [template contract](tiled-templates.md).
The example inputs are authored fixtures, not a real Tiled editor export. TMX maps
remain unsupported; complete game and platform acceptance remains open.

The offline Tiled importer now accepts external TSX tilesets referenced by JSON
maps. Atlas and sparse collection images, animation, collision shapes and file
properties flow into the existing format-3 chunk output. The package closure now
includes effective tileset/tile/layer file properties. Four focused asset tests,
two package checks, a relocated example, headless runtime and inspected native capture provide
evidence; see [TSX contract](tiled-tsx.md). Cache format was 12 at that checkpoint. TMX maps remain unsupported.

Barrage now has five distinct four-frame enemy sprites at native gameplay sizes.
The offline source script reproduces their PNG bytes. Content, short replay,
dependency closure and SDK checks pass; a five-kind native preview and one live
combat frame were inspected. See [sprite review](verification-barrage-sprites.md).
The full challenge and release acceptance remain open.

Aseprite PNG/JSON imports now restore trimmed frames, validate tags and durations,
and generate native sprite grids plus plain Lua animation catalogs. Focused asset
tests and the runnable animation_import sample passed content/SDK checks; headless
and hidden native observations agreed, and its PNG was inspected. Source metadata
is an authored format fixture, not an editor-export acceptance claim. See the
[import contract and evidence](aseprite.md). The shared builder cache was format 12
at that checkpoint; the sample SDK was dev.46.

The advanced_render aggregate capability now follows its build switch and agrees
with materials/postprocessing/geometry_shadows/normal_maps. A real full-build project
rejection was reproduced before the fix. Full/lightweight requirement and packaging
checks, headless sanitizer requirement checking and generated API consistency pass.
Build availability remains separate from graphics availability and platform acceptance;
see [capability contract](capabilities.md). No renderer or SDK change was needed.

Windows absolute Unicode CLI paths now survive argument and filesystem handling
through an embedded process UTF-8 manifest. Focused path IO checks pass on GCC
lightweight/full and LLVM-MinGW headless ASan/UBSan. Fresh Crossing/Barrage lightweight
and Wayfarer full ZIPs pass report-hash validation and launcher-driven native checks
after extraction outside the checkout into a Chinese/space-containing directory,
with only system PATH. Three PNGs were inspected; see [portable package evidence](verification-portable-packages.md).
These remain development packages. MSVC, other platforms, clean-system installation,
minimum Windows version, hardware and full product acceptance remain unverified.

Wayfarer adds a bundled transparent landmark atlas, seven passable camp/sign/stone/
log placements and a north-up locator with four district names. Read-only guidance
selects the current quest destination or nearest loaded herb without fetching distant
chunks; empty loaded regions invite further exploration. Content/guide and closure/
SDK checks pass; native camp, road and deep-forest views were inspected. See
[landmark evidence](verification-wayfarer-landmarks.md). The generated atlas is local
and its provenance recorded. Original terrain, save format and gameplay remain;
complete final art, human play duration and platform/package acceptance remain open.

Wayfarer now has independent traveler/courier idle and walk clocks, persistent
horizontal facing, equipment-range herb markers and bounded pickup feedback.
Presentation owns no saved state or gameplay RNG and freezes during menu/loading
pauses. The full keyboard quest retains its compared gameplay result; focused Lua,
content and dependency/SDK checks pass. Two native gathering captures were inspected;
see [Wayfarer presentation evidence](verification-wayfarer-presentation.md).
Original sprites remain; final world/foreground artwork and human play acceptance
are still outstanding. No native rebuild or SDK revision was needed.

Crossing now uses independent idle/walk/rise/fall animation clocks, preserves facing
when movement stops and resets presentation on rescue. Lever/signal/plate appearance
reads actual campaign progress, with shape as well as color cues; one signal-order
array is shared with the rules. A before/after three-room keyboard replay reaches
the same ending and compared gameplay fields. Focused Lua checks and content/package
checks pass. Native mill/signal captures and a corrected pose grid were reviewed;
see [Crossing presentation evidence](verification-crossing-presentation.md). Original
artwork is retained; bespoke jump frames, foreground art and final visual acceptance
remain incomplete. No native/standard-module change or SDK revision was needed.

All 182 registered functions in the full build now expose structured call contracts;
no missing entries remain in the lightweight build either. The final 27 core APIs
include strict optional-argument semantics, conditional map writes, named measure
returns and text options. Objects now rejects surplus arguments; emit/tone accept
the documented 0.001-second lower bound instead of rejecting it after float promotion.
Full/lightweight/headless-sanitizer focused core checks pass; existing Tiled edit and
particle atomicity checks, generated reference consistency and 16 dev.46 SDK audits
pass. See [core API evidence](core-api.md). Open dictionaries and resource-specific
constraints still require their documented semantics; function-level coverage alone
does not close the complete metadata or product acceptance requirements.

All fifteen network callbacks now use common structured API descriptors, retaining
context/guard closures; event and stats records include actual budgets and optional
fields. Session names reject numeric coercion. Conditional mutation metadata now
expresses explicit-nil writes without changing existing non-nil patch contracts.
Release/headless ASan/UBSan contract and Lua lifecycle checks pass, as does the
Release two-process paused room-transition exchange. Network-OFF lightweight build,
API/runtime exclusion and link graph checks pass. Both builds agree on shared API
documentation; 16 project SDKs are pinned at dev.45. See [network contract evidence](verification-network-contracts.md).
There were 27 remaining unstructured entries at that checkpoint; the later core
contract work above completes function-level coverage.

Application, settings, state, watch and require now expose ten structured contracts.
Application/settings bindings have a dedicated file. Strict argument counts and watch
name validation prevent ignored inputs and NUL-prefix aliasing; invalid UTF-8 or
oversized serialized observations are rejected before replacing existing data.
Release and headless ASan/UBSan focused boundary, menu, settings persistence/candidate
and state checks pass. SDK annotations are synchronized in 16 projects at dev.44;
see [application and state](application-state.md). Remaining unstructured APIs:
42 of 181 full-build function entries at that checkpoint (now 27 after network contracts).
This does not establish final platform/device acceptance.

All seven projectile functions now expose structured parameters, phases and returns,
plus spec/hit/stats records. Strict boundary checks reject numeric strings, surplus
arguments and embedded-NUL resource names without mutating live data. Floating field
validation and metadata share descriptors; defaults come from the native spec.
Release and headless ASan/UBSan contract, capacity, atlas and batch checks pass.
Generated annotations and 16 local SDKs are synchronized at dev.43; see
[projectile contracts](projectiles.md). Full API metadata remains incomplete:
52 of the full build's 181 function entries lacked structured contracts at that checkpoint
(subsequent application contracts reduce this to 42, above).

The standard Lua animation module now validates/copies clip data, returns first-frame
and crossed-frame events in order, supports finite nonnegative speed, and preserves
state on transition-budget failure. Barrage uses it for idle/walk/dash and independent
enemy clocks. Focused native-VM contracts and a 600-frame ordinary challenge comparison
pass; gameplay fields match the prior version. A native pose preview caught reversed
atlas artwork misuse, corrected and re-reviewed together with an actual dash capture.
See [sprite timelines](animation.md). SDK dev.42 is pinned in the four carrying projects;
final foreground art and continuous animation-quality acceptance remain outstanding.

Prefab now creates roots and children with native relationships in one transaction.
Its duplicated Lua transform/update path has been removed. Optional parent indices
in spawn_many allow forward references, preflight complete hierarchy geometry and
reject invalid batches without changing entities, generations or persistent IDs.
See [prefab contract and evidence](prefab.md). The new attachments sample persists
names and local poses, then atomically rebuilds the graph in a new room. Its save,
detach, reset and reload replay restores the same explicit data with new handles.

Native visual attachments now store a generation-checked parent and local pose in
each entity, synchronize after physics/root movement and interpolate along parent
chains. Cycles, depth above 32, bodies/velocities on children and invalid mutations
are rejected. Parent removal detaches direct children at their current world poses.
Release and full headless ASan/UBSan focused native/Lua checks pass, along with the
existing presentation replay/save check. A hidden native capture was reviewed and
matches headless fixed fields; see [attachment contract](attachments.md). Generated
API annotations and project SDK records are synchronized at dev.41. Batch/prefab and
saved hierarchy checks also pass in both builds; the new restored sample image was
reviewed and matches headless fixed fields. Fractional alpha geometry tests do not
establish high-refresh hardware visual acceptance; final sample/platform acceptance
remains outstanding.

Crossing now derives player objectives and target markers from actual campaign
progress, with nearby interaction prompts and expiring notices. The full existing
keyboard route/save checks and focused objective rules pass. Reviewed mill/signal
captures caught and resolved label overlap and floor occlusion; see
[guidance evidence](verification-crossing-guide.md). This does not establish final
art/audio quality or 5–10 minute human playtime.
The three rooms now have a local generated panorama atlas, drawn with the existing
image layers without extra entities. Content/package checks and three reviewed
checkpoint-restore captures pass; matching gameplay fields confirm the two comparable
restores are unchanged. See [background evidence](verification-crossing-scenery.md).
Foreground art, animation/audio and final sample acceptance remain outstanding.
Crossing now uses named keyboard/controller actions with independently persisted
bindings, controller pause/settings/save/load navigation and held-input isolation.
The complete controller replay reaches the same campaign state as the keyboard
route. Two hidden native captures were reviewed after fixing legend overflow;
physical controller/hot-plug acceptance remains pending. Per-room `--check-all`
now receives the settings service, verified with Release and ASan/UBSan; see
[controller evidence](verification-crossing-controls.md).

Barrage has bounded cosmetic hit/heal feedback, ranged-attack warnings, enemy and
guardian health, segmented player health and dash cooldown. Before/after six-wave
state and watches match exactly; actual battle and explicitly staged visual states
were reviewed through hidden native captures. See [combat feedback](verification-barrage-feedback.md).
This does not complete sample art/audio or hands-on usability acceptance.
Its new local arena background and recessed beacon seal are drawn below gameplay
with one image command. Battle/feedback captures preserve the recorded gameplay
fields and have been reviewed; provenance, prompt and package roots are included.
See [arena evidence](verification-barrage-arena.md); enemy art and final acceptance
remain outstanding.

Barrage now has nine local synthesized combat/outcome cues with bounded repetition,
stereo positioning, bus routing and explicit voice priorities. Persistent theme
music survives result-load/new-challenge rooms. The complete six-wave gameplay
result is unchanged; native logical voice capacity, restart continuity, asset and
package checks pass. See [audio evidence](verification-barrage-audio.md). Actual
speaker playback and subjective mix quality remain unverified.

Named Lua actions now accept directional analog axes and expose continuous values,
preserving digital bindings and UI consumption through return to neutral. Barrage
uses twin sticks, dash and controller menus with a shipped full challenge replay;
keyboard/controller results match, fractional movement and pause isolation pass,
and controller UI captures have been reviewed. See [analog actions](verification-analog-actions.md).
SDK dev.36 is pinned in the five projects using the input module. The shared
SETTINGS / CONTROLS panel now supports transactional key/mouse, button and axis
rebinding, defaults, cancellation and conflict checks; Crossing/Barrage use it.
Focused checks and actual save/restart captures pass; see [rebinding evidence](verification-rebinding.md).
Physical device acceptance remains outstanding.

Wayfarer now uses its own named-action/rebinding profile, analog movement and
controller journal/dialogue/menu/save handling. Same-update UI consumption holds
until release; name entry preserves I as text. Keyboard/controller full missions
match, journal isolation and package/SDK checks pass, and the controller prompt
capture was reviewed. See [Wayfarer controls](verification-wayfarer-controls.md).
Physical devices and final sample acceptance remain outstanding.

The Lua tween module now composes nested sequence/parallel/delay nodes, preserves
surplus time across stages and captures each stage's origin when it starts.
Cancellation preserves values; duplicate ownership is rejected. Focused contracts,
new sample completion and native/headless state agreement pass; the hidden native
sample image was reviewed. See [tween timelines](tween.md). This is SDK dev.37 in
the root distribution and new sample, without repinning projects that omit tween.

Navigation obstacle masks now update only cells covered by added/removed geometry
for finite tile edits and streamed terrain replacements that keep the grid.
Overlapping contributors are re-evaluated, order-only changes are ignored, and
both grids retain transactional preflight. Focused native/Lua checks pass in full
Release and headless ASan/UBSan; see [dirty-cell evidence](verification-navigation-refresh.md).
Geometry gathering/comparison remains, and flow fields still use budgeted full BFS;
this does not claim dynamic avoidance or performance acceptance.

Offline Tiled object templates now merge properties by name, resolve effective
file properties against their source document, and remap inherited tile GIDs to
the map's external tileset range. Template-only sets receive a new range, including
sparse image collections. Packaging includes final object file properties and
collection images. Focused tests and a native run from selected runtime files pass;
see [template data pipeline](tiled-templates.md). Asset-builder version is 8;
stream format stays 3. This does not establish all Tiled rendering coverage.

Compiled group-layer tint now survives into tile/image rendering: component-wise
inheritance stays separate from opacity and converts Tiled ARGB to native RGBA.
Invalid layer values and unsupported blend/color-key modes fail with source context.
Tool version 9 and Wayfarer's local stream module (SDK dev.38) are synchronized.
Targeted import/default-render tests and two-frame native/headless comparison pass;
actual tinted pixels and layout were inspected. See [layer tint](stream-tiles.md).
Object factories still choose how to apply their layer's presentation metadata.

The application now shares one `ScContentLoader` worker between map jobs and native
PNG decode jobs. Image requests own their inputs/results and reserve up to 128 MiB
of queued RGBA output; the CPU parser separately bounds encoded input and workspace.
Release and full-feature headless ASan/UBSan checks cover pixels, source faults,
cancellation, room isolation and fixed publication. The lightweight build excludes
the new decoder. Native CPU/GPU image caches now add counted references, bounded
LRU eviction, stale IDs, retry and limited host-thread uploads. Focused Release and
headless ASan/UBSan checks pass; an actual hidden GPU color capture was inspected.
Host/Lua integration now exposes explicit `sc.images` replacement transactions,
fixed-boundary waiting, retry/cancel and committed-image validation. Resources with
`stream=true` bypass eager decode; normal-map dependencies are checked when used.
Stream-world now collects tile/animation/image-layer and owned-object dependencies,
prepares images before outgoing saves, and publishes with terrain/objects. Image-set
changes during map edits use the same transaction. Native preparation accepts declared
paths and eager resources and includes bound normals. Wayfarer streams its forest and
herb textures; initial room sprites remain eager. Explicit `sc.images.reload` and `World.reload_images` now stage private revisions,
preserve active pixels on failure/cancel and publish future cache lookups only on commit.
Other native owners retain older versions until release. Scene `preload_images` now prepares initial streamed images after init and before
first draw. Candidate ownership persists across host frames; input/time and trace
publication wait while the active UI/draw remains available. GPU/first-draw failure
preserves the active runtime. Eager resources and Lua/project/map parsing remain
synchronous; full room-loading asynchrony is not yet complete. API copies now use SDK dev.29.
See [native residency and its limits](image-residency.md).

Nine-slice images now reuse the existing draw queue and emit up to nine quads per
command, retaining corners, shrinking undersized destinations and transforming
insets with flips/diagonal UVs. Image options and slice fields have structured API
metadata. Lua UI adds nine anchors, validated layout patches and node/theme skins;
`examples/ui_panels` demonstrates the complete path without optional modules.
Focused native/ASan checks and inspected hidden captures are recorded in
[UI style evidence](verification-ui-style.md). IME now carries bounded clause and
conversion segments through Windows sampling, replay, Lua, diagnostic hashes and
grapheme-aligned UI presentation. Focused Release/ASan checks and a new inspected
native capture are recorded in [IME segment evidence](verification-ime-segments.md).
UI now propagates layout invalidation through parent flow while reusing unchanged
subtrees, separating text updates from geometry and visible order. Focused layout
equivalence/interaction checks, inspected native output and the sanitizer-checked
inspection protocol are recorded in [layout evidence](verification-ui-layout.md).
Five UI projects use SDK dev.30. Large UI workloads and physical IME/platform
acceptance remain open.

Persistent music can now be acquired by resource path through `sc.audio.music`.
It reuses a live persistent voice without resetting position; aliases share the
same matching path and candidates retain transaction isolation. Crossing uses it
without storing handles in saves. Focused Release/ASan, contract, muted candidate
rollback and the three-room sample trace pass; see
[music evidence](verification-persistent-music.md). SDK annotations are dev.31;
audible hardware continuity remains unverified.

Paused Lua local tables now support bounded raw path lookup and pagination, with
lossless 64-bit integer keys and no expression/metamethod execution. Inspection
restores the VM stack on failure and shares a 65,536-entry request budget. New local
table protocol checks and existing line-step/budget checks pass in Windows Release
and full-feature headless ASan/UBSan. Loading-stage breakpoints now cover project,
require, init and first draw (`--debug-load` for initial startup), plus candidate
loads with existing breakpoints. Focused Release/ASan checks pass, and a reviewed
two-frame hidden GPU failure check preserves the old room after a candidate stop.
Native panels now support Agent selection via the graphical panel command and
reuse project fonts. Hidden captures verify Chinese tree names, UI pagination,
scrolling and tooltip status. Physical panel keys remain unverified; see [debug protocol](debug-stdio.md).

Logical audio now belongs to the application, with isolated candidate-room drafts
and host-side fixed updates in `src/audio/audio.cpp`. Failed candidate mutations do
not stop active music or alter buses; successful commits keep persistent IDs and
positions and expire room sounds. Mixer tests, existing host audio tests and an
eight-frame hidden/muted transaction check pass; the mixer and host tests also pass
on the rebuilt full-feature headless ASan/UBSan build. The native adapter now lives
in `src/audio/device.cpp`, with silent candidate preparation before the GPU/audio
commit, retained persistent streams and shared decoded samples. A fake-device test
using real raylib declarations passes in Release and ASan/UBSan, including failure
injection and resource release order; the rebuilt hidden/muted host check passes.
Repeated CPU preflight decode avoidance and actual device playback remain pending;
see [audio](audio.md).

Audio function/field metadata now drives generated Lua annotations and reference
entries, including default bus routing and conditional bus mutation phases.
Priority validation no longer rounds fractional inputs to integers; handle/name
checks reject coercible strings and embedded-NUL aliases. Focused Release and
headless ASan/UBSan checks pass. The type-block generator preserves following
namespace/function/alias declarations; its regression and synchronization checks
pass. The six annotated example SDKs now use `1.0.0-dev.7`, including updated
input-text returns; the four UI examples include IME cursor/target display. Other unstructured APIs
remain outstanding; see [contract evidence](verification-contracts.md).

Navigation now has structured parameters, defaults, phases, result fields and named
multiple returns. Blocked goals return `unreachable`; generation-checked flow handles
cannot revive after slot/region replacement, and steering validates a dense distinct
entity batch before changing velocities. Bindings and shared region preparation live
in `src/script/script_navigation.cpp`. Focused Release and headless ASan/UBSan checks
pass, including generation exhaustion and VM reuse; see [navigation evidence](verification-navigation-refresh.md).
Path queries and shared fields now accept circular body clearance against blocked
cells and grid boundaries; refresh preserves the chosen radius. Focused geometry
and Lua contract checks pass in Release and ASan/UBSan, with generated API copies
at SDK dev.11. Dynamic-agent avoidance and local dirty-region rebuilding remain open.

All eleven physics functions now expose structured parameters, defaults, phases,
capacities and four result/patch record types. Bindings reject excess arguments,
numeric-string handles, embedded-NUL joint names and metatables on query arrays.
Focused Release and full-feature headless ASan/UBSan contracts pass; two existing
shape-query and joint lifecycle cases also pass under sanitizer. Generated annotations
and the six annotated SDKs are synchronized at dev.16. The generator now preserves
adjacent class fields/aliases and exact integer bounds, with a field-completeness
check in addition to regeneration consistency; see [contract evidence](verification-contracts.md).

The user's Chinese-text review exposed insufficient detail in low-resolution UI
captures. Screen UI now composes at window resolution with size-specific TTF/OTF
glyph pages and unchanged logical layout. Actual dialogue/title/journal PNGs and
focused native clipping/resize checks pass; the former readability judgment was
withdrawn. World text remains at scene resolution. See [visual evidence](verification-sample-visuals.md).

Packaging now audits native imports, transitive explicit runtimes, architecture and
redistribution notices, with per-library size/dependency reporting. Three Windows
full-module sample ZIPs passed relocation with only system PATH entries and hidden
native screenshots were inspected. Explicit package manifests now select script/resource
roots, recursively collect literal Lua dependencies and all indexed stream chunks,
and require declarations for dynamic modules. Scaffolds and the three samples include
manifests; staged projects are validated before publication. Local SDK manifests now
pin module/annotation/license hashes and exact engine/contract versions, with explicit
versioning for local modifications and reports of the shipped subset. Four focused
SDK checks, three tooling checks and an actual Wayfarer package passed. Clean-system
installation and Linux/macOS native packaging remain pending; see [packaging](packaging.md).
Copied binaries now receive a fresh relocation audit that rejects stale search paths
and dependency names. Mach-O paths are inspected per architecture and differing slices
are rewritten separately before merging. Eleven dependency-tool checks and one actual
Windows package relocation check pass; ELF/Mach-O command execution remains mocked.

Input widgets now lay out IME composition as a temporary selection replacement,
with accent underlines, wrapping/scrolling and candidate anchoring at the preview end.
Observed composition and its ending frame suppress duplicate editing commands;
commit is undoable once, cancellation preserves content, and same-tree focus changes
discard the old session's pending text. Single-line paste strips newlines; multiline
input normalizes CRLF/CR. Focused headless composition, drag-selection and modal tests
pass. Native IME cursor and first target-segment positions now pass through input
snapshots, replay and UI layout, using bounded UTF-16 conversion and grapheme-aligned
display. Focused Release/headless ASan checks and reviewed one-frame native captures
pass. Full clause attributes, actual IMM messages/candidate positioning and device
behavior remain unverified or unimplemented; see [UI contracts](ui-lifecycle.md).

Vertical UI scroll containers now retain stable content extents, clamp after layout
changes, reveal focused descendants through nested viewports and route exhausted
wheel movement to ancestors. Containers/lists expose a draggable scrollbar with
capture, page clicks and modal/disabled cancellation. UI.scroll_to provides explicit
bounded positioning; inspection reports range/capture. Mixed-height grid rows use
their actual maximum height. Focused headless tests pass; native pixels remain unverified.

UI tooltips now attach to stable node IDs, use hover or keyboard/gamepad focus,
respect modal/disabled/clipped controls and overlapping owners, and expose bounded
popup rectangles through UI.inspect. Delay, wrapping, edge placement and truncation
are prepared during update; drawing is passive and escapes ancestor clips. Escape
dismissal participates in named-action consumption. Focused headless tests pass;
raw native inspection now includes tooltip bounds/visibility and scroll capture.
Hidden native tooltip/scrollbar and panel captures have been reviewed. Physical
input and large UI acceptance remain pending.

Lists now accept stable row IDs and retain selection across filtered/sorted data
replacement, clear removed selections, and validate batches before mutating the tree.
Lua/native inspection expose the selected row ID without invoking metatables. Focused
behavior checks and the existing 10,000-row bounded-draw replay pass; native inspection
also passes ASan/UBSan. Reviewed hidden captures verify highlight/scroll positions and
the corrected focus-border draw order. Wayfarer now submits keyed inventory rows;
the four UI sample SDKs are pinned to `1.0.0-dev.6`. This is not the 500-control UI
performance acceptance; see [UI contracts](ui-lifecycle.md).

Buttons/checkboxes/tabs/lists now accept Space alongside Enter/gamepad confirmation,
preserving text-entry semantics and same-dispatch consumption when callbacks clear
focus. Normalized sliders validate values/steps atomically, support page/end-point
keys and honor same-frame mouse taps plus final drag-release coordinates. Defaults
do not emit change events. Focused headless control tests and the actual settings
menu replay pass; slider track/thumb pixels remain unverified.

Crossing now has three distinct authored rooms: ferry/lever aqueduct, crate-pressure
mill with a physical ramp, and ordered beacon switches. Shared gameplay and explicit
campaign data replace the repeated-room prototype. Checkpoints reconstruct collected
object IDs, gates, switches, positions and motion phase; ending/new-journey flow and
local original sprite/audio assets are included. All-room content checks, first-light
checkpoint/fresh-process restore and mechanism rules pass headlessly. A keyboard-only
2,876-frame replay now completes all three rooms and 15 pickups without deaths;
the focused check verifies ferry/ramp support contacts, plate occupancy, ending-save
recovery and new-journey restart. Water now returns the player to camp instead of
allowing the solid map boundary to serve as a hidden floor. Human playtime, native
pixels/audio and portable final packages remain outstanding; the optimized replay's
48 simulated seconds do not establish the 5–10 minute playtime target. See the
[sample contract](../examples/crossing/README.md).

Barrage now has six timed waves, four regular enemy behaviors, a final guardian,
friendly/hostile projectile batches, textured particle bursts, dash/invulnerability,
five modal upgrade choices and victory/defeat/restart. Gameplay rules and the fixed
challenge seed live in a plain Lua module; local original art/audio are included.
An actual 18,138-frame keyboard replay completes 302 active seconds and 317 enemies.
Focused checks cover unchanged final results after a menu pause, real defeat,
upgrade rules and a fresh challenge. Results now persist in `last_result` (sample
data version 3); the title restores the authored victory/defeat screen, while a
new challenge resets gameplay. Restore/restart checks and native outcome screenshots
pass. Full presentation/audio acceptance, hands-on usability and final packages
remain outstanding. See the
[Barrage sample](../examples/barrage/README.md); this is not benchmark acceptance.

Wayfarer now separates title/save selection from its streamed room, preventing
old chunk deletions from mixing with a fresh inventory. A real 3,240-frame route
collects all 24 herbs, clears the road, delivers the quest and restores its ending
from disk. All 16 authored chunks, object deletion markers and the road edit are
checked. Cancelling a new journey preserves the slot; confirmation resets both
explicit state and chunk records. Original sprite/audio assets are bundled locally.
The optimized route's 54 seconds do not prove 5–10 minute human playtime.

Sample visual review now has actual hidden native captures, including Chinese
dialogue/journal and representative Crossing/Barrage gameplay. Final-frame capture
was corrected to read before buffer swap; focused native pixel/visibility checks
pass. See [visual evidence and limits](verification-sample-visuals.md). Remaining
art, audio-device, usability and platform gates are not implied. Restored endings
for all three samples and Barrage defeat have now been captured and inspected,
including outcome statistics and focused primary actions. The capture tool reaches
these saves through actual input and keeps them for bounded visual rechecks; it
does not inject completion or require replaying all gameplay on the GPU. Lua SDK
`1.0.0-dev.2` contains the optional shell outcome fields; the three sample pins are
updated, while other projects retain their existing fixed SDK versions.

[Camera controls](camera.md) now share a core transform for follow, zoom, rotation,
custom/map bounds, isolated fixed-step shake and logical screen/world conversion.
The backend uses it for world passes, culling and lighting, retaining fixed screen
UI. Streamed tile/image layers now submit world coordinates and read the same view,
including parallax and rotated/zoomed culling. Focused Release and headless ASan/UBSan
checks, related streamed-image checks, lightweight/full compilation and API metadata
checks pass; GPU pixels remain unverified.

[Display interpolation](presentation.md) now owns bounded previous-frame geometry,
with explicit entity/camera cuts and birth/death/slot-reuse semantics. Native entities,
batch effects, glows and dynamic occlusion share display poses; Lua pose and camera
queries use interpolation only during draw. Fixed gameplay reads remain unchanged.
Focused Release and headless ASan/UBSan pose, batch-compaction, Lua-boundary and
lighting checks pass; the same input replay matches gameplay fields and save state
with interpolation on/off. Lightweight/full builds and API metadata checks pass.
Native pixel verification remains outstanding.

Optional [materials](materials.md) now provide room-generation handles, typed atomic
uniform patches, shader resources, explicit reload and surface bindings. Image
defaults cover sc.image, entity sprites, finite/streamed tiles, atlas projectiles
and textured particles. Generation-checked entity overrides also cover geometry;
builtin rendering can be selected explicitly. Referenced image/live-entity materials
cannot be destroyed. Binding records belong to the room, outside simulation state. GL
programs compile/link/validate separately before replacing valid resources. The
materials capability is distinct from the still-incomplete advanced_render group.
The expanded sample now has a reviewed hidden native screenshot and ready GPU state.
Native compile/uniform failures retain the previous program's pixels; repaired reloads
replace them. Auxiliary samplers and long-running resource lifetimes still need checks.
Release/headless ASan/UBSan material tests cover atomic values, stale/cross-room
handles, capacity and source reload failure/recovery. The disabled-module build and
capability rejection pass; the [example](../examples/materials/README.md) runs headlessly.
Focused binding and postprocess checks pass in Release and headless ASan/UBSan,
covering aliases, atomic errors, retained references, slot reuse and room reset.
The expanded sample switches image defaults in a three-frame headless replay.

The optional [postprocess chain](postprocess.md) now supports up to four ordered
passes, atomic configuration, retained material references and bounded one/two
color-only targets. Bloom, grading and distortion have authored GLSL examples and
a mode-switching replay. Screen UI is submitted after the chain. Headless contracts
are validated; native off/combined captures now verify world changes and unchanged
opaque UI glyphs. Individual warp/grading modes and GPU lifetime checks remain
unverified. The normal-map stage is described below.

The new [geometry shadow stage](lighting.md) now collects committed Tiled/streamed
terrain and rotated/compound entity bodies, with fixed polygon approximations for
curves. CPU visibility outlines include corner rays; bounded source samples provide
adjustable soft shadows. `sc.lighting` exposes atomic settings and submission usage.
These CPU/boundary checks do not verify native pixels or the lighting benchmark.
Explicit draw-time point lights now expose per-light RGB/alpha, intensity, projection,
source quality and self-exclusion. CPU preflight merges glows and commands with a
32 visible / 16 projected light budget; headless and native draw use the same checks.
Normal mapping now uses same-size image/atlas bindings shared by world images,
sprites and tiles, with a normal buffer feeding the existing shadowed light stage.
Source height and tangent transforms have CPU/authoring checks; see [normal maps](normal-maps.md).
Independent entity occlusion now supports default
body rules, visual bounds, stored shape and disabling projection. Generation-qualified
room records allocate with the entity budget; they never create/change physics or
carry overrides across slot reuse. Arbitrary texture-alpha silhouette extraction is
not provided; visual bounds explicitly mean a rotated rectangle.
Focused Release/headless ASan/UBSan checks cover these modes, stale/reused handles,
room reset and unchanged world hash/solver ownership. The advanced-module-OFF check
passes; the sample includes a bodyless decoration with switchable projection.
Normal binding/transform and sample checks pass in Release and headless ASan/UBSan;
the disabled-module build rejects normal_maps and omits its API. GPU shader/program
and color-target ownership is shared with materials/postprocessing. Hidden native
checks found and fixed lost normal-texture binding when changing primitive mode;
six XY/flip/rotation probes now distinguish facing/away brightness, and the normal
sample plus hard/soft shadow captures were reviewed. The postprocess sample now
selects one atlas frame at the correct aspect ratio. These short checks do not
establish GPU fault recovery, leak freedom or benchmark compliance; see
[native evidence](verification-advanced-render.md).
Candidate GPU/script/dimension failures now complete the active room's simulated tick
without skipping trace or debugger notifications. Successful later room commits clear
the previous error overlay. Focused hidden protocol/pixel checks pass, including small
viewport error wrapping, plus the audio room transaction check and a rebuilt headless
ASan/UBSan material check. GPU allocation failures and long-running cycles remain pending.
Release and headless ASan/UBSan geometry/authoring checks pass, including the sample
replay; the disabled-module build rejects the capability and omits the Lua API.

Optional [debug-stdio protocol](debug-stdio.md) now supports request IDs, paused
startup, continue/pause/frame steps, paged entity/state/watch inspection, bounded
input and EOF pause retention. Logs remain on stderr and stdout stays JSON Lines.
The devtools capability reflects this implemented subset; ready lists commands.
Lua source breakpoints, step in/over/out and raw stack/locals inspection now preserve
the real call stack and instruction quota. Initial/candidate load/init breakpoints
and bounded local table expansion are implemented with focused checks. UI trees register weakly and expose
paged raw inspection including focus and inherited visibility/enabled state.
Entities, declared resources and capacity/Lua-memory metrics share readers with an
opt-in native panel (F4 with --debug-keys or the graphical panel command). Reviewed
hidden captures cover CJK tree names, UI pages, scroll ranges and tooltip status.
Physical keys and detailed phase timings inside the panel remain unverified or unfinished.
Focused UI/stdio process tests pass, including UI inspection while stopped in Lua
under ASan/UBSan. The devtools-OFF build also passes its API omission, ordinary UI
and disabled-channel checks; see [protocol evidence](debug-stdio.md).

Streamed tiles now expose rectangular solid/one-way terrain preparation and an
atomic native terrain replacement API; failed replacements retain prior bodies.
Successful commits remove finite room borders and camera clamping. See
[stream terrain contract and focused verification](stream-tiles.md).
Tile collision object groups now bake rectangles, concave polygons and 16-sided
ellipse approximations offline, with rotation and runtime GID transforms. A focused
end-to-end test covers negative-coordinate and flipped native ray hits plus invalid
geometry diagnostics. Bounded local navigation grids now support negative world
origins, unloaded holes, terrain obstacle publication and budgeted flow refresh;
terrain and a replacement navigation grid can now publish atomically in one call,
including default-grid obstacle refresh. Focused host and sanitizer verification
covers failure retention. Entering entity batches and region loading walls now share terrain preflight and
publication, retaining old state on invalid drafts or capacity errors. Global
sparse-world routing and complete render/physics resource publication remain open.
The Lua stream-object owner now optionally publishes prepared objects, terrain,
navigation and region walls together, including saved deletion markers. A targeted
host test covers failed publication and disk-save restoration. Outgoing persistence
can now be coordinated through Objects.transition: export/prepare, save snapshots,
publish entering state, then release outgoing entities. Disk or publication failure
retains active outgoing objects; successful saves are not rolled back if publication
fails. A focused failure/revisit test passes. Stream-world now wraps these drafts in
async saves; cross-process crash atomicity and GPU resource publication remain outstanding.

The Lua [stream-world coordinator](stream-world.md) now owns active chunks and
combines interest requests, object save/restore, terrain/navigation publication,
loading walls and prepared draw-list replacement. A focused headless revisit test
covers first load, persisted changes and clearing the region. Atomic loaded-tile
patches now update terrain/navigation and persist sparse overrides together with
object snapshots; explicit save preserves active objects. A focused edit/remove/
revisit test also verifies native collision and navigation results. Writes now use the
async IO service; streamed saved-state reads are also asynchronous. GPU streaming remains outstanding.

Wayfarer now uses the coordinator with a 16-chunk authored forest, persistent herb
objects, a removable terrain obstacle and explicit checkpoint saving. Its local SDK
and assets are bundled. Check-all, startup smoke and a short boundary-crossing/save
replay pass headlessly. Explicit healer quest stages, equipment tradeoffs and a
budgeted shared-field courier patrol are implemented. A focused journal replay and
fresh-process checkpoint check pass. Chinese quest/journal/HUD text and name input
now use a licensed full Source Han Sans SC resource with pinned provenance. The
focused check covers Chinese input persistence and native metrics for all dialogue
branches; native pixels, physical IME and the complete RPG experience remain open.

Terrain collision bodies now have [32×32 chunk ownership](terrain-chunks.md),
with unchanged-body retention and transactional replacement of changed chunks.
This is a foundation for streaming; joint render/physics/object publication and
loading boundaries are still incomplete.

Streaming asset format 3 partitions objects into anchor-owned chunks, including
object-only regions, with persistent IDs and layer references. Import validation
and native publication reject invalid object records; automatic entity creation,
unload/save/restore and cross-chunk geometry coverage remain incomplete.

`sc.spawn_many` now preflights an entire entity batch, including identity and
entity capacity, before committing. Failed batches retain registry states and
handle generations. This is the object-creation primitive for streamed commits;
the complete terrain/render/object transaction remains outstanding.

The Lua [stream object owner](stream-objects.md) now prepares and batch-creates
chunk entities, exports explicit snapshots, saves before unloading, and restores
data and deletion markers on revisits or a fresh process. Failed saves retain
active entities. Low-level object calls remain synchronous utilities; stream_world
adds interest regions, joint publication and asynchronous writes. Compound object
ownership remains outstanding.

Object owners also support multi-chunk load and unload batches: preparation precedes
one native spawn batch, and all outgoing snapshots share one save commit before
release. The [interest-region module](stream-regions.md) unions player/camera areas,
prefetches a margin, retains old pins until explicit commit, and exposes coverage.
Optional region boundaries now install transparent static entity walls around the
committed union, replacing them only after successful batch creation. A targeted
headless test covers blocking, expansion and failed replacement retention. Physics
filters/kinematic movement still require gameplay policy; joint world publication
remains outstanding.

`sc.image` now accepts validated atlas source rectangles, UV flips/diagonal
transforms and tint/alpha in the existing draw queue. PNG dimensions are checked
headlessly as well. This supplies the image primitive for streamed tile drawing;
the streamed tileset/layer renderer and native pixel verification remain pending.

The initial [streamed tile renderer](stream-tiles.md) now consumes a bounded header
copy from `sc.stream.metadata`, prepares atlas tile layers in stable global row order,
and submits animated, flipped, tinted and parallax-adjusted image commands with
viewport culling. Image layers now include imported PNG dimensions, repeated
backgrounds, parallax origins and bounded image subdivision. Collection tilesets
now resolve independent PNG dependencies, sparse IDs and variable-size animation
frames with bottom alignment. Explicit image layers now share a stable native
scene order with map layers and entities; streamed tiles submit those layers.
Ordering storage is prepared with room resources, and UI-clip misuse is rejected.
Terrain integration and native pixel acceptance remain incomplete.

Optional scene `ui_update(dt)` runs before gameplay, including loading/debug waits.
Live input updates UI once per display; replay/headless inputs update it before each
fixed tick. Lua [action consumption](input-actions.md) now bridges the UI and gameplay
contexts with independent held histories, pending edges, catch-up coverage and
release suppression after menu close. Raw native input and recordings stay intact.
UI text focus survives gameplay; hiding the root removes its input targets.
Focused Release and full-feature headless ASan/UBSan checks pass. A seven-frame
hidden native replay matches headless watches; its Chinese capture and a one-frame
live host-order capture have been reviewed. Physical devices remain unverified.

Stream read failures now expose an owner-published record and a nonblocking retry
operation usable from UI. Retries retain the original sequence/deadline, pins and
cache reservation; the old visible set survives until the due batch succeeds.
Headless UI can explicitly retry or quit; unhandled failures still terminate.
Controlled-reader and real missing-file repair checks pass in Release and full
headless ASan/UBSan. A three-frame hidden native recovery capture was reviewed;
GPU residency and other loading-failure recovery remain open; streamed saved-state reads now run asynchronously.
The host now owns one lazy chunk-reader thread shared by active and candidate rooms.
Jobs own their inputs; destroying a room cancels queued jobs and discards in-flight
results without waiting for disk. Controlled lifetime/cancellation, room isolation,
ordered publication and real-file recovery checks pass in Release and full headless
ASan/UBSan. Index opening and application shutdown can still wait; see
[streaming evidence](verification-streaming.md).
Native asynchronous checkpoint writing now has a bounded single-transaction
`ScSaveIo`, immutable request data, nonblocking completion observation, retry
and shutdown draining. It reuses format 3 preflight and atomic commit; controlled
waiting, real index-write failure, exception conversion and frozen-payload retry
pass in Release and full headless ASan/UBSan. The host now owns this writer; Lua
write_async/write_chunks_async publish completion at the next fixed boundary while
UI/devices/network remain serviced. Status/retry/release enforce one transaction;
unreleased requests exclude other save calls, room changes and reloads. Real-file
failure/retry, immutable payloads, shutdown draining, cache invalidation and metadata
checks pass in Release and full headless ASan/UBSan; a hidden native recovery capture
was inspected. Stream-world and Wayfarer now stage outgoing snapshots and publish
only after asynchronous saving succeeds. Retry preserves the snapshot; cancelling
publication retains the old world without undoing an accepted disk write. UI recovery,
prior pause restoration and save-success/publication-failure are covered by five
focused Release and headless sanitizer checks. Wayfarer's dev.20 SDK and native
save-error/menu captures are inspected; the I/O footer leaves recovery controls
visible. The SDK versions and streamed reads have since advanced as described below.
Subsequent work converted Crossing checkpoints and Barrage result saves to the
application worker; GPU residency and other save workflows remain open.
The same service now also reads checkpoint summaries and selected chunks through
read_chunks_async/result, with a single worker/transaction and fixed-boundary
publication. It retains a pinned index or selects one complete valid snapshot,
omits missing keys, bounds selected results and exposes no partial result on failure.
Release pins a successful read index. Stream-world now uses read/prepare/write/publish
phases, keeping the old world while reading, preparing and saving. Preparation errors
are recoverable statuses; retries reuse loaded records, failed IO retries retain their
requests, and cancelling reads never starts outgoing writes. Initial loading also waits
for its read result. Wayfarer's SDK is dev.22; the other five annotated SDKs are dev.21.
Later work added `project.stream_indexes`: declared room indexes now parse on the
application content worker before `init`; undeclared indexes remain synchronous.
Wayfarer and Barrage title summaries now use asynchronous reads; explicit `load`
still reconstructs the selected scene on the main thread.
See [checkpoint evidence](verification-chunk-saves.md).
Text controls support grapheme-safe pointer drag selection, Shift extension,
outside release and cancellation when hidden/disabled. Headless replay covers
multiline hit testing and undo; native text/IME acceptance remains outstanding.
Lists now support selection/change/activation, page navigation, automatic reveal
and controller focus escape. A 10,000-item headless fixture checks visible-row
submission under a 32-command budget; this is not the full UI performance acceptance.
Grouped tabs associate stable panel IDs, validate ownership, switch visibility and
flush interaction-triggered layout changes before drawing. Headless replay covers
mouse/controller switching and invalid relationships; native pixels remain unverified.
Modal focus now enters/restores per visible dialog, supports nested/background closure
and rejects hidden/disabled restoration targets; same-tree activation does not fall through.
UI inspection now pages the authored tree, includes hidden/selected/disabled state,
returns detached values and avoids node metamethods. Tests cover 601 nodes, UTF-8
preview bounds and malformed parent chains. This is not the interactive debugger.

The combined barrage acceptance workload and repeatable runner now exist. The initial
native GTX 1060/i7-8700 short run **fails CPU p99**, despite sufficient live counts and
hits; [measured evidence](verification-barrage.md). Formal three-run acceptance remains pending.
Candidate deduplication, compact target caching, direct Lua hit output and linear
creation-order drawing have [2026-09-25 regression evidence](verification-projectile-performance.md);
The fixed cell-index cache passes a subsequent short diagnostic, but formal
three-run performance acceptance remains pending; short runs are not release evidence.

Dedicated particle columns, stable compaction, atomic burst capacity and native batching:
[contract](particles.md), [2026-09-24 verification](verification-particles.md).
Immutable emitter templates, speed/lifetime/angle ranges, size/RGBA curves and a
fixed-update Lua emitter module are now included in that verification.
PNG emitter regions and alpha/additive batching include native pixel assertions;
opaque target copies avoid applying transparency twice during presentation.

Projectile atlas regions, bounded registration and stable transparent batching:
[contract](projectile-atlas.md), [2026-09-24 verification](verification-projectile-atlas.md).
Project projectile budgets (default 32768; zero disables), lazy allocation and capacity
diagnostics are covered by the same verification record.

Initial broad baseline: [2026-09-18 development verification](verification-complete-dev.md).
Profiling work and current measurement limits: [profiling](profiling.md),
[2026-09-21 verification](verification-profiling.md).
Entity contract generation, boundary fixes and batch storage verification:
[2026-09-21 entity contracts](verification-contracts.md).
Object persistent IDs, room-local lifecycle and saved-reference reconstruction:
[identity contract](identity.md), [2026-09-21 verification](verification-identity.md).
Atomic checkpoint indices and bounded world records: [chunk saves](chunk-saves.md),
[2026-09-21 verification](verification-chunk-saves.md).
Nonblocking scheduled chunk publication and host simulation gates: [streaming](streaming.md),
[2026-09-22 verification](verification-streaming.md).
Named network maintenance during simulation waits, fixed-tick delivery and budgets:
[2026-09-23 verification](verification-network-sessions.md).
External bounded UDP fault injection and four-process reliable traffic:
[network testing](network-testing.md), [2026-09-23 verification](verification-network-faults.md).
Bounded Lua snapshot interpolation and native visual illustration:
[snapshot contract](snapshots.md), [2026-09-23 verification](verification-snapshots.md).
Application network clock and bounded token reservation/rejoin lifecycle:
[rejoin contract](rejoining.md), [2026-09-23 verification](verification-rejoining.md).
Session-owned protocol state separated from game checkpoints:
[2026-09-23 verification](verification-session-state.md).
OS-backed temporary token issuance and failure/budget isolation:
[2026-09-23 verification](verification-net-tokens.md).
Playable four-process host authority, two-room flow, 20 Hz snapshots, weak-network
rejoin and real-time expiry: [Constellation verification](verification-constellation.md).
Actual shape overlap, translational sweep and stable nearest-hit ordering:
[2026-09-24 verification](verification-physics-queries.md).
Joint limit/motor controls, distance anchors and cross-room handle isolation:
[2026-09-24 verification](verification-joints.md).
Tile passability invalidation and bounded shared-field reconstruction:
[flow refresh](navigation-refresh.md), [verification](verification-navigation-refresh.md).
Finite Tiled collision navigation, polygon rasterization and atomic topology changes:
[2026-09-24 verification](verification-tiled-navigation.md).
Explicit finite-map object collision now shares static terrain with physics,
navigation and projectiles. Rectangle/convex polygon transforms and layer offsets
survive tile edits; ordinary object factories remain opt-in. Two focused integration
tests plus the existing tile-edit regression pass in Release and ASan/UBSan; see the
2026-09-27 entry in the same verification record. Project API copies use SDK dev.8.
Finite tile edits now update an active custom navigation region atomically with the
room grid and geometry. The removed-wall regression and independent revision/failure
checks pass in Release and ASan/UBSan; authored region cells and flow handles remain
intact, and changes outside the region do not invalidate its field.
Batch projectile sweeps against finite Tiled terrain with cached spatial indexing:
[contract](projectile-terrain.md), [verification](verification-projectile-terrain.md).
Next implementation dependencies and acceptance gates: [continuation plan](continuation-plan.md).
Implemented subsets include application settings with rollback, native input and
IME plumbing, retained Lua UI/text editing, bounded projectile/navigation systems,
resource imports, chunk-file cache, save backups, shared audio and named network
sessions. Each large work package below still has unfinished requirements.

## Work packages

All seven save functions now have structured contracts and two generated record
types. Exact argument counts are enforced; backup-only slots appear once in sorted
listing, malformed frame/time metadata triggers backup recovery, and private index
or extra file fields are not exposed. Focused Release and ASan/UBSan checks pass;
all 13 sample API copies are synchronized, with six annotated SDKs at dev.14.
See [save contracts](saves.md); streamed-world async reads and writes are integrated.

UI navigation/editing now repeats held arrows, paging, deletion and gamepad D-pad
input after a 0.4-second delay, with bounded callbacks and focus/modal/IME reset
rules. Confirmation remains edge-triggered; native input snapshots are unchanged.
Three targeted replays and related regressions pass, and a hidden native slider
capture was inspected. UI sample SDK copies use dev.12; physical devices remain unverified.
Text controls now collapse selections without an extra grapheme step, preserve the
preferred column through short lines, support page selection and Ctrl+Home/End,
and accept Ctrl+Shift+Z for redo. Three focused replays and two related regressions
pass; a native CJK multiline selection capture was inspected. UI SDK copies are now
dev.13. Full IME clause attributes and fine-grained layout updates remain open.

The input API now has structured contracts for all 18 functions and six snapshot
record types, with strict argument/UTF-8 validation and explicit mutation phases.
Bindings are separated from the systems aggregation file. Focused Release/ASan
checks and generated-document consistency pass; see [input contracts](input.md).
Six annotated project SDKs are dev.10; snapshot retains its unchanged dev.1 bundle.

UI keyboard arrows and gamepad D-pad now navigate by control geometry, with
Tab/shoulders retaining authored order. Control-local directions, modal isolation,
scroll reveal and input consumption remain explicit. Focused replay checks and two
hidden native screenshots cover grid focus and slider entry; see [UI lifecycle](ui-lifecycle.md).
The four UI sample SDKs use dev.9; physical-controller acceptance remains pending.

- [ ] Runtime ownership, configurable pools, stale handles, atomic batch edits
- [ ] Input snapshots, four controllers, recording, Windows IME
- [ ] Declarative UI, text editing, settings, focus, virtual lists
- [ ] Projectile batches, navigation, physics queries and joints
- [ ] Resource pipeline, expanded Tiled import, streaming and persistence
- [ ] Animation, particles, cameras, materials, lighting and postprocessing
- [ ] Audio buses, shared resources and persistent music
- [ ] Save slots, backup recovery, application-owned network sessions
- [ ] Agent API contract, tracing, profiling, debugger, portable SDK
- [ ] Three playable samples and multiplayer fixture
- [ ] Five benchmark loads and original performance thresholds
- [ ] Sanitizers, native Windows evidence, desktop CI and portable packages
- [ ] Two-hour endurance and physical controller/IME/audio acceptance

## Constraints

No skeleton animation, old-version compatibility layer or migration guide.
No cloud services, general ECS, editor database or provider integration.
Native, performance and platform checks must report actual measured evidence.
