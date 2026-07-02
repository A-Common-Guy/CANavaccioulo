#!/usr/bin/env bash
# Sweep node ids 1..127 on a CANopen bus and report which ones reply.
# Sends an SDO read of 0x1000 (Device Type, mandatory on every CANopen
# device) to each id and listens for the response.
#
# Usage:
#   tools/scan_nodes.sh <iface> [--max N] [--settle MS]
#
# Options:
#   --max N       highest node id to probe (default: 127)
#   --settle MS   how long to listen for straggler replies after the last
#                 request is sent (default: 400)
#
# Requires: can-utils (cansend, candump) on an UP interface, e.g.
#   sudo ip link set can0 up type can bitrate 1000000

set -euo pipefail

die() { echo "[error] $*" >&2; exit 1; }

IFACE=""
MAX_ID=127
SETTLE_MS=400

while [[ $# -gt 0 ]]; do
    case "$1" in
        --max) MAX_ID="${2:?}"; shift 2 ;;
        --settle) SETTLE_MS="${2:?}"; shift 2 ;;
        -h | --help) sed -n '2,16p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) die "unknown option: $1 (try --help)" ;;
        *) [[ -z "${IFACE}" ]] && IFACE="$1" || die "unexpected arg: $1"; shift ;;
    esac
done

[[ -n "${IFACE}" ]] || die "missing <iface> (try --help)"
[[ "${MAX_ID}" =~ ^[0-9]+$ ]] && ((10#${MAX_ID} >= 1 && 10#${MAX_ID} <= 127)) || die "--max must be 1..127"
command -v cansend >/dev/null || die "cansend not found (install can-utils)"
command -v candump >/dev/null || die "candump not found (install can-utils)"
ip link show "${IFACE}" >/dev/null 2>&1 || die "interface '${IFACE}' not found"

echo "Sweeping ids 1..${MAX_ID} on ${IFACE} (SDO read 0x1000)..."

TMP="$(mktemp)"
trap 'rm -f "${TMP}"' EXIT

# Listen for any SDO response (0x580..0x5FF) while we blast all the requests.
candump "${IFACE},580:780" </dev/null >"${TMP}" 2>/dev/null &
CAP_PID=$!
sleep 0.15

for ((id = 1; id <= 10#${MAX_ID}; id++)); do
    req_cob="$(printf '%03X' "$((0x600 + id))")"
    cansend "${IFACE}" "${req_cob}#4000100000000000"
done

sleep "$(awk "BEGIN{print ${SETTLE_MS}/1000}")"
kill "${CAP_PID}" 2>/dev/null || true
for _ in 1 2 3 4 5; do
    kill -0 "${CAP_PID}" 2>/dev/null || break
    sleep 0.1
done
kill -9 "${CAP_PID}" 2>/dev/null || true
wait "${CAP_PID}" 2>/dev/null || true

echo
echo "=== Nodes detected on ${IFACE} ==="
found=0
for ((id = 1; id <= 10#${MAX_ID}; id++)); do
    resp_cob="$(printf '%03X' "$((0x580 + id))")"
    if grep -qiE "[[:space:]]${resp_cob}[[:space:]]" "${TMP}"; then
        printf "  node %-3d (0x%02X)  via SDO\n" "${id}" "${id}"
        found=$((found + 1))
    fi
done

echo
echo "${found} node(s) found."
