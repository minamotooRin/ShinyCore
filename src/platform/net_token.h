#pragma once
#include "shiny/net.h"

// No simulation RNG, process-global seed or weak fallback.
ScResult<ScNetToken> sc_platform_net_token();
