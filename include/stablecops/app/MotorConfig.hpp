#pragma once

// MotorConfig lives in stablecops::config (see that header for the full field
// documentation) so the Lely driver and the public app API share one struct.
// This forwarder keeps the historical stablecops::app spelling working.

#include "stablecops/app/RealtimeScheduling.hpp"
#include "stablecops/config/MotorConfig.hpp"

namespace stablecops::app {

using MotorConfig = config::MotorConfig;

}  // namespace stablecops::app
