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
code should not hardcode RPDO/TPDO map indexes or vendor-specific boot writes.

`stablecops::ds402::DriveController` works in DS402 object terms: controlword,
statusword, operation mode, target position, velocity, and torque. SDO remains
the fallback for configuration and diagnostics.

Add explicit PDO remapping to a profile only when the vendor default layout is
insufficient and the drive documentation confirms the remap sequence.
