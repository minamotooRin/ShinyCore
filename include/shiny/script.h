#ifndef SHINY_SCRIPT_H
#define SHINY_SCRIPT_H
#include "shiny/core.h"
#include "shiny/state.h"
#include "shiny/audio.h"
#include "shiny/navigation.h"
#include "shiny/projectiles.h"
#include <memory>
#include <bitset>
#ifdef SC_HAS_ADVANCED_RENDER
#include "shiny/material.h"
#include "shiny/lighting.h"
#endif
#ifdef SC_HAS_STREAMING
#include "shiny/stream.h"
#include "shiny/room_images.h"
#endif

struct lua_State;
struct lua_Debug;
struct ScNetSessions;
class ScSettingsService;
class ScContentLoader;
class ScImageCache;
class ScSaveIo;

// Lua's allocator and extraspace borrow this object's address, so a live script
// cannot move. The host owns ScWorld and keeps it alive until this VM is closed.
class ScScript final {
public:
    ScScript() noexcept = default;
    ~ScScript() noexcept;
    ScScript(const ScScript &) = delete;
    ScScript &operator=(const ScScript &) = delete;
    ScScript(ScScript &&) = delete;
    ScScript &operator=(ScScript &&) = delete;

    lua_State *lua = nullptr; // Borrowed C API observer. Never assign or lua_close it.
    ScWorld *world = nullptr;
    ScAudioState audio_draft; // Standalone scripts and candidate rooms own their isolated mixer.
    ScAudioState* audio = &audio_draft; // Host rebinds to application storage only after commit.
    ScNetSessions* network_sessions = nullptr; // Application outlives every room VM.
    ScSettingsService* settings = nullptr;
    ScSaveIo* save_io = nullptr; // Borrowed application transaction; no VM data on worker.
    bool candidate = false;
    char root[SC_PATH_MAX]{}, entry[SC_PATH_MAX]{}, pending_scene[SC_PATH_MAX]{};
    char error[SC_ERROR_MAX]{};
    int module_ref = -2; // LUA_NOREF, without requiring Lua headers in the public API.
    std::size_t memory_used = 0;
    std::size_t projectile_limit = 32768; // Project budget; storage is lazy until init/configure.
    int instruction_budget = 0;
#ifdef SC_HAS_DEVTOOLS
    bool (*debug_hook)(void*,ScScript&,lua_State*,lua_Debug*) noexcept = nullptr;
    void* debug_context{};
#endif
    int phase = 0; // 0 load/init, 1 update, 2 draw, 3 migration, 4 UI update.
    bool has_ui_update{};
    bool initialized{}; // Host completes init once, after any declared index preparation.
    float draw_alpha{1};
    ScValue state{ScValue::Object{}}, pending_state{ScValue::Object{}}, project{ScValue::Object{}};
    ScValue scratch, preload_images; // Declarative initial image names, consumed after init.
#ifdef SC_HAS_STREAMING
    ScContentLoader* content_loader=nullptr; // Application outlives room and candidate.
    ScImageCache* image_cache=nullptr;
    std::unique_ptr<ScRoomImages> images;
    std::uint64_t initial_images{}; // Host-owned preparation; never exposed as a Lua request.
    std::unique_ptr<ScStream> stream;
    std::string preloaded_index_path; // Declared room index, supplied before init by the host.
    ScValue preloaded_index;
#endif
    ScValue::Object watches;
#ifdef SC_HAS_ADVANCED_RENDER
    std::unique_ptr<ScMaterials> materials;
    ScLighting lighting;
#endif
    std::vector<ScEntity> batch_entities;
    std::vector<std::size_t> batch_parents;
    std::vector<ScProjectileSpec> projectile_batch;
    std::bitset<65536> batch_seen; // One bit per possible handle slot; no frame allocation.
    std::unique_ptr<ScNavigationRegion> navigation_region;
    std::array<std::unique_ptr<ScFlowField>,16> flow_fields;
    std::array<std::uint32_t,16> flow_generations{};
    ScValue objects{ScValue::Array{}};
    bool has_pending_state = false, checking = false, warned_missing_glyph = false;
    std::string save_directory;
    std::map<std::string, ScValue> memory_saves;
    ScValue save_snapshot; // One pinned disk snapshot; chunk reads never mix generations.
    std::string save_snapshot_slot;
    std::string save_request_slot; // Name owned before async submission; used when a read is released.

private:
    struct LuaCloser { void operator()(lua_State *state) const noexcept; };
    // Declared last so callbacks can still access all accounting fields on close.
    std::unique_ptr<lua_State, LuaCloser> state_;
    friend bool sc_script_open(ScScript *, ScWorld *, const char *, const char *);
    friend bool sc_script_load(ScScript *, ScWorld *, const char *, const char *);
    friend void sc_script_close(ScScript *) noexcept;
};

/* Initializes VM, validates returned scene table, runs optional init(). */
bool sc_script_open(ScScript *script, ScWorld *world, const char *root, const char *entry);
bool sc_script_load(ScScript *script, ScWorld *world, const char *root, const char *entry); // Stops before init.
bool sc_script_initialize(ScScript *script); // Runs init and prepares eager images once.
void sc_script_close(ScScript *script) noexcept; // Optional early release; destructor handles normal lifetime.
bool sc_script_update(ScScript *script); /* invokes update(SC_DT), before sc_step */
// Host UI input is borrowed for this call; fixed simulation input is restored on error too.
bool sc_script_ui_update(ScScript*,float dt,const ScDeviceInput&);
bool sc_script_draw(ScScript *script, float alpha); /* clears draw queue; optional draw(alpha) */
bool sc_script_validate_path(const char *relative);
/* Emits the Lua API metadata as JSON to stdout. */
void sc_script_describe(void);
#endif
