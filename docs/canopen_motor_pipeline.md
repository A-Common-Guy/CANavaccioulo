# CANopen Motor Pipeline

The motor pipeline has one rule: vendor data goes in, generated artifacts come
out. Runtime code stays generic unless a drive really needs vendor-specific
behavior.

## Onboard A Motor

1. Copy the vendor EDS into `eds/EDS files/` and leave it unchanged.
2. Add a profile under `config/motors/`.
3. Generate artifacts:

```bash
python3 tools/generate_canopen_config.py --profile config/motors/eyou_phu.yml
```

4. Bring up SocketCAN:

```bash
sudo ./canup.sh
```

5. Boot and inspect:

```bash
build/stablecops_master --can can0 --dcf dcf/master.dcf --master-node 127 --node 1 --inspect --run
```

6. Enable motion commands only after the live identity, DS402 state, supported
   modes, and PDO summary match expectations.

## Enable And Hold

The first useful action after inspection is a safe DS402 state transition. This
primes the CSP target to the current position, sends `shutdown`, `switch on`,
and `enable operation`, then checks that the drive reaches operation enabled.

```bash
build/stablecops_master --can can0 --dcf dcf/master.dcf --master-node 127 --node 1 --enable --run
```

For CSP bring-up, prefer holding the current position first:

```bash
build/stablecops_master --can can0 --dcf dcf/master.dcf --master-node 127 --node 1 --hold-position --run
```

Only after that works, request small explicit CSP target changes. The target is
rejected if the requested step exceeds `--max-position-step`.

```bash
build/stablecops_master --can can0 --dcf dcf/master.dcf --master-node 127 --node 1 --csp-relative 1000 --max-position-step 1000 --run
```

For interactive bench work, run the local browser commissioning daemon instead
of hand-sending SDOs or raw CAN frames:

```bash
build/stablecops_commissiond --can can0 --dcf dcf/master.dcf --node 1 --mode csp
```

It reuses `stablecops::app::MotorDrive`: feedback comes from the generated TPDO
summary, Enable/Stop/Fault Reset use the same CiA402 ladder as the CLI, and
motion commands call `commandPosition`, `commandVelocity`, `commandTorque`, or
`moveToPosition`. The object panel is intentionally typed and low-level for
parameters/diagnostics; mapped command objects such as `0x6040`, `0x607A`,
`0x60FF`, and `0x6071` remain PDO-driven on this firmware.

Available boot actions:

- `--enable`: run the DS402 safe enable sequence.
- `--hold-position`: enable and keep the CSP target at the current position.
- `--csp-target counts`: enable and request an absolute CSP target.
- `--csp-relative counts`: enable and request a relative CSP target.
- `--max-position-step counts`: limit the allowed target delta.

## Profile Format

Profiles are small YAML files. The default PHU profile is
`config/motors/eyou_phu.yml`.

Required fields:

- `name`: stable name used for generated artifact filenames.
- `vendor_eds`: vendor EDS path relative to the repository root.
- `master.node_id`: Lely master CANopen node ID.
- `node.node_id`: drive CANopen node ID.
- `identity_policy`: `strict` or `ignore`.
- `pdo_policy`: start with `vendor-default`.
- `mode_policy`: start with `vendor-default`.

Generated artifacts are written to `generation.generated_dir`; the runtime DCF
is written to `generation.dcf_dir`.

For a chain of identical joints, replace the single `node:` mapping with a
`nodes:` list (the generator accepts either). All nodes share this same EDS and
PDO layout, differing only by node id:

```yaml
nodes:
  - { name: rp_joint_1, node_id: 1 }
  - { name: rp_joint_2, node_id: 2 }
  - { name: rp_joint_3, node_id: 3 }
```

`dcfgen` emits one slave section per node and builds the master's RPDO/TPDO
image for every slave; the summary records all of them under `node_ids` (and
keeps `node_id` as the first, for single-node call sites).

## Generated Files

For the PHU profile, generation writes:

- `generated/canopen/eyou_phu/eyou_phu.normalized.eds`
- `generated/canopen/eyou_phu/eyou_phu.dcfgen.yml`
- `generated/canopen/eyou_phu/eyou_phu.summary.json`
- `dcf/master.dcf`

The normalized EDS, generated YAML, summary, and DCF are derived artifacts. Do
not hand-edit them; update the vendor EDS/profile and regenerate.

## Inspection

`--inspect` performs read-only SDO diagnostics after boot. It reads:

