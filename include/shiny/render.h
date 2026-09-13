#ifndef SHINY_RENDER_H
#define SHINY_RENDER_H
#include "shiny/core.h"

bool sc_render_open(const ScWorld *world, const char *root, bool audio, char *error, size_t error_size);
/* CPU-only asset preflight; does not create a window, audio device, or GPU context. */
bool sc_render_validate_assets(const ScWorld *world, const char *root, char *error, size_t error_size);
void sc_render_close(void);
void sc_render_reload_assets(void);
/* Prepare GPU assets before a scene swap; failure keeps the active cache. */
bool sc_render_prepare_assets(const ScWorld*,char* error,size_t error_size);
const char *sc_render_error(void);
bool sc_render_should_close(void);
float sc_render_delta(void);
uint32_t sc_render_input(void);
uint32_t sc_render_pressed_input(void);
bool sc_render_reload_requested(void);
bool sc_render_pause_requested(void);
bool sc_render_step_requested(void);
void sc_render_frame(ScWorld *world, float alpha, const char *error, bool paused);
void sc_render_audio(const ScWorld *world);
bool sc_render_capture(const char *path);
#endif
