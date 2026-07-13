# stableCOPS

A C++17 library for driving CANopen / CiA-402 servo drives over SocketCAN,
built on [Lely CANopen](https://opensource.lely.com/canopen/). It hides the
CANopen state machine, cyclic PDO exchange, faults, homing, and shutdown behind
one thread-safe handle: **`MotorDrive`**.

| Document | What it covers |
| --- | --- |
| this README | Quick start, the `MotorDrive` API, configuration, safety |
| [`docs/development.md`](docs/development.md) | Building, tests, CLI tools, examples, real-time tuning |
| [`docs/architecture.md`](docs/architecture.md) | Runtime internals: threading, cyclic engine, DS402 control, safety, homing |
| [`docs/canopen_motor_pipeline.md`](docs/canopen_motor_pipeline.md) | Motor profiles and the EDS → DCF/summary generation pipeline |
| [`docs/eyou_rp_notes.md`](docs/eyou_rp_notes.md) | EYou/EuServo RP drive specifics: quirks, units, faults, wire-level debugging |

## Quick start

Install the one build/runtime dependency, then link the CMake target:

```bash
sudo apt-get install pkg-config liblely-coapp-dev
```

```cmake
find_package(stableCOPS REQUIRED)          # or add_subdirectory / FetchContent
target_link_libraries(myapp PRIVATE stableCOPS::stablecops)
```

The public headers are Lely-free, so consumers need no Lely headers or link
flags of their own. At runtime you need SocketCAN up (`sudo ./canup.sh`), CAN
privileges, and the generated DCF/summary reachable at the paths in
`MotorConfig` (installed samples live under `share/stablecops/`).

```cpp
#include "stablecops/app/MotorConfig.hpp"
#include "stablecops/app/MotorDrive.hpp"

using namespace stablecops;

app::MotorConfig config;                 // profile fills scaling/timeouts/homing
config.can_interface = "can0";
config.node_id = 1;
config.operation_mode = ds402::OperationMode::CyclicSynchronousPosition;
config.enable_on_boot = true;            // energise + hold current position

app::MotorDrive drive(config);
drive.start();                           // boots the chain; throws on failure
drive.waitUntilLive(std::chrono::seconds(5));

while (drive.feedbackLive()) {           // false once the drive stops talking
    auto fb = drive.feedback();          // thread-safe snapshot
    drive.commandPosition(fb.position);  // stream a CSP setpoint
    if (drive.faulted()) {
        drive.resetFault();              // clear + recover to current intent
    }
}

drive.quickStop();                       // controlled ramp-down (stays energised)
drive.stop();                            // graceful de-energise (coasts)
```

Several `MotorDrive`s that name the same `can_interface` transparently share
one bus (one Lely master, one loop thread, one SYNC). Construct all drives for
a chain first, then `start()` any one of them.

## The `MotorDrive` API

- **Lifecycle** — `start()`, `stop()` (coast), `quickStop()` (ramp + hold),
  `running()`, `shutdownBus()` / `forceStopBus()` (whole-chain teardown).
- **Telemetry (any thread)** — `feedback()`, `feedbackLive()`,
  `waitUntilLive(timeout)`, `positionDegrees()` / `positionRadians()`,
  `cyclicStats()` (achieved cycle interval / jitter).
- **Status & faults** — `enabled()`, `faulted()`, `errorCode()`, `resetFault()`.
- **Enable & mode** — `enableOperation(hold)`, `setOperationMode(mode)`
  (confirmed against 0x6061).
- **Cyclic setpoints** — `commandPosition()` (CSP), `commandVelocity()`
  (CSV/PV), `commandTorque()` (CST/PT).
- **Profile move** — `moveToPosition(counts, relative)` (PP; the drive runs its
  own trajectory).
- **Homing** — `startHoming(config)`, `homingPhase()`, `homingResult()`;
  `drive.config().homing` is the actuator's profile-tuned base configuration.
- **Objects** — `readObject()` / `writeObject()` for arbitrary CANopen objects
  (served from the cyclic cache or staged into it when mapped, SDO otherwise).
- **Configuration** — `config()` returns the profile-resolved `MotorConfig`
  this drive runs with.

## Configuration

`MotorConfig` is the only configuration type. The **motor profile YAML** is the
single source of truth for actuator settings: its `runtime:` section (scaling,
watchdog windows, homing defaults) and the master's SYNC period are recorded in
the generated `summary.json`, and constructing a `MotorDrive` resolves them
into the config:

- a field you left at its built-in default takes the profile's value;
- a field you set explicitly (code or CLI flag) wins;
- `sync_period_us` **always** follows the summary — it must match the DCF.

Key fields:

- `can_interface`, `node_id` — which bus and drive.
- `master_dcf_path`, `summary_path` — generated artifacts, loaded by path at
  boot ([pipeline doc](docs/canopen_motor_pipeline.md)).
- `operation_mode` — CSP/CSV/CST or PP/PV/PT, selected over SDO at boot.
- `enable_on_boot` / `hold_position_on_boot` / `monitor_on_boot` — boot action.
- `profile_velocity` / `profile_acceleration` / `profile_deceleration` /
  `torque_slope` — profile-mode ramps, written over SDO at boot when set.
- `counts_per_rev`, `gear_ratio`, `feedback_timeout`, `max_position_step`,
  `homing` — actuator values, profile-sourced.
- `sync_period_us`, `rt` — bus-level: cyclic period and opt-in real-time tuning.

## Safety behaviour

- **Feedback watchdog** — while energised, no cyclic feedback for
  `feedback_timeout` (default 100 ms) drops the power stage and clears
  `feedbackLive()`.
- **Three fault channels** — DS402 statusword / 0x603F in the cyclic TPDO, the
  drive's EMCY messages, and node loss via the master's consumer heartbeat
  (`feedback().node_alive`). `resetFault()` clears and recovers to the current
  operating intent.
- **Stopping** — `stop()` de-energises (the joint coasts). Prefer `quickStop()`
  for a loaded or vertical axis: it decelerates on the drive's quick-stop ramp
  and holds energised. Neither tears down the shared bus.
- **Setpoint hygiene** — enable, fault recovery, and mode changes glue the
  position target to the measured position so the drive never sees a step.

## Logging

The library never writes to stdout/stderr directly; it routes whole lines
through `stablecops::log`. Install a sink to redirect or silence it, and set a
minimum level:

```cpp
#include "stablecops/log/Log.hpp"
stablecops::log::setLevel(stablecops::log::Level::Warn);
stablecops::log::setSink([](stablecops::log::Level level, const std::string& line) {
    myLogger.log(stablecops::log::toString(level), line);
});
```

## Operator tools

Two CLI front-ends ship with the library: `stablecops_master` (bring-up and
diagnostics) and `stablecops_commissiond` (browser commissioning UI). See
[`docs/development.md`](docs/development.md).
