# EYou / EuServo RP drive notes

Drive-specific behaviour of the EYou RP series (the actuators in the leg
joints), collected from the vendor manual ("RP Series CANopen Communication
Manual" V1.01, `docs_motor/`), the vendor EDS (`eds/EDS files/`), and bench
observation. Facts marked **[empirical]** were observed on real hardware and
are *not* in the manual — re-verify them on firmware updates.

## Units & scaling

| Quantity | Unit | Notes |
| --- | --- | --- |
| position (0x6064 / 0x607A) | counts | 524288 counts/rev at the output shaft (19-bit) |
| gear ratio (RP90L) | 21.913 | motor → output reduction; profile `runtime.gear_ratio` |
| velocity (0x606C / 0x60FF) | counts/s | |
| acceleration (0x6083/0x6084/0x6085) | counts/s² | |
| torque (0x6071 / 0x6077 / 0x6072) | per-mille of **rated current** | not rated torque |
| torque limits 0x60E0/0x60E1 | mA | inconsistent with the per-mille objects — beware |

## Boot & configuration behaviour

- **TxPDOs come out of NVM disabled** (COB-ID bit 31 set) and the reported
  transmission types/mappings can be stale **[empirical]** — the drive never
  streams cyclic feedback until reconfigured. The runtime therefore rewrites
  every active PDO during the NMT "update configuration" step, while the node
  is **pre-operational** (the manual's own examples configure PDOs between
  Reset Node and NMT Start; several PDO subindices are marked RO in the object
  tables but writable in the examples — the examples are authoritative).
- **Power-up**: the manual (3.1) requires **> 5 s** after power-on before
  attempting to enable. Enabling earlier fails in confusing ways.
- **Persistence**: write `0x65766173` ("save") to 0x1010:03 to store
  application parameters; do not power off during the save. Some faults (e.g.
  0xFF24) explicitly demand save + power-cycle.

## PDO-only command objects

SDO downloads to **0x6040 (controlword), 0x607A, 0x60FF, 0x6071** abort with
vendor code `0x00000002` on this firmware **[empirical]** — the code does not
appear in the manual (the closest standard abort is 0x06010004 "may be PDO
mapped only"). These objects must ride in RxPDOs; the runtime stages writes to
them into the cyclic image and warns if they are not mapped.

Additionally, streaming **0x6060 in an RxPDO makes the firmware discard the
whole frame** **[empirical]** — with the mode object mapped, the drive ignores
every controlword and sticks in *ready to switch on*. This is why the profile
uses the **cyclic superset** layout (mode kept out of the PDOs, selected over
SDO at boot):

- RxPDO1 (0x200+id): controlword + target position
- RxPDO2 (0x300+id): target velocity + target torque
- TPDO1 (0x180+id): statusword + mode display + torque actual
- TPDO2 (0x280+id): position actual + velocity actual

The drive obeys only the target matching its active mode and ignores the
others, so all targets can ride the bus every cycle.

## Operation modes

Supported (0x6060): PP=1, PV=3, PT=4, CSP=8, CSV=9, CST=10, and a vendor MIT
mode (11, manual 3.8 — 64-bit packed command/reply in 0x2130..0x2133; **not
implemented** in stableCOPS, and the manual's bit-packing tables contain
typos). Notable:

- **No CiA402 homing mode (6)** and no 0x6098 homing-method object. The only
  homing primitive is vendor object **0x2262 = 1** ("current position becomes
  zero"). stableCOPS implements hardstop-midpoint homing in software on top of
  it ([architecture.md](architecture.md)).
- **Mode changes** are only supported in the *enabled, stationary* state
  (manual 3.3) — or over SDO while pre-operational at boot. Before switching
  into a position mode, copy 0x6064 into 0x607A (the runtime glues the target
  automatically). Statusword bit 10 also pulses on a mode change — don't read
  it as "target reached".
- 0x6502 (supported drive modes) has no documented value; don't validate
  against it.

## SYNC & timing

- The profile runs SYNC at **2000 µs** (`master.sync_period`, DCF 0x1006). A
  command-cycle mismatch raises fault **E404 / 0xFF34** ("interpolation cycle
  not supported"). The manual never documents 0x60C2 usage; cycle matching is
  governed by the actual SYNC period.
- Heartbeat: the drive's producer default is 1000 ms; the profile sets 100 ms
  per node with a 3× consumer multiplier, so the master flags node loss after
  ~300 ms independently of PDO traffic.

## Faults & diagnostics

- Fault codes share one space across **0x603F** (streamed in TPDO), the **EMCY
  error code** (COB 0x80+id, bytes 0-1 = code, byte 2 = error register), and
  the **0x1003** history array. `ds402::describeDeviceFault` implements the
  manual's Table 4-2 plus the codes documented only in 4.2's per-fault pages
  (secondary-encoder faults 0x7511/0x7513/0x7514, E404/E405).
- Error register 0x1001 is **not PDO-mappable** — poll it over SDO (inspection
  does) or read it from EMCY byte 2.
- Fault reset is a **rising edge** on controlword bit 7. EEPROM/parameter-class
  faults (0x6320..0x6323, 0xFF03/0xFF10/0xFF24, 0xFF11) generally need factory
  reset / power-cycle, not a bit-7 reset.
- The manual does **not** document an EMCY 0x0000 "no error" frame; the runtime
  treats one as the standard error-reset notification if it arrives.

## Undocumented areas (validated on the bench only)

- Option codes 0x605A..0x605E (quick stop / shutdown / disable / halt / fault
  reaction): listed with **no values and no defaults**. `quickStop()` holding
  in quick-stop-active assumes 0x605A ∈ 5..8 — verify per drive.
- Vendor disable block 0x2103 (disable mode) and 0x2104..0x2108 (active
  disable): listed in the object dictionary with **no value documentation**.
  0x2103 selects coast vs short-circuit braking on disable **[empirical]**.
- 0x2219/0x221A (second-feedback config) exist in the EDS but not in the
  manual; 0x2772 is the second encoder's single-turn value (its English name in
  the manual, "Multi Turn", is a mistranslation).
- Node-id and bit-rate assignment are not covered by the CANopen manual at all
  (see `tools/set_can_id.sh`).

## Wire-level debugging

When a transition stalls, confirm what is on the bus before changing code:

```bash
candump -tz can0,201:7FF can0,181:7FF can0,281:7FF can0,080:7FF
```

Healthy bring-up (node 1):

- `0x080` SYNC at the configured period (2 ms for the RP profile).
- `0x201` (RxPDO1) 6 bytes: controlword walking `06 00 → 07 00 → 0F 00`,
  target position (bytes 2-5) tracking the measured position, then frozen.
- `0x181` statusword tracking `31 06 → 33 06 → 37 06`; error code stays 0.

Checks:

```bash
# Prove a target object is PDO-only: SDO download aborts with 0x00000002
cansend can0 601#2360 7A00 00000000 && candump -tz can0,581:7FF

# After bring-up, --inspect should show every active PDO with transmission
# type 0x01 and the expected mapped-object counts:
build/stablecops_master --can can0 --node 1 --inspect --run
```

If the drive never emits TPDOs or ignores RPDOs, the cause is almost always in
its PDO configuration: COB-ID valid bit still set, a COB-ID mismatch, or a
non-cyclic transmission type — all visible in the `--inspect` dump. If
SocketCAN reports `CAN transmit queue full`, re-run `canup.sh` (sets
`txqueuelen 1000`) and reduce bus load before adding nodes.
