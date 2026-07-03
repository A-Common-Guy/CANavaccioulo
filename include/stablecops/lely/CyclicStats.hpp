#pragma once

#include <cstdint>

namespace stablecops::lely {

// Measured cadence of the cyclic SYNC, accumulated on the loop thread and
// published for other threads. Intervals are between consecutive OnSync calls;
// jitter is the worst absolute deviation from the nominal period. All times in
// microseconds. cycles is the number of measured intervals.
//
// Deliberately a plain POD with no Lely dependency so it can appear in the
// public MotorDrive API without pulling the Lely headers into a consumer's
// build.
struct CyclicStats {
    uint64_t cycles{0};
    double last_us{0.0};
    double min_us{0.0};
    double max_us{0.0};
    double mean_us{0.0};
    double max_jitter_us{0.0};
};

}  // namespace stablecops::lely