- `0x1018` identity
- `0x6502` supported modes
- `0x6060` commanded mode
- `0x6061` displayed mode
- `0x6041` statusword and decoded DS402 state
- position, velocity, torque, and error code feedback

## Runtime Contract

The generated DCF and summary own profile-specific PDO knowledge. Runtime C++
code does not hardcode RPDO/TPDO map indexes: at start-up the runtime loads the
generated `*.summary.json` (`--summary`, defaulting to the euservo_rp profile)
into a `stablecops::config::PdoMap`, and `OnConfig` programs the drive directly
from that map. Pick a different motor at runtime by pointing `--dcf`/`--summary`
at its generated artifacts.

`stablecops::ds402::DriveController` works in DS402 object terms: controlword,
statusword, operation mode, target position, velocity, and torque. SDO remains
the fallback for configuration and diagnostics.

### Command image and cyclic streaming

The drive groups command objects into each RPDO. The EYou firmware **rejects**
the EDS-default RxPDO1 (`0x6040` controlword + `0x60FF` target velocity +
`0x6060` modes) during CSP bring-up: even with a correct controlword on the
wire, the drive stays in *ready to switch on* and ignores every controlword
(including `0x80` disable-voltage), which means it is discarding the whole
frame. Keeping the mode object out of the cyclic stream fixes this. We use a
single fixed **cyclic superset** layout so one PDO map serves CSP/CSV/CST
without re-mapping per mode (the active mode is selected over SDO at boot):

- **RPDO1** (`0x301`) = `0x6040` controlword + `0x607A` target position
- **RPDO2** (`0x381`) = `0x60FF` target velocity + `0x6071` target torque
- RPDO3: disabled (unused)
- **TPDO1** = `0x6041` statusword + `0x6061` mode display + `0x6077` torque
- **TPDO2** = `0x6064` position + `0x606C` velocity

The drive obeys only the target that matches its active mode (`0x6060`) and
ignores the others, so all cyclic targets can ride the bus every cycle. This is
the layout the generator emits (`eds_overrides` in the profile), records
in `*.summary.json`, and that `OnConfig` reads back from the summary (via
`config::PdoMap`) to program on the drive. Because a PDO is transmitted as a whole
frame, the master must never update one mapped object in isolation: doing so
would send stale or zero values for that object's PDO neighbours.

`MotorDriver` therefore runs a generic cyclic engine driven by the loaded
`PdoMap`. At construction it snapshots the active RxPDO objects into a
`command_objects_` list (each with its index, subindex, and CANopen type) and
the active TxPDO objects into a `feedback_objects_` list. On every `OnSync` it
reads *every* feedback object into a cache (decoding the known DS402 fields into
`ds402::Feedback`) and writes *every* command object out as one coherent frame.
Nothing in the cyclic path hardcodes which objects are mapped, so changing the
PDO layout in the profile is enough to change what the master streams and
decodes — CSV/CST/PP only need their target object mapped in the profile.

Command writes from `DriveController` are staged, not sent immediately: a write
to a mapped command object updates its buffered value (streamed next SYNC); a
write to a DS402 command object that is *not* currently mapped is dropped (those
objects are PDO-only on this firmware and abort SDO downloads with
`0x00000002`); any other object falls back to a blocking SDO write. Feedback
objects are returned from the cache without bus traffic. SDO is used only for
configuration, identity, error code, and the one-time image seed at enable.

### Drive configuration (pre-operational)

The PHU/RP drives ship with every PDO set to transmission type `0`
(event-driven), so out of NVM they never stream TPDOs on SYNC and never run the
cyclic RPDO exchange. PDO parameters are only reliably SDO-writable while the
node is pre-operational. The vendor bring-up recipe (manual Table 5-5) therefore
configures the drive *before* it is started.

`MotorDriver::OnConfig` runs during the NMT boot "update configuration" step,
which executes while the node is still pre-operational. When a motion action is
requested (so `--inspect` stays read-only) it pushes, over SDO, for each active
PDO (RPDO1, TPDO1, TPDO2): disable the PDO (set the COB-ID valid bit), set the
transmission type to `1` (cyclic synchronous), rewrite the mapping from scratch,
then re-enable it. With the cyclic superset layout RPDO1 and RPDO2 are both
programmed; RPDO3 is explicitly disabled (COB-ID valid bit) because the master
never transmits it.

