#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "stablecops/ds402/DriveController.hpp"
#include "stablecops/ds402/ObjectAccess.hpp"
#include "stablecops/ds402/State.hpp"

namespace stablecops::config {

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

// MotorConfig describes one drive. It is both the public configuration handed
// to app::MotorDrive / app::CanopenApplication and the per-drive settings the
// Lely driver consumes, so there is exactly one struct to keep in sync.
//
// Several drives that name the same can_interface share a single bus (one
// master, one loop thread, one SYNC), so the fields split in two groups:
//   - Bus-level (can_interface, master_dcf_path, summary_path, master_node_id,
//     sync_period_us, rt) must match across all drives on that interface; the
//     first drive to register defines the bus and mismatching siblings are
//     rejected.
//   - Per-drive (node_id, boot behaviour, mode, profile params, targets,
//     counts_per_rev, feedback_timeout, homing) apply only to that node.
//
// Profile-sourced defaults: resolveMotorConfig() (applied automatically when a
// MotorDrive or CanopenApplication is constructed) fills the fields marked
// [profile] below from the runtime section the generator records in
// summary.json, so the motor profile YAML stays the single source of truth.
struct MotorConfig {
    std::string can_interface{"can0"};
    std::string master_dcf_path{"dcf/master.dcf"};
    // Generated PDO layout + runtime-profile summary; the single source of
    // truth for which objects ride in each RxPDO/TxPDO and for the [profile]
    // defaults below. Generated alongside master.dcf from the motor profile,
    // so both ends of the bus stay coherent.
    std::string summary_path{"generated/canopen/euservo_rp/euservo_rp.summary.json"};
    uint8_t master_node_id{127};
    uint8_t node_id{1};
    bool inspect_on_boot{false};
    bool enable_on_boot{false};
    bool hold_position_on_boot{false};
    // Configure the drive's PDOs for cyclic transfer and stream SYNC without
    // enabling the power stage, so feedback can be observed with the joint safe.
    bool monitor_on_boot{false};
    // Operation mode (0x6060) to select over SDO while the node is
    // pre-operational at boot. One fixed PDO layout serves CSP/CSV/CST, so only
    // this object changes between cyclic modes; unset leaves the drive's
    // persisted mode in place.
    std::optional<ds402::OperationMode> operation_mode;

    std::optional<uint32_t> profile_velocity;
    std::optional<uint32_t> profile_acceleration;
    std::optional<uint32_t> profile_deceleration;
    std::optional<uint32_t> torque_slope;
    // Vendor "Disable Mode" (0x2103) written over SDO at boot when set; selects
    // coast/high-impedance vs short-circuit dynamic braking on disable.
    std::optional<uint8_t> disable_mode;
    // Ad-hoc raw object writes applied over SDO at boot, in order.
    std::vector<ds402::ObjectWrite> object_writes;
    // Persist boot-time parameter writes to NVM (0x1010:03).
    bool save_params{false};
    std::optional<int32_t> csp_target_position;
    std::optional<int32_t> csp_relative_move;
    // [profile] Guard for boot-time CSP targets, in counts.
    int32_t max_position_step{10000};
    // [profile] Output-shaft counts per revolution of 0x6064.
    uint32_t counts_per_rev{524288};
    std::chrono::milliseconds boot_timeout{5000};
    // [profile] Timeout for each DS402 state transition.
    std::chrono::milliseconds state_transition_timeout{2000};
    // [profile] Feedback-staleness watchdog window; 0 disables it.
    std::chrono::milliseconds feedback_timeout{100};

    // Bus-level. Nominal SYNC period used as the jitter-telemetry reference.
    // Always overwritten from the summary when it records one, because it must
    // match the generated DCF's SYNC period.
    uint32_t sync_period_us{2000};
    // Bus-level. Opt-in real-time tuning of the bus loop thread (off by
    // default). Shared by all drives on the interface.
    RtConfig rt;

    // [profile] Homing defaults for this actuator (velocities, contact
    // thresholds, travel limits). Callers of startHoming() typically start from
    // this and override per-application fields such as home_offset.
    ds402::HomingConfig homing;
};

// Fill `config` from the runtime profile recorded in its summary.json:
//   - sync_period_us always follows the summary (it must match the DCF);
//   - every other [profile] field (counts_per_rev, feedback_timeout,
//     state_transition_timeout, max_position_step, homing.*) is replaced only
//     while it still holds its built-in default, so an explicit value set by
//     the caller/CLI wins over the profile.
// A missing/unreadable summary or a summary without runtime data leaves the
// config unchanged (the hard failure, if any, happens when the PDO map is
// loaded at boot). Idempotent, so applying it more than once is harmless.
MotorConfig resolveMotorConfig(MotorConfig config);

}  // namespace stablecops::config
