#include "stablecops/config/MotorConfig.hpp"

#include <chrono>
#include <fstream>

#include <nlohmann/json.hpp>

#include "stablecops/log/Log.hpp"

namespace stablecops::config {

namespace {

using nlohmann::json;

// Replace `field` with the profile value only while it still holds its
// built-in default, so explicit caller/CLI overrides win.
template <typename T, typename U>
void applyIfDefault(T& field, const T& built_in_default, const json& runtime, const char* key) {
    if (field != built_in_default) {
        return;
    }
    const auto it = runtime.find(key);
    if (it != runtime.end() && it->is_number()) {
        field = static_cast<T>(it->get<U>());
    }
}

template <typename Rep, typename Period>
void applyIfDefaultMs(std::chrono::duration<Rep, Period>& field,
                      const std::chrono::duration<Rep, Period>& built_in_default,
                      const json& runtime, const char* key) {
    if (field != built_in_default) {
        return;
    }
    const auto it = runtime.find(key);
    if (it != runtime.end() && it->is_number()) {
        field = std::chrono::milliseconds{it->get<int64_t>()};
    }
}

void applyHoming(ds402::HomingConfig& homing, const json& runtime) {
    const auto it = runtime.find("homing");
    if (it == runtime.end() || !it->is_object()) {
        return;
    }
    const json& h = *it;
    const ds402::HomingConfig defaults{};

    applyIfDefault<int32_t, int32_t>(homing.search_velocity, defaults.search_velocity, h,
                                     "search_velocity");
    applyIfDefault<int32_t, int32_t>(homing.approach_velocity, defaults.approach_velocity, h,
                                     "approach_velocity");
    applyIfDefault<int32_t, int32_t>(homing.center_velocity, defaults.center_velocity, h,
                                     "center_velocity");
    applyIfDefault<int32_t, int32_t>(homing.center_final_velocity, defaults.center_final_velocity,
                                     h, "center_final_velocity");
    applyIfDefault<int32_t, int32_t>(homing.center_slowdown_distance,
                                     defaults.center_slowdown_distance, h,
                                     "center_slowdown_distance");
    applyIfDefault<int32_t, int32_t>(homing.backoff_distance, defaults.backoff_distance, h,
                                     "backoff_distance");
    applyIfDefault<int32_t, int32_t>(homing.center_tolerance, defaults.center_tolerance, h,
                                     "center_tolerance");
    applyIfDefault<int32_t, int32_t>(homing.center_settle_tolerance,
                                     defaults.center_settle_tolerance, h,
                                     "center_settle_tolerance");
    applyIfDefault<int32_t, int32_t>(homing.min_travel, defaults.min_travel, h, "min_travel");
    applyIfDefault<int32_t, int32_t>(homing.max_travel, defaults.max_travel, h, "max_travel");
    applyIfDefault<int32_t, int32_t>(homing.home_offset, defaults.home_offset, h, "home_offset");
    applyIfDefault<int16_t, int16_t>(homing.threshold_torque, defaults.threshold_torque, h,
                                     "threshold_torque");
    applyIfDefault<int32_t, int32_t>(homing.stopped_velocity, defaults.stopped_velocity, h,
                                     "stopped_velocity");
    applyIfDefaultMs(homing.contact_dwell, defaults.contact_dwell, h, "contact_dwell_ms");
    applyIfDefaultMs(homing.settle_time, defaults.settle_time, h, "settle_time_ms");
    applyIfDefaultMs(homing.timeout, defaults.timeout, h, "timeout_ms");

    if (homing.save_zero_to_nvm == defaults.save_zero_to_nvm) {
        const auto save = h.find("save_zero_to_nvm");
        if (save != h.end() && save->is_boolean()) {
            homing.save_zero_to_nvm = save->get<bool>();
        }
    }
}

}  // namespace

MotorConfig resolveMotorConfig(MotorConfig config) {
    std::ifstream stream(config.summary_path);
    if (!stream) {
        // Boot fails later (PDO map load) with a clear error if the path is
        // genuinely wrong; resolution itself is best-effort.
        return config;
    }

    json summary;
    try {
        stream >> summary;
    } catch (const json::exception& exception) {
        log::warn() << "profile resolution: failed to parse '" << config.summary_path
                    << "': " << exception.what() << '\n';
        return config;
    }

    const MotorConfig defaults{};

    // The SYNC period must match the generated DCF, so the summary always wins.
    const auto sync = summary.find("sync_period_us");
    if (sync != summary.end() && sync->is_number()) {
        const auto profile_sync = sync->get<uint32_t>();
        if (profile_sync != 0) {
            if (config.sync_period_us != defaults.sync_period_us &&
                config.sync_period_us != profile_sync) {
                log::warn() << "profile resolution: sync_period_us " << config.sync_period_us
                            << " overridden by the profile value " << profile_sync
                            << " (must match the generated DCF)\n";
            }
            config.sync_period_us = profile_sync;
        }
    }

    const auto runtime_it = summary.find("runtime");
    if (runtime_it == summary.end() || !runtime_it->is_object()) {
        return config;
    }
    const json& runtime = *runtime_it;

    applyIfDefault<uint32_t, uint32_t>(config.counts_per_rev, defaults.counts_per_rev, runtime,
                                       "counts_per_rev");
    applyIfDefault<int32_t, int32_t>(config.max_position_step, defaults.max_position_step, runtime,
                                     "max_position_step");
    applyIfDefaultMs(config.feedback_timeout, defaults.feedback_timeout, runtime,
                     "feedback_timeout_ms");
    applyIfDefaultMs(config.state_transition_timeout, defaults.state_transition_timeout, runtime,
                     "state_transition_timeout_ms");
    applyHoming(config.homing, runtime);

    return config;
}

}  // namespace stablecops::config
