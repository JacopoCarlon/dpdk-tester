#!/bin/bash
# Usage: ./rapl_logger.sh <iterations> <tmpfile> <interval>

ITERATIONS=$1
TMPFILE=$2
INTERVAL=$3
SOCKET="0"

# Clear previous temporary file
> "$TMPFILE"

for ((i=0; i<ITERATIONS; i++)); do
    TIMESTAMP=$(date +%s.%N)
    ENERGY_PKG=$(sudo cat /sys/class/powercap/intel-rapl:${SOCKET}/energy_uj)
    ENERGY_DRAM=$(sudo cat /sys/class/powercap/intel-rapl:${SOCKET}/intel-rapl:${SOCKET}:0/energy_uj)
    echo "$TIMESTAMP $ENERGY_PKG $ENERGY_DRAM" >> "$TMPFILE"
    sleep "$INTERVAL"
done
