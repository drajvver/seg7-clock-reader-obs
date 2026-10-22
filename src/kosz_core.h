#pragma once

#include "clock_core.h"

namespace kosz {

// Decodes a basketball scoreboard clock (e.g. "20:09.1", "9:59", "59.9").
// Reuses seg7::Reading so the result can be fed to seg7::ClockTracker.
seg7::Reading decode(const uint8_t *gray, int width, int height, int stride);

}
