// read_aux_encoder -- Print the auxiliary (load-side) encoder of every drive
// on the EtherCAT chain. Intended for the 2 Everest boards.
//
// Usage: read_aux_encoder <interface> [config.yaml]
//   e.g. sudo ./build/examples/read_aux_encoder enp0s31f6
//
// The auxiliary encoder is CoE object 0x2033 (pdo::auxFeedback), which is part
// of the default PDO profile, so no extra mapping is required. The raw value is
// printed alongside the SI load position (radians) derived from it.
//
// With no config.yaml the master runs in scan mode: it auto-detects the drives
// and names them drive1, drive2, ... in bus-discovery order.

#include "ecat/master_runtime.hpp"
#include "ecat/pdo_handles.hpp"

#include <cstdio>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: %s <iface> [config.yaml]\n", argv[0]);
        return 1;
    }

    const char *iface = argv[1];
    const char *config = (argc >= 3) ? argv[2] : ""; // empty => scan mode

    MasterRuntime runtime(iface, config);
    if (!runtime.start())
        return 1;

    auto &io = runtime.io();
    const std::size_t n = io.driveCount();
    std::printf("[read_aux_encoder] %zu drive(s) on %s\n", n, iface);

    for (std::size_t i = 0; i < n; ++i) {
        const auto &d = io.info(i);
        if (!io.hasDriveTxField(i, 0x2033))
            std::fprintf(stderr,
                         "[warn] drive %zu '%s' (%s) has no aux feedback (0x2033) in its "
                         "PDO profile — value will read as 0\n",
                         i, d.name.c_str(), d.model.c_str());
    }

    double last_print = -1.0;
    while (runtime.ok()) {
        double t = runtime.elapsedSec();
        if (t - last_print >= 0.1) {
            std::printf("[%6.1fs]", t);
            for (std::size_t i = 0; i < n; ++i) {
                const auto &d = io.info(i);
                uint32_t aux = io.get(i, pdo::auxFeedback);
                double loadRad = io.feedback(i).loadPosition;
                std::printf("  %s(%s): aux=%u load=%.4f rad", d.name.c_str(),
                            d.model.c_str(), aux, loadRad);
            }
            std::printf("\n");
            last_print = t;
        }
        runtime.sleepCycle();
    }
    return 0;
}
