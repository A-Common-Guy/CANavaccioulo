# Runtime architecture

How the library is layered, how threads and the cyclic path work, and how the
DS402 control sequences, safety guards, and homing are implemented. Read this
before changing `src/lely/MotorDriver.cpp` or the `app` layer.

## Layers

```
app::MotorDrive          public, thread-safe handle (one per drive)
   │ shares
app::Bus                 one per CAN interface: master + loop thread + SYNC
   │ owns (on the loop thread)
app::CanopenApplication  Lely io/event plumbing, coordinated shutdown
   │ one per node
lely::MotorDriver        cyclic engine + DS402 state machines (FiberDriver)
   │ built on
ds402::*                 pure DS402 decoding, diagnostics, value types
config::*                MotorConfig + profile resolution, PDO-summary loader
```

Lely types appear only under `include/stablecops/lely/` and in `src/`; every
other public header is Lely-free, so consumers of `MotorDrive` need no Lely
headers.

## Threading & ownership

- **One CAN interface = one `Bus`** = one Lely master, one event-loop thread,
  one SYNC stream. Buses live in a process-wide registry keyed by interface
  name; the first `MotorDrive` for an interface creates the bus, siblings join
  it, and the last one released tears it down (graceful stop + join).
- Lifecycle is static: construct all drives for a chain, then `start()` once.
  Registering a node after the bus started throws. Bus-level config
  (`can_interface`, DCF/summary paths, master node id, `sync_period_us`, `rt`)
  must match across drives on one interface.
- The `CanopenApplication` (and every Lely object) is constructed **and
  destroyed on the loop thread** — a Lely `FiberDriver` must live on the thread
  that runs its tasks.
- Cross-thread access goes through the bus: `postToDriver` (fire-and-forget)
  and `invokeOnDriver` (blocks, rethrows exceptions). These are the only
  correct ways to touch driver state from another thread. Telemetry
  (`feedback()`, `cyclicStats()`, `feedbackLive()`) reads published snapshots
  and is safe from any thread.

### Fiber rules (important when editing `MotorDriver`)

Lely runs every driver callback in a **fiber**. `Wait()`/`USleep()` suspend the
calling fiber while the event loop keeps running — so blocking SDO calls are
fine in boot/config/request context, but two consequences matter on the cyclic
path:

1. `OnSync` is `noexcept`: anything that can throw (SDO aborts) must be caught.
2. Each `OnSync` runs on its own fiber. If one suspends on an SDO exchange, the
   next SYNC **re-enters** the state machines. Long-running phases must guard
   against re-entry (see `homing_command_in_flight_`).

## Configuration

`config::MotorConfig` is the single configuration struct, consumed both by the
public API and by `lely::MotorDriver` (no translation layer).
`config::resolveMotorConfig` — applied automatically when a `MotorDrive` or
`CanopenApplication` is constructed — fills profile-covered fields from the
generated `summary.json`: defaults follow the profile, explicit values win,
`sync_period_us` always follows the summary. Details:
[`canopen_motor_pipeline.md`](canopen_motor_pipeline.md).

Inside `MotorDriver` the configuration is immutable. Runtime *intent*
(`want_enabled_`, `want_hold_position_`, `selected_mode_`) is seeded from the
config at construction and updated by runtime requests — fault recovery
restores the intent, an explicit stop clears it, and a mode change updates
`selected_mode_` so a node that reboots gets the mode last selected.

## The cyclic engine

The PDO layout is loaded from the generated summary into a `config::PdoMap`;
nothing in the runtime hardcodes which objects are mapped.

- At construction the driver snapshots the active RxPDO objects into a
  `command_objects_` image and the active TxPDO objects into
  `feedback_objects_` (index, subindex, integer type).
- **Every `OnSync`**: read all feedback objects from the last received frames
  (no bus traffic), decode the known DS402 fields into `ds402::Feedback`, run
  at most one control sequence (below), then write the **whole command image**.
  A PDO is one frame — streaming every mapped object together keeps frames
  coherent and never zeroes a neighbour that shares a PDO.
- The cyclic path is allocation-free and takes one mutex once per cycle to
  publish the feedback + cadence snapshots.

Object access routing (`readObject`/`writeObject`, `DriveController`):

| Object | Read | Write |
| --- | --- | --- |
| mapped in an active TPDO | served from the cyclic cache | — |
| mapped in an active RxPDO | — | staged into the command image (next SYNC) |
| PDO-only command (0x6040, 0x607A, 0x60FF, 0x6071) but unmapped | — | dropped with a warning (SDO would abort) |
| anything else | blocking SDO upload | blocking SDO download |

## Boot & drive configuration

Lely's NMT boot invokes `OnConfig` while the node is **pre-operational** — the
only window this firmware reliably accepts SDO writes to PDO parameters. When a
cyclic action is requested the driver rewrites every active PDO from the
summary (disable via COB-ID valid bit → transmission type 1 → rewrite mapping
→ re-enable), disables unused RxPDOs, selects the operation mode over SDO
(0x6060), writes any profile parameters / vendor objects / ad-hoc
`object_writes`, and optionally persists to NVM (0x1010:03). `--inspect` stays
read-only.

