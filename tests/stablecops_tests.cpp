// Unit tests for the pure (bus-free) parts of stableCOPS: DS402 statusword
// decoding, diagnostic decoding, and the PDO-summary loader. Framework-free:
// each CHECK reports and fails the process on mismatch, so the binary doubles
// as a quick smoke test (`ctest` or run it directly).

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <utility>

#include "stablecops/config/MotorConfig.hpp"
#include "stablecops/config/PdoMap.hpp"
#include "stablecops/ds402/Diagnostics.hpp"
#include "stablecops/ds402/State.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                              \
    do {                                                                              \
        if (!(condition)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                               \
        }                                                                             \
    } while (0)

void testStateDecode() {
    using stablecops::ds402::decodeState;
    using stablecops::ds402::State;

    // Canonical CiA402 statusword patterns (low bits only).
    CHECK(decodeState(0x0000) == State::NotReadyToSwitchOn);
    CHECK(decodeState(0x0040) == State::SwitchOnDisabled);
    CHECK(decodeState(0x0021) == State::ReadyToSwitchOn);
    CHECK(decodeState(0x0023) == State::SwitchedOn);
    CHECK(decodeState(0x0027) == State::OperationEnabled);
    CHECK(decodeState(0x0007) == State::QuickStopActive);
    CHECK(decodeState(0x000F) == State::FaultReactionActive);
    CHECK(decodeState(0x0008) == State::Fault);

    // Live values with the usual upper bits set (voltage enabled, remote,
    // zero-speed) must decode identically.
    CHECK(decodeState(0x0631) == State::ReadyToSwitchOn);
    CHECK(decodeState(0x0633) == State::SwitchedOn);
    CHECK(decodeState(0x0637) == State::OperationEnabled);
    CHECK(decodeState(0x0608) == State::Fault);

    CHECK(stablecops::ds402::isFault(0x0008));
    CHECK(stablecops::ds402::isFault(0x000F));
    CHECK(!stablecops::ds402::isFault(0x0637));
    CHECK(stablecops::ds402::isOperationEnabled(0x0637));
    CHECK(!stablecops::ds402::isOperationEnabled(0x0633));
}

void testDiagnostics() {
    using stablecops::ds402::describeAbortCode;
    using stablecops::ds402::describeDeviceFault;
    using stablecops::ds402::describeErrorRegister;

    CHECK(describeErrorRegister(0x00) == "none");
    CHECK(describeErrorRegister(0x01) == "generic");
    CHECK(describeErrorRegister(0x0A) == "current, temperature");

    CHECK(describeDeviceFault(0) == "no fault");
    CHECK(describeDeviceFault(0x4210) == "Motor Over-Temperature [Drive Fault]");
    CHECK(describeDeviceFault(0x7510) ==
          "Primary Encoder Communication Loss [Motor/Encoder Fault]");
    CHECK(describeDeviceFault(0xFF33) ==
          "PDO Configuration Exceeds Quantity Limit [Application Fault]");
    CHECK(describeDeviceFault(0x1234).find("unknown device fault") != std::string::npos);

    CHECK(describeAbortCode(0x05040000) == "SDO protocol timeout");
    CHECK(describeAbortCode(0x06010004) == "object is not SDO-accessible (PDO mapped only)");
    CHECK(describeAbortCode(0xDEADBEEF).find("unknown abort code") != std::string::npos);
}

std::string writeTempSummary(const char* name, const std::string& contents) {
    std::string path = std::string("stablecops_test_") + name + ".json";
    std::ofstream file(path);
    file << contents;
    return path;
}

void testPdoMapNodeRelative() {
    // Modern summary: node-relative COB-ID metadata present.
    const auto path = writeTempSummary("node_relative", R"({
        "node_id": 1,
        "node_ids": [1, 2],
        "pdo_mappings": {
            "rpdo": [{
                "pdo": 1,
                "communication_index": "0x1400",
                "mapping_index": "0x1600",
                "cob_id": 513,
                "transmission_type": 1,
                "cob_id_node_relative": true,
                "cob_id_offset": 512,
                "entries": [
                    {"index": "0x6040", "subindex": 0, "bit_length": 16, "name": "Controlword"}
                ]
            }],
            "tpdo": [{
                "pdo": 1,
                "communication_index": "0x1800",
                "mapping_index": "0x1A00",
                "cob_id": 1073742209,
                "transmission_type": 1,
                "cob_id_node_relative": true,
                "cob_id_offset": 1073742208,
                "entries": [
                    {"index": "0x6041", "subindex": 0, "bit_length": 16, "name": "Statusword"}
                ]
            }]
        }
    })");

    const auto map = stablecops::config::loadPdoMapFromSummary(path, 2);
    CHECK(map.rpdo.size() == 1);
    CHECK(map.tpdo.size() == 1);
    CHECK(map.rpdo[0].comm_index == 0x1400);
    CHECK(map.rpdo[0].map_index == 0x1600);
    CHECK(map.rpdo[0].cob_id == 0x202);  // 0x200 offset + node 2
    CHECK(map.rpdo[0].active());
    CHECK(map.rpdo[0].baseCobId() == 0x202);
    CHECK(map.rpdo[0].entries.size() == 1);
    CHECK(map.rpdo[0].entries[0].index == 0x6040);
    CHECK(map.rpdo[0].entries[0].bit_length == 16);
    // TPDO COB-ID keeps its flag bits (bit 30) while the 11-bit id is rebased.
    CHECK(map.tpdo[0].cob_id == (0x40000000u | 0x182u));
    CHECK(map.tpdo[0].active());

    const auto node_ids = stablecops::config::loadNodeIdsFromSummary(path);
    CHECK(node_ids.size() == 2);
    CHECK(node_ids[0] == 1);
    CHECK(node_ids[1] == 2);

    std::remove(path.c_str());
}

