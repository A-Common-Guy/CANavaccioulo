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

## Layout

- `eds/EDS files/`: immutable vendor EDS files.
- `config/motors/`: declarative motor profiles.
- `tools/generate_canopen_config.py`: EDS normalization, dcfgen YAML emission, DCF generation, and validation.
- `generated/canopen/`: derived EDS/YAML/summary artifacts.
- `dcf/master.dcf`: generated runtime DCF.
- `include/stablecops/ds402/` and `src/ds402/`: generic DS402 objects, state decoding, and commands.
- `include/stablecops/lely/` and `src/lely/`: Lely adapter and boot callbacks.