The COB-IDs and mappings written match `dcf/master.dcf` exactly (both are driven
from the profile's `eds_overrides`), so both ends agree. Rewriting the whole
mapping (not just the transmission type) also clears the stale/oversized mapping
the drive otherwise reports straight from NVM. If configuration fails, the boot
is aborted with the SDO abort code rather than starting a node that cannot
communicate.

The mode object `0x6060` is kept **out of the cyclic RxPDO** (streaming it is
exactly what makes the firmware reject RxPDO1), but it is SDO-writable while the
node is pre-operational, so it is selected over SDO here when a mode is requested
(`--mode csp|csv|cst`, or `MotorConfig::operation_mode`) — the window `OnConfig`
runs in. This is distinct from the *target/controlword* command objects
(`0x6040`, `0x607A`, `0x60FF`, `0x6071`), which are PDO-only on this firmware and
abort SDO downloads with `0x00000002`. When no mode is requested the write is
skipped and the drive's persisted mode (factory default `8`, CSP) is left in
place, preserving the original CSP-only behaviour.

### Enable sequence

`enableDrive` seeds the command image from the live drive (mode, actual
position, profile parameters), starts cyclic streaming, waits a few SYNC cycles,
pulses a fault reset (controlword bit 7, per the vendor recipe), then walks
`shutdown -> switch on -> enable operation`, confirming each transition from
cached statusword feedback. While bringing CSP up, the commanded target tracks
the measured position so the drive never sees a step at enable; once operation
enabled is reached, the target is frozen (held).

The enable path refuses to proceed on fault states, nonzero drive error codes,
transition timeouts, a missing SYNC stream, non-CSP target commands, or target
steps larger than the configured guard.

Add explicit PDO remapping to a profile only when the vendor default layout is
insufficient and the drive documentation confirms the remap sequence.

### Safety: feedback watchdog and fault recovery

Two guards run entirely on the cyclic path, in the same non-blocking style as the
graceful stop and the Profile Position handshake:

- **Feedback-staleness watchdog.** Every received TPDO frame stamps
  `last_feedback_time_` (in `OnRpdoWrite`). Each `OnSync` recomputes staleness
  against `feedback_timeout`; `feedbackLive()` is published as
  `received-at-least-one && not-stale` (no longer latched), so readers and the
  facade can trust it. While the drive is energised, going stale logs once and
  calls `requestGracefulStop`, which streams disable-voltage and force-finishes
  at its deadline even if the drive never answers — so a drive that drops off the
  bus mid-motion is always de-energised. A `feedback_timeout` of 0 disables it.
- **Fault logging and recovery.** `OnSync` logs the edge into/out of a fault
  (statusword + error code) so faults during cyclic operation are visible.
  `requestFaultReset` (exposed as `MotorDrive::resetFault`) first resets the
  targets to a safe hold (zero velocity/torque, position glued to actual), then
  runs a non-blocking ladder from `OnSync`: a controlword bit-7 fault-reset edge,
  then `shutdown -> switch on -> enable operation`, confirming each transition
  from cached statusword. It recovers to operation enabled when the drive was
  configured to run, otherwise clears the fault and leaves it safely disabled; a
  timeout or re-fault drops to disable-voltage. Only one controlword driver runs
  per cycle, in priority order: graceful stop > fault recovery > setpoint
  handshake.

### Profile modes (PP / PV / PT)

Profile modes reuse the same fixed PDO layout and the same SDO mode select
(`0x6060`). Their **profile parameters** are ordinary configuration objects, so
they are written over SDO during the same pre-operational window when provided
via `MotorConfig` (`configureProfileParameters`): profile velocity (`0x6081`),
acceleration (`0x6083`), deceleration (`0x6084`), torque slope (`0x6087`).

- **Profile Velocity / Profile Torque** stream the same `target_velocity` /
  `target_torque` objects as CSV / CST; the drive applies its own ramp instead
  of following the setpoint directly, so `commandVelocity` / `commandTorque`
  work unchanged.
- **Profile Position** needs the DS402 new-setpoint handshake. `moveToPosition`
  stages the target into the cyclic image and arms `advanceProfileSetpoint`,
  which (from `OnSync`) pulses controlword bit 4 (new setpoint, with
  change-immediately and optional relative), waits for statusword bit 12
  (setpoint acknowledge), then drops bit 4 — producing the rising edge coherently
  in the cyclic stream. The drive then runs the trajectory itself.

## Multiple Drives And The Real-Time Loop

A CAN interface is a shared resource: one Lely master, one event-loop thread,
one SYNC stream. Every drive on that wire is stepped by that single SYNC, so the
runtime models a chain as **one master with one `MotorDriver` per node**, all
sharing the master and SYNC. Each `MotorDriver` keeps its own DS402 state
machine, cyclic PDO image, feedback watchdog, and fault recovery, so the
per-drive behaviour above is unchanged; only the count changes.

### Ownership: the hidden bus

Applications hold only `stablecops::app::MotorDrive` handles. Each names a CAN
interface and a node id. Drives that name the same interface transparently share
one hidden, ref-counted `Bus` (keyed by interface name in a process-wide
registry); different interfaces get fully independent buses, each with its own
loop thread and SYNC. So "several chains" is just constructing `MotorDrive`s
with different interface names.

Lifecycle is static: construct all drives for a chain, then `start()` any one of
them. The first `start()` builds the Lely application for **all** registered
nodes on the loop thread (FiberDrivers must live on the thread that runs their
tasks) and resets the master once, booting the whole chain; sibling `start()`s
are no-ops. The last `MotorDrive` released on an interface tears the bus down
with a coordinated graceful stop (every drive de-energised, all nodes reset,
then the loop stops). Bus-level config (`can_interface`, `master_dcf_path`,
`summary_path`, `master_node_id`, `sync_period_us`, `rt`) must match across all
drives on one interface; a mismatch throws at construction.

`stablecops_master --nodes 1,2,3` does the same from the CLI (one config per
node, shared bus-level fields).

### Deterministic cadence (latency / jitter)

The cadence stays the master's SYNC (driven by `master.sync_period` in the DCF,
`CLOCK_MONOTONIC`); we make it deterministic by tuning the loop thread rather
than changing the timing source. The cyclic path is already allocation-free
(`OnSync` only reads/writes the cached PDO images), so the remaining jitter is
scheduling. `RtConfig` (`MotorConfig::rt`, or `--rt/--rt-prio/--rt-cpu/--no-mlock`)
opts the loop thread into:

