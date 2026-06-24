# stableCOPS

Generic CANopen motor bring-up scaffold built on Lely CANopen and a small
transport-independent DS402 layer.

The intended flow is data-driven: keep vendor EDS files unchanged, describe the
motor in a profile, generate Lely artifacts, then boot and inspect the drive
before enabling motion commands. See `docs/canopen_motor_pipeline.md` for the
full workflow.

## Requirements

```bash
sudo apt-get update
sudo apt-get install pkg-config liblely-coapp-dev liblely-co-tools python3-dcf-tools
```

## Build

```bash
cmake --preset default
cmake --build --preset default
```

## Generate CANopen Artifacts

```bash
python3 tools/generate_canopen_config.py --profile config/motors/eyou_phu.yml
```

This derives a normalized EDS, dcfgen YAML, PDO summary, and `dcf/master.dcf`
from the immutable vendor EDS.

## Run

Bring up CAN:

```bash
sudo ./canup.sh
```

Print configuration without opening CAN:

```bash
build/stablecops_master --can can0
```

Boot the drive:

```bash
build/stablecops_master --can can0 --dcf dcf/master.dcf --master-node 127 --node 1 --run
```

Boot and inspect live CANopen/DS402 objects:

```bash
build/stablecops_master --can can0 --dcf dcf/master.dcf --master-node 127 --node 1 --inspect --run
```

Receive cyclic PDO feedback without energising the drive (monitor mode):

```bash
build/stablecops_master --can can0 --dcf dcf/master.dcf --master-node 127 --node 1 --monitor --run
```

Safely enable the DS402 power stage:

```bash
build/stablecops_master --can can0 --dcf dcf/master.dcf --master-node 127 --node 1 --enable --run
```

Enable and hold the current CSP position:

```bash
build/stablecops_master --can can0 --dcf dcf/master.dcf --master-node 127 --node 1 --hold-position --run
```

Select the cyclic mode at boot (CSP/CSV/CST share one fixed PDO layout; the mode
is chosen over SDO while pre-operational):

```bash
build/stablecops_master --can can0 --dcf dcf/master.dcf --master-node 127 --node 1 --mode csv --enable --run
```

Command a guarded CSP step only after hold/enable has been verified:

```bash
build/stablecops_master --can can0 --dcf dcf/master.dcf --master-node 127 --node 1 --csp-relative 1000 --max-position-step 1000 --run
```

## Library API (MotorDrive)

`stablecops::app::MotorDrive` is a thread-safe handle that runs the Lely event
loop on its own thread, so an application can read feedback and issue setpoints
from its own thread without touching Lely internals:

```cpp
stablecops::app::MotorConfig config;
config.monitor_on_boot = true;          // or enable_on_boot / hold_position_on_boot
stablecops::app::MotorDrive drive(config);
drive.start();
while (drive.feedbackLive()) {
    auto fb = drive.feedback();          // thread-safe snapshot
    // ... use fb.position / fb.velocity / fb.torque / fb.state ...
}
drive.commandPosition(counts);          // posted to the loop thread (when enabled in CSP)
// In a cyclic mode chosen via config.operation_mode:
drive.commandVelocity(units);           // CSV
drive.commandTorque(units);             // CST
drive.stop();                            // graceful de-energise + join
```

## Examples

Built under `build/examples/`:

```bash
# Stream decoded feedback from the drive over PDO, power stage stays off:
build/examples/pdo_feedback_monitor --can can0 --dcf dcf/master.dcf --node 1 --seconds 10

# Enable + hold the current CSP position while printing live feedback:
build/examples/enable_and_hold --can can0 --dcf dcf/master.dcf --node 1 --seconds 10
```

One simple example per cyclic mode (defaults command 0 = no motion; SPINS the
motor if a nonzero setpoint is passed):

```bash
# CSP: hold (start + offset) counts
build/examples/csp_position --can can0 --node 1 --offset 0 --seconds 5

# CSV: stream a constant target velocity
build/examples/csv_velocity --can can0 --node 1 --velocity 0 --seconds 5

# CST: stream a constant target torque
build/examples/cst_torque  --can can0 --node 1 --torque 0 --seconds 5
```

## Linting & formatting

In-editor diagnostics, completion, and go-to come from **clangd**, which reads
`build/compile_commands.json` plus the repo configs (`.clangd`, `.clang-tidy`,
`.clang-format`). Static analysis is **clang-tidy**; formatting is
**clang-format**.

One-time setup:

```bash
# 1. Tools (user-space; no sudo needed). Or apt install clangd clang-tidy clang-format.
pip install --user clangd clang-tidy clang-format

# 2. Generate the compile database clangd needs.
cmake --preset default        # creates build/compile_commands.json (symlinked at repo root)

# 3. In Cursor/VS Code: install the "clangd" extension (llvm-vs-code-extensions.vscode-clangd)
#    and disable the Microsoft C/C++ IntelliSense engine so they don't fight.
```

Command-line usage (wraps the gcc-toolchain pin for you):

```bash
tools/lint.sh format        # rewrite files in place
tools/lint.sh format-check  # fail if anything is unformatted
tools/lint.sh tidy          # run clang-tidy static analysis
tools/lint.sh               # format-check + tidy

# Or invoke the tools directly:
clang-format -i src/ds402/State.cpp
clang-tidy -p build src/ds402/State.cpp
```

Note: the pip-installed clang defaults to a gcc toolchain dir without libstdc++
headers, so `.clangd` and `tools/lint.sh` pin it to gcc-11
(`--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/11`). Adjust that path if your
libstdc++ lives elsewhere (`ls -d /usr/include/c++/*`).

## Layout

- `eds/EDS files/`: immutable vendor EDS files.
- `config/motors/`: declarative motor profiles.
- `tools/generate_canopen_config.py`: EDS normalization, dcfgen YAML emission, DCF generation, and validation.
- `generated/canopen/`: derived EDS/YAML/summary artifacts.
- `dcf/master.dcf`: generated runtime DCF.
- `include/stablecops/ds402/` and `src/ds402/`: generic DS402 objects, state decoding, and commands.
- `include/stablecops/lely/` and `src/lely/`: Lely adapter and boot callbacks.
