ip link set can0 type can bitrate 1000000
# Multi-drive commissioning can burst several PDOs per SYNC. A larger TX queue
# absorbs short scheduling bursts; if it still overflows, slow the SYNC period or
# reduce the mapped PDO traffic.
ip link set can0 txqueuelen 1000
ip link set up can0
