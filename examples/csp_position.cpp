// Simple example: enable the drive in Cyclic Synchronous Position (CSP) and hold
// the position captured at enable, optionally offset by --offset counts. The
// commanded position is streamed every cycle; the live feedback is printed. On
// exit the drive is de-energised by the graceful stop.
//
// SAFETY: this moves the motor to (start + offset). Default --offset is 0 (hold
// the current position). Keep the joint clear and be ready to cut power.
//
// Run:
//   sudo ./canup.sh
//   build/examples/csp_position --can can0 --node 1 --offset 0 --seconds 5

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <cmath>

#include "stablecops/app/MotorConfig.hpp"
#include "stablecops/app/MotorDrive.hpp"
#include "stablecops/ds402/State.hpp"

int main(int argc, char** argv) {
    stablecops::app::MotorConfig config;
    config.enable_on_boot = true;
    config.operation_mode = stablecops::ds402::OperationMode::CyclicSynchronousPosition;
    int32_t amplitude = 0;  // counts to add to the position captured at enable
    double seconds = 100.0;
    double period = 1.0;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--can" && i + 1 < argc) {
            config.can_interface = argv[++i];
        } else if (arg == "--dcf" && i + 1 < argc) {
            config.master_dcf_path = argv[++i];
        } else if (arg == "--summary" && i + 1 < argc) {
            config.summary_path = argv[++i];
        } else if (arg == "--master-node" && i + 1 < argc) {
            config.master_node_id = static_cast<uint8_t>(std::stoi(argv[++i]));
        } else if (arg == "--node" && i + 1 < argc) {
            config.node_id = static_cast<uint8_t>(std::stoi(argv[++i]));
        } else if (arg == "--amplitude" && i + 1 < argc) {
            amplitude = std::stoi(argv[++i]);
        } else if (arg == "--seconds" && i + 1 < argc) {
            seconds = std::stod(argv[++i]);
        } else if (arg == "--period" && i + 1 < argc) {
            period = std::stod(argv[++i]);
        } else {
            std::cerr << "usage: csp_position [--can can0] [--dcf path] [--summary path] "
                         "[--master-node 127] [--node 1] [--amplitude 0] [--seconds 5] [--period 1]\n";
            return EXIT_FAILURE;
        }
    }

    std::cout << "CSP: hold (start + " << amplitude << " * sin(2 * M_PI * t / " << period << ")) for " << seconds << " s\n";

    stablecops::app::MotorDrive drive(config);
    drive.start();

    // Wait for the first cyclic feedback so we can read the position at enable.
    const auto boot_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!drive.feedbackLive() &&
           std::chrono::steady_clock::now() < boot_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    const int32_t center = drive.feedback().position;
    const auto start = std::chrono::steady_clock::now();

    const auto end = std::chrono::steady_clock::now() +
                     std::chrono::milliseconds(static_cast<long>(seconds * 1000));
    while (std::chrono::steady_clock::now() < end) {
        auto ms_since_start = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start
        );
        int32_t target = center + static_cast<int32_t>((amplitude * sin(2 * M_PI * ms_since_start.count() / (period * 1000.0))));
        drive.commandPosition(target);
        const auto fb = drive.feedback();
        std::cout << "  state=" << stablecops::ds402::toString(fb.state)
                  << " pos=" << fb.position << " target=" << target << " vel=" << fb.velocity << '\n';
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    drive.stop();
    std::cout << "done\n";
    return EXIT_SUCCESS;
}
