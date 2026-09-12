#ifndef SHINY_SCRIPT_H
#define SHINY_SCRIPT_H
#include "shiny/core.h"
#include <memory>

struct lua_State;

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
    char root[SC_PATH_MAX]{}, entry[SC_PATH_MAX]{}, pending_scene[SC_PATH_MAX]{};
    char error[SC_ERROR_MAX]{};
    int module_ref = -2; // LUA_NOREF, without requiring Lua headers in the public API.
    std::size_t memory_used = 0;
    int instruction_budget = 0;

private:
    struct LuaCloser { void operator()(lua_State *state) const noexcept; };
    // Declared last so callbacks can still access all accounting fields on close.
    std::unique_ptr<lua_State, LuaCloser> state_;
    friend bool sc_script_open(ScScript *, ScWorld *, const char *, const char *);
    friend void sc_script_close(ScScript *) noexcept;
};

/* Initializes VM, validates returned scene table, runs optional init(). */
bool sc_script_open(ScScript *script, ScWorld *world, const char *root, const char *entry);
void sc_script_close(ScScript *script) noexcept; // Optional early release; destructor handles normal lifetime.
bool sc_script_update(ScScript *script); /* invokes update(SC_DT), before sc_step */
bool sc_script_draw(ScScript *script, float alpha); /* clears draw queue; optional draw(alpha) */
bool sc_script_validate_path(const char *relative);
/* Emits the Lua API metadata as JSON to stdout. */
void sc_script_describe(void);
#endif
