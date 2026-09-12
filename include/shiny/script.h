#ifndef SHINY_SCRIPT_H
#define SHINY_SCRIPT_H
#include "shiny/core.h"

typedef struct lua_State lua_State;
typedef struct {
    lua_State *lua;
    ScWorld *world;
    char root[SC_PATH_MAX], entry[SC_PATH_MAX], pending_scene[SC_PATH_MAX];
    char error[SC_ERROR_MAX];
    int module_ref;
    size_t memory_used;
    int instruction_budget;
} ScScript;

/* Initializes VM, validates returned scene table, runs optional init(). */
bool sc_script_open(ScScript *script, ScWorld *world, const char *root, const char *entry);
void sc_script_close(ScScript *script);
bool sc_script_update(ScScript *script); /* invokes update(SC_DT), before sc_step */
bool sc_script_draw(ScScript *script, float alpha); /* clears draw queue; optional draw(alpha) */
bool sc_script_validate_path(const char *relative);
/* Emits the Lua API metadata as JSON to stdout. */
void sc_script_describe(void);
#endif
