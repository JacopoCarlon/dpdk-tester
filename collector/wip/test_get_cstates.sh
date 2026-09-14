#!/bin/bash

LATENCY_TEST_CORES="0,2,4,6"
cstates_file="pippo.txt"
power_file="pf.txt"
MEASUREMENT_DURATION=5

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RAPL_SCRIPT="${SCRIPT_DIR}/run_rapl.sh"

echo "getting cstate before rapl"
# Collect C-state counters before RAPL
#   -> this way we get the cstate of actual execution,
#      .. perfectly wrapping RAPL is not strictly needed.
{
    echo "C-state counters (before RAPL) Timestamp: $(date +%s)"
    for core in $(echo "$LATENCY_TEST_CORES" | tr ',' ' '); do
        cpu_dir="/sys/devices/system/cpu/cpu${core}/cpuidle"
        if [ -d "$cpu_dir" ]; then
            for state_dir in "$cpu_dir"/state*; do
                [ -d "$state_dir" ] || continue
                # Only proceed if the required files exist and are readable
                [ -r "$state_dir/name" ] && [ -r "$state_dir/usage" ] && [ -r "$state_dir/time" ] || continue
                state_name=$(cat "$state_dir/name")
                state_usage=$(cat "$state_dir/usage")
                state_time=$(cat "$state_dir/time")
                echo "cpu${core} $(basename $state_dir): name=${state_name} usage=${state_usage} time=${state_time}"
            done
        fi
    done
    echo ""
} > "$cstates_file"
echo "getting cstate before _ done"


echo "starting rapl"
# Start power measurement on Server A
echo "[$(date +%T)] Starting power measurement..."
$RAPL_SCRIPT -y -r -c $((MEASUREMENT_DURATION + 2)) -s 1 "$power_file"
echo "rapl finished"

echo "getting cstate after rapl"
# Collect C-state counters after RAPL
{
    echo "C-state counters (after RAPL) Timestamp: $(date +%s)"
    for core in $(echo "$LATENCY_TEST_CORES" | tr ',' ' '); do
        cpu_dir="/sys/devices/system/cpu/cpu${core}/cpuidle"
        if [ -d "$cpu_dir" ]; then
            for state_dir in "$cpu_dir"/state*; do
                [ -d "$state_dir" ] || continue
                [ -r "$state_dir/name" ] && [ -r "$state_dir/usage" ] && [ -r "$state_dir/time" ] || continue
                state_name=$(cat "$state_dir/name")
                state_usage=$(cat "$state_dir/usage")
                state_time=$(cat "$state_dir/time")
                echo "cpu${core} $(basename $state_dir): name=${state_name} usage=${state_usage} time=${state_time}"
            done
        fi
    done
    echo ""
} >> "$cstates_file"

echo "getting cstate after rapl _ done"

cat "$cstates_file"


