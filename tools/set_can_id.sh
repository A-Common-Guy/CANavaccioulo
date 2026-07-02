#!/usr/bin/env bash
# Change and permanently persist an EYou/Yiyou RP-series drive's CANopen
# node id. This is the confirmed-working recipe -- no A/B testing needed.
#
# The two things about this recipe that look unusual are deliberate, not bugs:
#
#   1) It writes the id to 0x204C ("Addr", UINT16 -- documented in the EDS),
#      not the undocumented 0x2001:01 that the vendor's own PC tool uses.
#
#   2) The NV-save command written to 0x1010:01 uses the byte sequence
#      65 76 61 73, NOT the CiA-301-standard "save" signature (which would be
#      73 61 76 65, decoding to 0x65766173 as documented in the vendor's own
#      manual). This drive's firmware apparently compares that signature
#      byte-reversed, so the value that actually works out to 0x73617665.
#      Confirmed working with EYou support on 2026-07-02. If this stops
#      working on a different unit/firmware revision, try the standard
#      73 61 76 65 signature instead.
#
# Run with EXACTLY ONE drive on the bus (it's addressed via its current id).
#
# Usage:
#   tools/set_can_id.sh <iface> <current_id> <new_id>
#
# Example:
#   tools/set_can_id.sh can0 1 2
#
# Requires: can-utils (cansend, candump) on an UP interface, e.g.
#   sudo ip link set can0 up type can bitrate 1000000

set -euo pipefail

die() { echo "[error] $*" >&2; exit 1; }

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" || $# -ne 3 ]]; then
    sed -n '2,28p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit "$([[ "${1:-}" == "-h" || "${1:-}" == "--help" ]] && echo 0 || echo 1)"
fi

IFACE="$1" CURRENT_ID="$2" NEW_ID="$3"

is_node_id() { [[ "$1" =~ ^[0-9]+$ ]] && ((10#$1 >= 1 && 10#$1 <= 127)); }
is_node_id "${CURRENT_ID}" || die "current id must be 1..127 (got '${CURRENT_ID}')"
is_node_id "${NEW_ID}" || die "new id must be 1..127 (got '${NEW_ID}')"
command -v cansend >/dev/null || die "cansend not found (install can-utils)"
command -v candump >/dev/null || die "candump not found (install can-utils)"
ip link show "${IFACE}" >/dev/null 2>&1 || die "interface '${IFACE}' not found"

req_cob() { printf '%03X' "$((0x600 + 10#$1))"; }
resp_cob() { printf '%03X' "$((0x580 + 10#$1))"; }

REPLY_BYTES=""
# sdo <node_id> <8-byte-hex> -> sets REPLY_BYTES, returns 1 on no reply
sdo() {
    local nid="$1" hex="$2" req resp tmp cp line
    req="$(req_cob "${nid}")"; resp="$(resp_cob "${nid}")"; tmp="$(mktemp)"
    candump "${IFACE},${resp}:7FF" >"${tmp}" 2>/dev/null & cp=$!
    sleep 0.15
    cansend "${IFACE}" "${req}#${hex}"
    sleep 0.45
    kill "${cp}" 2>/dev/null || true; wait "${cp}" 2>/dev/null || true
    line="$(grep -iE "[[:space:]]${resp}[[:space:]]" "${tmp}" | tail -n1 || true)"
    rm -f "${tmp}"
    if [[ -z "${line}" ]]; then REPLY_BYTES=""; return 1; fi
    REPLY_BYTES="$(sed -E 's/.*\][[:space:]]+//' <<<"${line}" | tr 'a-f' 'A-F')"
    return 0
}

read_addr() {
    sdo "$1" "404C200000000000" || return 1
    read -r -a B <<<"${REPLY_BYTES}"
    [[ "${B[0]:0:1}" == "4" ]] || return 1
    echo "$((16#${B[5]}${B[4]}))"
}

echo "== Current 0x204C (Addr) on node ${CURRENT_ID} =="
before="$(read_addr "${CURRENT_ID}")" || die "no reply from node ${CURRENT_ID} on ${IFACE} -- check wiring/bitrate/id"
echo "  = ${before}"

echo "== Entering pre-operational (NMT 0x80 -> ${CURRENT_ID}) =="
cansend "${IFACE}" "000#80$(printf '%02X' "$((10#${CURRENT_ID}))")"
sleep 0.5

new_hex="$(printf '%02X' "$((10#${NEW_ID}))")"
echo "== Writing 0x204C = ${NEW_ID} =="
sdo "${CURRENT_ID}" "2B4C2000${new_hex}000000" || die "no reply to write"
[[ "${REPLY_BYTES:0:2}" == "60" ]] || die "write not acknowledged: ${REPLY_BYTES}"

echo "== Saving to non-volatile memory (0x1010:01) =="
sdo "${CURRENT_ID}" "2310100165766173" || die "no reply to save command"
[[ "${REPLY_BYTES:0:2}" == "60" ]] || die "save not acknowledged: ${REPLY_BYTES}"

echo
echo "Written and saved. Now physically POWER-CYCLE the drive"
echo "(remove power a few seconds, then reapply)."
read -r -p "Press Enter once you've done that and the drive has powered back up... "

echo
echo "== Verifying on new id ${NEW_ID} =="
ok=0
for _ in 1 2 3 4 5; do
    if val="$(read_addr "${NEW_ID}")"; then
        ok=1
        break
    fi
    sleep 1
done

if [[ "${ok}" -eq 1 ]]; then
    echo "SUCCESS: node ${NEW_ID} answers, 0x204C = ${val}."
else
    echo "FAILED: no reply on node ${NEW_ID}."
    echo "Checking if it's still on the old id ${CURRENT_ID}..."
    if old_val="$(read_addr "${CURRENT_ID}")"; then
        echo "  still at id ${CURRENT_ID} (0x204C = ${old_val}) -- the change did not take."
    else
        echo "  no reply at ${CURRENT_ID} either -- check power/wiring."
    fi
    exit 1
fi
