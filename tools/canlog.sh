#!/usr/bin/env bash
# Capture CAN traffic to a file instead of flooding the terminal.
#
# Usage:
#   tools/canlog.sh [iface] [logfile] [id-filters...]
#
# Examples:
#   tools/canlog.sh                       # can0 -> logs/can-<ts>.log, all frames
#   tools/canlog.sh can0 /tmp/boot.log    # explicit iface + file
#   tools/canlog.sh can0 /tmp/boot.log 181:7FF 201:7FF   # only statusword + RPDO1
#
# Stop with: pkill -x candump   (or Ctrl-C if run in the foreground)

set -euo pipefail

iface="${1:-can0}"
logfile="${2:-}"
shift "$(( $# < 2 ? $# : 2 ))" || true

if [[ -z "${logfile}" ]]; then
    mkdir -p logs
    logfile="logs/can-$(date +%Y%m%d-%H%M%S).log"
fi

filters=()
if [[ $# -gt 0 ]]; then
    for f in "$@"; do
        filters+=("${iface},${f}")
    done
else
    filters+=("${iface}")
fi

echo "Logging ${iface} -> ${logfile}"
echo "Filters: ${filters[*]}"
echo "Stop with: pkill -x candump"

# -tz: timestamps relative to the first frame; keeps the file easy to read.
exec candump -tz "${filters[@]}" >>"${logfile}" 2>&1