- `SCHED_FIFO` at a configurable priority (real-time scheduling class),
- optional CPU pinning (pair with `isolcpus` to keep other work off the core),
- `mlockall` + a stack prefault to keep the cyclic path off the pager.

It is best-effort: each step degrades gracefully with a single warning if the
process lacks privileges (`CAP_SYS_NICE`, `rtprio`/`memlock` ulimits), so an
unprivileged run still works at normal priority.

Achieved cadence is measured on the loop thread in `OnSync` (interval between
consecutive SYNCs) and published as `CyclicStats` — interval min/max/mean and
the worst-case absolute deviation from the nominal `sync_period_us`. Read it via
`MotorDrive::cyclicStats()` or the CLI `--stats` readout to verify latency and
jitter on the target machine.

## Wire-Level Verification

When a transition stalls, confirm what is actually on the bus before changing
code. The command frames and feedback frames are the ground truth.

Watch the command RPDO the master sends (`0x201`), the feedback TPDOs the drive
sends (`0x181/0x281`), and SYNC (`0x080`). In the CSP layout the master no
longer transmits `0x301`/`0x401`:

```bash
candump -tz can0,201:7FF can0,181:7FF can0,281:7FF can0,080:7FF
```

What healthy bring-up looks like:

- `0x080` SYNC frames arrive at the configured cycle (1 ms for the PHU profile).
- `0x201` is a 6-byte frame: controlword (bytes 0-1) walking `06 00 -> 07 00 ->
  0F 00`, followed by the 4-byte target position (bytes 2-5) tracking the
  measured position during bring-up, then frozen.
- `0x181` statusword tracks the transitions (`31 06` ready to switch on ->
  `33 06` switched on -> `37 06` operation enabled), error code stays `0`.

The drive accepts SDO writes to **PDO communication/mapping** objects and to the
mode object `0x6060` while pre-operational (this is what `OnConfig` relies on).
The DS402 **target/controlword** objects (`0x6040`, `0x607A`, `0x60FF`, `0x6071`)
are PDO-only and abort SDO downloads with `0x00000002`, which is why they go over
PDO:

```bash
# SDO download of a target object (e.g. 0x607A) aborts (581 ... 80) with
# abort code 0x00000002 -- target/controlword objects are PDO-only on this firmware
cansend can0 601#2360 7A00 00000000
candump -tz can0,581:7FF
```

After a successful bring-up, re-running `--inspect` should now report every
active PDO with `transmission type 0x01` and the expected mapped-object counts.

