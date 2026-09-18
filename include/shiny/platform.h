#pragma once
#include "shiny/input.h"
void sc_platform_open(void* window);
void sc_platform_close() noexcept;
void sc_platform_input(ScDeviceInput& input);
void sc_platform_text_position(int x,int y,bool enabled);
