#pragma once

#include "stablecops/config/MotorConfig.hpp"

namespace stablecops::app {

// The tuning knobs are defined next to MotorConfig (stablecops::config) so the
// whole drive configuration lives in one struct; this alias keeps the
// historical stablecops::app spelling working.
using RtConfig = config::RtConfig;

// Apply rt to the calling thread (intended to be the bus loop thread). Sets the
// thread name for debuggability regardless of rt.enabled. Best-effort: every
// step degrades gracefully with a single warning on failure, so the caller can
// always proceed to run the loop.
void applyRealtimeScheduling(const RtConfig& rt, const char* thread_name);

}  // namespace stablecops::app
