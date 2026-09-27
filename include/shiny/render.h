#ifndef SHINY_RENDER_H
#define SHINY_RENDER_H
#include "shiny/core.h"
#include "shiny/settings.h"
#include "shiny/profile.h"
class ScScript;
#ifdef SC_HAS_STREAMING
class ScRoomImages;
ScResult<bool> sc_render_prepare_images(ScRoomImages&);
#endif

bool sc_render_open(const ScWorld *world, const char *root, bool audio, char *error, size_t error_size, bool hidden=false);
/* CPU-only asset preflight; does not create a window, audio device, or GPU context. */
bool sc_render_validate_assets(const ScWorld *world, const char *root, char *error, size_t error_size);
void sc_render_close(void);
void sc_render_loading(); // Initial loading screen; no room resources or simulation.
void sc_render_reload_assets(void);
/* Prepare GPU assets and silent audio voices before a scene swap; failure keeps active resources. */
bool sc_render_prepare_assets(const ScWorld*,char* error,size_t error_size,ScScript* script=nullptr);
const char *sc_render_error(void);
bool sc_render_should_close(void);
float sc_render_delta(void);
ScDeviceInput sc_render_sample_input();
void sc_render_input_consumed();
void sc_render_debug_keys(bool enabled);
#ifdef SC_HAS_DEVTOOLS
ScValue sc_render_inspector(const ScValue& request);
#endif
bool sc_render_reload_requested(void);
bool sc_render_pause_requested(void);
bool sc_render_step_requested(void);
// Enable only after opening a context. Unsupported timers return false.
bool sc_render_profile_enable();
const char* sc_render_device(); // Borrowed renderer description; nullptr without a context.
enum class ScRenderNoticeKind { reload_error, save_pending, save_error, read_pending, read_error,
    delete_pending, delete_error, stream_pending, stream_error, image_pending, image_error };
struct ScRenderNotice {
    ScRenderNoticeKind kind{ScRenderNoticeKind::reload_error};
    const char* detail{""}; // Borrowed for this frame; full I/O diagnostics remain in stderr/API.
};
// Optional capture reads this submitted frame before the display buffers are swapped.
void sc_render_frame(ScWorld *world, float alpha, ScRenderNotice notice, bool paused, ScRenderProfile* profile=nullptr,ScScript* script=nullptr,const char* capture=nullptr);
void sc_render_audio(const ScScript* script);
ScResult<void> sc_render_settings(const ScSettings&);
#endif
