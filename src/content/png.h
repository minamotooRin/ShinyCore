#pragma once
#include "shiny/content_loader.h"

// CPU only; fixed PNG parser/input/workspace bounds, independent of raylib.
ScResult<ScImagePixels> sc_read_png(const ScImageRequest&);
