// motion_faster_can_move -- read EtherCAT encoders (via the motion-faster
// library) while moving a CAN motor (via stableCOPS MotorDrive), each on its own
// real-time / loop thread.
//
// The EtherCAT master runs in scan mode by default (auto-detects the drives on
// the NIC); the CAN drive is enabled in Cyclic Synchronous Velocity and streamed
// a constant target velocity (default 0 = hold, so nothing moves unless you pass
// --velocity). Every 100 ms it prints each EtherCAT drive's auxiliary (load)
// encoder alongside the CAN drive's position/velocity/state.
//
// SAFETY: a nonzero --velocity SPINS the CAN motor. Keep the joint clear and be
// ready to cut power. Ctrl-C (handled by the EtherCAT runtime) ends the loop and
// the CAN drive is de-energised on exit.
//
// Usage:
//   sudo ./canup.sh
//   sudo build/examples/motion_faster_can_move \
//       --ecat enp0s31f6 --can can0 --node 1 --velocity 0 --seconds 30
//
// Build with -DSTABLECOPS_BUILD_ECAT=ON (see CMakeLists.txt).

#include "ecat/master_runtime.hpp"
#include "ecat/pdo_handles.hpp"

#include "stablecops/app/MotorConfig.hpp"
#include "stablecops/app/MotorDrive.hpp"
#include "stablecops/ds402/State.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace {

void printUsage(const char* argv0) {
    std::fprintf(stderr,
                 "usage: %s --ecat <iface> [--ecat-config <yaml>] "
                 "[--can can0] [--node 1] [--master-node 127] [--dcf path] "
                 "[--summary path] [--velocity 0] [--seconds 30]\n",
                 argv0);
}

}  // namespace

int main(int argc, char** argv) {
    std::string ecat_iface;
    std::string ecat_config;  // empty => scan mode
    stablecops::app::MotorConfig can_config;
    can_config.can_interface = "can0";
    can_config.node_id = 1;
    can_config.operation_mode = stablecops::ds402::OperationMode::CyclicSynchronousVelocity;
    can_config.enable_on_boot = true;
    int32_t velocity = 0;
    double seconds = 30.0;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", name);
                std::exit(EXIT_FAILURE);
            }
            return argv[++i];
        };
        if (arg == "--ecat") {
            ecat_iface = next("--ecat");
        } else if (arg == "--ecat-config") {
            ecat_config = next("--ecat-config");
        } else if (arg == "--can") {
            can_config.can_interface = next("--can");
        } else if (arg == "--node") {
            can_config.node_id = static_cast<uint8_t>(std::stoi(next("--node")));
        } else if (arg == "--master-node") {
            can_config.master_node_id = static_cast<uint8_t>(std::stoi(next("--master-node")));
        } else if (arg == "--dcf") {
            can_config.master_dcf_path = next("--dcf");
        } else if (arg == "--summary") {
            can_config.summary_path = next("--summary");
        } else if (arg == "--velocity") {
            velocity = static_cast<int32_t>(std::stol(next("--velocity")));
        } else if (arg == "--seconds") {
            seconds = std::stod(next("--seconds"));
        } else {
            printUsage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (ecat_iface.empty()) {
        printUsage(argv[0]);
        return EXIT_FAILURE;
    }

    std::printf("CAN node %d on %s in CSV; EtherCAT on %s (%s). velocity=%d for %.1f s\n",
                static_cast<int>(can_config.node_id), can_config.can_interface.c_str(),
                ecat_iface.c_str(), ecat_config.empty() ? "scan mode" : ecat_config.c_str(),
                velocity, seconds);

    // Bring up the CAN drive first (enabled + streaming CSV), then the EtherCAT
    // master. Each owns its own thread; the main thread just orchestrates.
    stablecops::app::MotorDrive drive(can_config);
    try {
        drive.start();
    } catch (const std::exception& exception) {
        std::fprintf(stderr, "CAN start failed: %s\n", exception.what());
        return EXIT_FAILURE;
    }

    MasterRuntime runtime(ecat_iface, ecat_config);
    if (!runtime.start()) {
        std::fprintf(stderr, "EtherCAT start failed: %s\n", runtime.lastError().c_str());
        drive.stop();
        return EXIT_FAILURE;
    }

    auto& io = runtime.io();
    const std::size_t n = io.driveCount();
    std::printf("[motion_faster_can_move] %zu EtherCAT drive(s) on %s\n", n, ecat_iface.c_str());
    for (std::size_t i = 0; i < n; ++i) {
        if (!io.hasDriveTxField(i, 0x2033)) {
            const auto& d = io.info(i);
            std::fprintf(stderr,
                         "[warn] EtherCAT drive %zu '%s' (%s) has no aux feedback (0x2033); "
                         "value will read as 0\n",
                         i, d.name.c_str(), d.model.c_str());
        }
    }

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<long>(seconds * 1000));
    double last_print = -1.0;

    while (runtime.ok() && std::chrono::steady_clock::now() < deadline) {
        // Stream the CAN velocity setpoint (0 = hold). Only takes effect while
        // the drive is enabled in CSV; safe to call every iteration.
        drive.commandVelocity(velocity);

        const double t = runtime.elapsedSec();
        if (t - last_print >= 0.1) {
            std::printf("[%6.1fs]", t);
            for (std::size_t i = 0; i < n; ++i) {
                const auto& d = io.info(i);
                const uint32_t aux = io.get(i, pdo::auxFeedback);
                const double load_rad = io.feedback(i).loadPosition;
                std::printf("  ecat %s(%s): aux=%u load=%.4f rad", d.name.c_str(),
                            d.model.c_str(), aux, load_rad);
            }
            const auto fb = drive.feedback();
            std::printf("  | can node %d: %s pos=%d (%.2f deg) vel=%d%s\n",
                        static_cast<int>(can_config.node_id),
                        stablecops::ds402::toString(fb.state).c_str(), fb.position,
                        drive.positionDegrees(), fb.velocity,
                        drive.faulted() ? " [FAULT]" : "");
            last_print = t;
        }

        if (!drive.feedbackLive()) {
            std::fprintf(stderr, "CAN feedback went stale; stopping\n");
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    drive.stop();
    std::printf("done\n");
    return EXIT_SUCCESS;
}