void testPdoMapLegacyRebase() {
    // Older summary without metadata: the representative node's COB-IDs must be
    // shifted to the requested node id.
    const auto path = writeTempSummary("legacy", R"({
        "node_id": 1,
        "pdo_mappings": {
            "rpdo": [{
                "pdo": 1,
                "communication_index": "0x1400",
                "mapping_index": "0x1600",
                "cob_id": 513,
                "transmission_type": 1,
                "entries": []
            }],
            "tpdo": []
        }
    })");

    const auto map = stablecops::config::loadPdoMapFromSummary(path, 3);
    CHECK(map.rpdo.size() == 1);
    CHECK(map.rpdo[0].cob_id == 0x203);

    // Disabled channel: valid bit (31) set -> inactive.
    stablecops::config::PdoChannel disabled;
    disabled.cob_id = 0x80000400u;
    CHECK(!disabled.active());
    CHECK(disabled.baseCobId() == 0x400);

    std::remove(path.c_str());
}

void testPdoMapErrors() {
    bool threw = false;
    try {
        stablecops::config::loadPdoMapFromSummary("/nonexistent/summary.json");
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);

    const auto path = writeTempSummary("no_mappings", R"({"node_id": 1})");
    threw = false;
    try {
        stablecops::config::loadPdoMapFromSummary(path);
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
    std::remove(path.c_str());
}

void testMotorConfigResolution() {
    using stablecops::config::MotorConfig;
    using stablecops::config::resolveMotorConfig;

    const auto path = writeTempSummary("runtime_profile", R"({
        "node_id": 1,
        "sync_period_us": 4000,
        "runtime": {
            "counts_per_rev": 262144,
            "feedback_timeout_ms": 50,
            "state_transition_timeout_ms": 3000,
            "max_position_step": 500,
            "homing": {
                "search_velocity": 12345,
                "threshold_torque": 42,
                "timeout_ms": 60000,
                "save_zero_to_nvm": false
            }
        }
    })");

    // Fields left at their built-in defaults follow the profile.
    MotorConfig config;
    config.summary_path = path;
    config = resolveMotorConfig(std::move(config));
    CHECK(config.sync_period_us == 4000);
    CHECK(config.counts_per_rev == 262144);
    CHECK(config.feedback_timeout == std::chrono::milliseconds{50});
    CHECK(config.state_transition_timeout == std::chrono::milliseconds{3000});
    CHECK(config.max_position_step == 500);
    CHECK(config.homing.search_velocity == 12345);
    CHECK(config.homing.threshold_torque == 42);
    CHECK(config.homing.timeout == std::chrono::milliseconds{60000});
    CHECK(!config.homing.save_zero_to_nvm);
    // Homing fields the profile does not name keep their built-in defaults.
    CHECK(config.homing.backoff_distance == stablecops::ds402::HomingConfig{}.backoff_distance);

    // Resolution is idempotent.
    const auto again = resolveMotorConfig(config);
    CHECK(again.counts_per_rev == config.counts_per_rev);
    CHECK(again.homing.search_velocity == config.homing.search_velocity);

    // Explicit (non-default) values win over the profile...
    MotorConfig overridden;
    overridden.summary_path = path;
    overridden.counts_per_rev = 1000;
    overridden.feedback_timeout = std::chrono::milliseconds{7};
    overridden.homing.search_velocity = 777;
    overridden.sync_period_us = 999;  // ...except sync, which must match the DCF.
    overridden = resolveMotorConfig(std::move(overridden));
    CHECK(overridden.counts_per_rev == 1000);
    CHECK(overridden.feedback_timeout == std::chrono::milliseconds{7});
    CHECK(overridden.homing.search_velocity == 777);
    CHECK(overridden.sync_period_us == 4000);

    std::remove(path.c_str());

    // A missing summary leaves the config untouched.
    MotorConfig missing;
    missing.summary_path = "/nonexistent/summary.json";
    missing = resolveMotorConfig(std::move(missing));
    CHECK(missing.counts_per_rev == MotorConfig{}.counts_per_rev);
    CHECK(missing.sync_period_us == MotorConfig{}.sync_period_us);
}

}  // namespace

int main() {
    testStateDecode();
    testDiagnostics();
    testPdoMapNodeRelative();
    testPdoMapLegacyRebase();
    testPdoMapErrors();
    testMotorConfigResolution();

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("all checks passed\n");
    return EXIT_SUCCESS;
}
