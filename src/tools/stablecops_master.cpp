#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>

#include "stablecops/app/CanopenApplication.hpp"
#include "stablecops/app/MotorConfig.hpp"
#include "stablecops/ds402/State.hpp"

namespace {

std::optional<stablecops::ds402::OperationMode> parseOperationMode(
    const std::string& name) {
    using stablecops::ds402::OperationMode;
    if (name == "csp") return OperationMode::CyclicSynchronousPosition;
    if (name == "csv") return OperationMode::CyclicSynchronousVelocity;
    if (name == "cst") return OperationMode::CyclicSynchronousTorque;
    return std::nullopt;
}

}  // namespace

int main(int argc, char** argv) {
    stablecops::app::MotorConfig config;

    const auto print_usage = [] {
        std::cerr << "usage: stablecops_master [--can can0] [--dcf dcf/master.dcf] "
                     "[--summary generated/.../<name>.summary.json] "
                     "[--master-node 127] [--node 1] [--inspect] [--monitor] [--enable] "
                     "[--mode csp|csv|cst] "
                     "[--hold-position] [--csp-target counts] [--csp-relative counts] "
                     "[--max-position-step counts] [--run]\n";
    };

    bool run = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--run") {
            run = true;
        } else if (arg == "--inspect") {
            config.inspect_on_boot = true;
        } else if (arg == "--monitor") {
            config.monitor_on_boot = true;
        } else if (arg == "--enable") {
            config.enable_on_boot = true;
        } else if (arg == "--hold-position") {
            config.enable_on_boot = true;
            config.hold_position_on_boot = true;
        } else if (arg == "--mode" && i + 1 < argc) {
            const std::string mode_name = argv[++i];
            const auto mode = parseOperationMode(mode_name);
            if (!mode) {
                std::cerr << "unknown --mode '" << mode_name
                          << "' (expected csp, csv, or cst)\n";
                print_usage();
                return EXIT_FAILURE;
            }
            config.operation_mode = mode;
        } else if (arg == "--can" && i + 1 < argc) {
            config.can_interface = argv[++i];
        } else if (arg == "--dcf" && i + 1 < argc) {
            config.master_dcf_path = argv[++i];
        } else if (arg == "--summary" && i + 1 < argc) {
            config.summary_path = argv[++i];
        } else if (arg == "--master-node" && i + 1 < argc) {
            config.master_node_id = static_cast<uint8_t>(std::stoi(argv[++i]));
        } else if (arg == "--node" && i + 1 < argc) {
            config.node_id = static_cast<uint8_t>(std::stoi(argv[++i]));
        } else if (arg == "--csp-target" && i + 1 < argc) {
            config.enable_on_boot = true;
            config.csp_target_position = std::stoi(argv[++i]);
        } else if (arg == "--csp-relative" && i + 1 < argc) {
            config.enable_on_boot = true;
            config.csp_relative_move = std::stoi(argv[++i]);
        } else if (arg == "--max-position-step" && i + 1 < argc) {
            config.max_position_step = std::stoi(argv[++i]);
            if (config.max_position_step < 0) {
                print_usage();
                return EXIT_FAILURE;
            }
        } else {
            print_usage();
            return EXIT_FAILURE;
        }
    }

    std::cout
        << "stableCOPS CANopen master\n"
        << "CAN interface: " << config.can_interface << '\n'
        << "Master Node ID: " << static_cast<int>(config.master_node_id) << '\n'
        << "Node ID: " << static_cast<int>(config.node_id) << '\n'
        << "Inspect on boot: " << (config.inspect_on_boot ? "yes" : "no") << '\n'
        << "Monitor on boot: " << (config.monitor_on_boot ? "yes" : "no") << '\n'
        << "Enable on boot: " << (config.enable_on_boot ? "yes" : "no") << '\n'
        << "Operation mode: "
        << (config.operation_mode
                ? stablecops::ds402::toString(*config.operation_mode)
                : std::string("persisted (unchanged)"))
        << '\n'
        << "Hold position: " << (config.hold_position_on_boot ? "yes" : "no") << '\n'
        << "Max position step: " << config.max_position_step << " counts\n"
        << "Master DCF: " << config.master_dcf_path << '\n'
        << "PDO summary: " << config.summary_path << '\n';

    if (!run) {
        std::cout << "\nPass --run to open SocketCAN and start the Lely master.\n";
        return EXIT_SUCCESS;
    }

    stablecops::app::CanopenApplication app(config);
    app.resetMaster();
    app.run();

    return EXIT_SUCCESS;
}
