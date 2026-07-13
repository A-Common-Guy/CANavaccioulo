# Motor profiles & the CANopen generation pipeline

One rule: **vendor data goes in, generated artifacts come out**. The motor
profile YAML is the single source of truth for the PDO layout, the SYNC period,
and the actuator's runtime settings; the vendor EDS is never edited; generated
files are never hand-edited. Runtime code stays generic — it loads whatever the
profile generated.

```
config/motors/<name>.yml      profile (edit this)
eds/EDS files/<vendor>.eds    immutable vendor EDS
        │  tools/generate_canopen_config.py
        ▼
generated/canopen/<name>/     normalized EDS, dcfgen YAML, summary.json
dcf/master.dcf                the DCF the Lely master loads
        │  loaded by path at boot (MotorConfig)
        ▼
PdoMap (cyclic layout)  +  resolveMotorConfig (runtime settings)
```

## Onboard a motor

1. Copy the vendor EDS into `eds/EDS files/` and leave it unchanged.
2. Add a profile under `config/motors/` (start from `euservo_rp.yml`).
3. Generate: `python3 tools/generate_canopen_config.py --profile config/motors/<name>.yml`
4. Bring up SocketCAN (`sudo ./canup.sh`) and inspect:
   `build/stablecops_master --can can0 --node 1 --inspect --run`
5. Enable motion only after the live identity, DS402 state, supported modes,
   and PDO configuration match expectations.

## Profile reference

```yaml
name: euservo_rp                       # names the generated artifacts
vendor_eds: eds/EDS files/....eds      # immutable input

master:                                # -> dcfgen master section
  node_id: 127
  baudrate: 1000
  sync_period: 2000                    # us; also recorded as summary sync_period_us
  heartbeat_consumer: true             # master monitors each node's heartbeat
  heartbeat_multiplier: 3.0            # consumer timeout = producer * multiplier
  boot_time: 5000

nodes:                                 # one entry per drive on the chain
  - { name: rp_joint_1, node_id: 1, mandatory: true, boot: true, heartbeat_producer: 100 }
  - { name: rp_joint_2, node_id: 2, mandatory: false, boot: true, heartbeat_producer: 100 }
# (a single drive may use `node:` with one mapping instead)

generation:
  generated_dir: generated/canopen/euservo_rp
  dcf_dir: dcf                         # NOTE: shared output dir, see Regenerating
  no_strict: true

identity_policy: ignore                # zero out 0x1018 identity in the normalized EDS
pdo_policy: cyclic-superset            # informational tag recorded in the summary
mode_policy: sdo-at-boot

eds_overrides:                         # bend the vendor PDO layout, per EDS section
  "1600sub2": { DefaultValue: "0x607A0020" }
  # ...

runtime:                               # actuator runtime settings (see below)
  counts_per_rev: 524288
  feedback_timeout_ms: 100
  state_transition_timeout_ms: 2000
  max_position_step: 10000
  homing:
    search_velocity: 25000
    approach_velocity: 24000
    center_velocity: 24000
    center_final_velocity: 4000
    center_slowdown_distance: 1000
    backoff_distance: 2000
    center_tolerance: 50
    center_settle_tolerance: 200
    min_travel: 1000
    max_travel: 2000000
    threshold_torque: 90
    stopped_velocity: 200
    contact_dwell_ms: 20
    settle_time_ms: 200
    timeout_ms: 30000
    save_zero_to_nvm: true
```

- **`nodes`** — all nodes share the EDS and PDO layout, differing only by node
  id; `dcfgen` emits one slave section per node and builds the master's
  RPDO/TPDO image for each. Every node's `heartbeat_producer` (0x1017) is what
  lets the master detect its loss independently of PDO traffic.
- **`eds_overrides`** — rewrites `[section] key=value` pairs in the *normalized*
  EDS, so a profile can impose its PDO layout without touching the vendor file.
  The euservo_rp profile uses this for the **cyclic superset** layout: RxPDO1 =
  controlword + target position, RxPDO2 = target velocity + target torque,
  RxPDO3 disabled — one fixed map serves CSP/CSV/CST, with the active mode
  selected over SDO (0x6060) at boot. Rationale and drive constraints:
  [`eyou_rp_notes.md`](eyou_rp_notes.md).
- **`runtime`** — copied verbatim into the summary and applied to `MotorConfig`
  at runtime (below). Tune actuator behaviour here, not in C++ defaults.

## Generated artifacts

For profile `<name>` the generator writes:

| File | Purpose |
| --- | --- |
| `generated/canopen/<name>/<name>.normalized.eds` | vendor EDS + normalization + `eds_overrides`; input to dcfgen, browsable in the commissioning UI |
| `generated/canopen/<name>/<name>.dcfgen.yml` | dcfgen input built from the profile |
| `generated/canopen/<name>/<name>.summary.json` | everything the **runtime** loads (below) |
| `dcf/master.dcf` | the DCF the Lely master loads |

`summary.json` records: `node_id`/`node_ids`, `master_node_id`,
**`sync_period_us`**, the policies, **`pdo_mappings`** (each PDO's
communication/mapping index, COB-ID with node-relative metadata, transmission
type, and mapped objects), and the profile's **`runtime`** section verbatim.
The runtime and `dcf/master.dcf` are generated from the same profile, so both
ends of the bus stay coherent by construction.

## Runtime resolution

At boot the runtime loads the summary twice over:

- `config::loadPdoMapFromSummary(path, node_id)` → the cyclic PDO layout, with
  COB-IDs rebased per node for homogeneous chains.
- `config::resolveMotorConfig(config)` — applied automatically when a
  `MotorDrive` or `CanopenApplication` is constructed — fills the config from
  `sync_period_us` and `runtime`:
  - a field left at its **built-in default** takes the profile's value;
  - a field set explicitly (code, CLI flag) **wins**;
  - `sync_period_us` **always** follows the summary, because it must match the
    DCF's SYNC period (an explicit mismatch logs a warning).

So the precedence chain is: *built-in default < profile < explicit override* —
and the profile is where actuators get tuned.

## Regenerating

```bash
python3 tools/generate_canopen_config.py --profile config/motors/euservo_rp.yml
```

- Requires `python3-dcf-tools` (`dcfgen`, `dcfchk`) and PyYAML; `--skip-tools`
  writes the EDS/YAML/summary without running dcfgen.
- **`dcf/master.dcf` is a shared output**: profiles that set `dcf_dir: dcf`
  overwrite it. Regenerate the profile you actually run on the bus last, or use
  `--skip-tools` for the others (e.g. `eyou_phu` when the RP profile owns the
  DCF).
- Never hand-edit generated files — change the vendor EDS/profile and
  regenerate.
