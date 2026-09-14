#!/bin/bash

LATENCY_TEST_CORES="0,2,4,6"
cstates_file="pippo.txt"
power_file="pf.txt"
MEASUREMENT_DURATION=5

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RAPL_SCRIPT="${SCRIPT_DIR}/run_rapl.sh"

# Start RAPL in the background, 
#   in // run turbostat.
echo "starting rapl"
echo "[$(date +%T)] Starting RAPL & turbostat..."
$RAPL_SCRIPT -y -r -c $((MEASUREMENT_DURATION + 2)) -s 1 "$power_file" &
RAPL_PID=$!
echo "starting rapl _ done"


# 
echo "getting cstate for some time _ done"
sudo turbostat --quiet sleep $MEASUREMENT_DURATION > "$cstates_file"
echo "getting cstate for some time _ done"

# Wait for RAPL to finish 
echo "waiting rapl"
wait $RAPL_PID
echo "[$(date +%T)] RAPL and turbostat finished."

echo "rapl has (in fact) finished"

cat "$cstates_file"




