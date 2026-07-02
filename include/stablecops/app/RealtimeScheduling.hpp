#pragma once

namespace stablecops::app {

// Opt-in real-time tuning for a bus loop thread. Defaults are inert (enabled is
// false) so a normal build keeps running at ordinary priority. When enabled the
// thread is moved to SCHED_FIFO, optionally pinned to one CPU, and the process
// memory is locked to avoid page faults on the cyclic path. All of this needs
// privileges (CAP_SYS_NICE / an rtprio ulimit); without them the helper logs a
// warning and the loop keeps running at normal priority.
struct RtConfig {
    bool enabled{false};
    // SCHED_FIFO priority (1..99). 80 leaves headroom above ordinary work while
    // staying below typical kernel threads.
    int priority{80};
    // CPU to pin the loop thread to; -1 leaves it unpinned. Pair with isolcpus
    // to keep other work off the chosen core for the best jitter.
    int cpu{-1};
    // Lock current + future pages (mlockall) to keep the cyclic path off the
    // pager. Recommended whenever enabled.
    bool lock_memory{true};
};

// Apply rt to the calling thread (intended to be the bus loop thread). Sets the
// thread name for debuggability regardless of rt.enabled. Best-effort: every
// step degrades gracefully with a single stderr warning on failure, so the
// caller can always proceed to run the loop.
void applyRealtimeScheduling(const RtConfig& rt, const char* thread_name);

}  // namespace stablecops::app