`OnBoot` then runs the boot actions: the safe enable sequence
(`enableDrive`) primes the command image from the live drive (actual position,
persisted profile params), streams a few SYNC cycles, pulses a fault reset,
and walks `shutdown → switch on → enable operation`, confirming each
transition from cached feedback. While bringing a position mode up, the
commanded target **tracks the measured position** so the drive never sees a
step; it freezes once operation-enabled is reached.

## DS402 control sequences

All state transitions are driven **non-blocking from `OnSync`** by staging
controlwords into the command image. Exactly one controlword driver runs per
cycle, in priority order:

1. **Graceful stop** (`stop()` / watchdog): stream disable-voltage until the
   drive confirms switch-on-disabled, or force-finish at a deadline so a silent
   drive can never stay energised.
2. **Enable/fault-recovery ladder** (`resetFault()`, `enableOperation()`):
   fault-reset edge (bit 7) → shutdown → switch-on → enable-operation, each
   phase confirmed from feedback with a timeout; targets are zeroed and the
   position target glued to actual before re-energising.
3. **Homing** (below).
4. **Profile Position handshake** (`moveToPosition()`): pulse controlword
   bit 4 (+ change-immediately, optional relative), wait for statusword bit 12
   acknowledge, drop bit 4.

`quickStop()` is a priority action: it aborts in-flight sequences, zeroes the
motion targets, glues the position target, and streams the CiA402 quick-stop
controlword — the drive decelerates on its quick-stop ramp (0x6085/0x605A) and
holds energised. Recover with `enableOperation()` or `resetFault()`.

Runtime mode changes (`setOperationMode`) write 0x6060 over SDO, glue the
position target when entering a position mode, refuse while the axis is moving
(stationary threshold), and confirm against 0x6061.

## Safety

Three independent fault channels feed `ds402::Feedback`:

| Channel | Detects | Reaction |
| --- | --- | --- |
| cyclic TPDO staleness | drive stopped streaming (watchdog window `feedback_timeout`) | log once, de-energise, `feedbackLive()` = false |
| statusword / 0x603F in the TPDO | DS402 fault state, drive error code | logged edge, `faulted()` |
| EMCY (COB 0x80+id) | internally latched faults, error register, vendor bytes | folded into feedback, decoded via the manual's fault table |
| consumer heartbeat / node guarding | node loss independent of PDO cadence | `node_alive` = false, de-energise if running |

`feedbackLive()` is never latched: it is true only while fresh feedback keeps
arriving and the node is alive, so `enabled()` can never report a stale
"operation enabled". Fault decoding (`ds402::describeDeviceFault`,
`describeErrorRegister`, `describeAbortCode`) renders the manual's fault tables
as text everywhere a code is logged.

## Homing (hardstop-midpoint)

The RP firmware has no CiA402 homing mode; homing is implemented in software
from `OnSync`, in CSV:

```
SearchNegative → BackoffNegative → SearchPositive → MoveToCenter
   → WaitAtCenter → ZeroAtCenter → (RestoreDisable → RestoreMode → RestoreEnable)
```

- **Contact** = |torque| ≥ `threshold_torque` **while** |velocity| ≤
  `stopped_velocity` (a stalled axis, not a mid-travel torque transient). On
  contact the commanded velocity drops to zero immediately and the contact is
  confirmed over `contact_dwell`.
- The center is the midpoint of the two contacts plus `home_offset`; travel is
  validated against `min_travel`/`max_travel`.
- **ZeroAtCenter** writes the vendor set-zero object (0x2262) and optionally
  persists to NVM — blocking SDO from the cyclic path, protected by the
  in-flight guard and a catch that turns aborts into a homing failure.
- The routine snapshots the previous mode/enabled state and restores it when
  done. Fault/mode-drop/timeout at any phase fails the homing safely (zero
  velocity, drive stays energised).

Homing parameters come from the profile (`drive.config().homing`); callers
override per-application fields such as `home_offset` (see
`leg/examples/zero_leg.cpp` for the leg sign conventions).

## Real-time behaviour

The cadence is the master's SYNC (from the DCF, `CLOCK_MONOTONIC`); `RtConfig`
tunes the loop thread (SCHED_FIFO, optional CPU pinning, `mlockall` + stack
prefault), degrading gracefully without privileges. Achieved cadence is
measured per `OnSync` and published as `CyclicStats` (interval min/max/mean,
worst-case jitter vs the profile's nominal period).

Known caveats for hard-RT deployments: the default log sink writes to
stdout/stderr from the loop thread (install a non-blocking sink via
`log::setSink` for production), and the snapshot mutex has no priority
inheritance — both are bounded but nonzero jitter sources.
