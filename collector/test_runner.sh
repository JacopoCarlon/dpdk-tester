#!/bin/bash
# test_runner.sh - Sweep DPDK parameters across a list of traffic patterns
set -euo pipefail

AUTO_SCRIPT="./exp_auto_grid.sh"
FREQ_SCRIPT="../j_usersp_optCstate_noTurbo.sh"
SLEEP_BETWEEN=15               # seconds between individual runs
WRAPPER_LOG="wrapper_$(date +%Y%m%d_%H%M%S).log"

############################################################
# 1. Define the traffic configurations you want to test
#    Format: quoted string containing all flags EXCEPT
#    -t (type) and the tunable DPDK parameters (-m, -M, -g, -B, -q).
#    Include -p, -s, -b, -n, and any pattern‑specific flags.
############################################################
# sudo ./latency_test -l 2,4,6 -- -B 32 -s 256 -p tlogn -T -16.812 0.336 -9.904 0.336 -14.509 1.386


TRAFFICS_OLD=(
    # Example 1: TLOGN with name "turbo_tuned_tlogn"
    # "-p tlogn -s 256 -b 256 -w -3.5 -x 0.1 -y -3.5 -z 0.1 -W -9.9 -X 0.1 -n turbo_tuned_tlogn"   
    "-p tlogn -s 256 -b 256 -w -3.5 -x 0.1 -y -3.5 -z 0.1 -W -9.9 -X 0.1 -n turbo_tuned_tlogn"

    # Example 2: ON/OFF with name "onoff_lowload"
    ## "-p onoff -s 512 -b 32 -U 0.01 -N 0.02 -R 1500000 -n onoff_lowload"

    # Example 3: Uniform baseline (you can override rates via -r if needed)
    ## "-p uniform -s 256 -r 2440000 -b 32 -n uniform_baseline"
)


TRAFFICS=(
    # Uniform
    "-p uniform -s 256 -b 32 -r 2440000 -n uniform_baseline"

    # TLOGN
    # circa 5Gbps
    "-p tlogn -s 256 -b 32 -w -3.5 -x 0.1 -y -3.5 -z 0.1 -W -9.9 -X 0.1 -n tlogn5G"
    
    # 30Gbps
    # "-p tlogn -s 256 -b 256 -w -16.812 -x 0.336 -y -9.904 -z 0.336 -W -14.509 -X 0.386 -n tlogn33G"

    # multipleExpLogn
    '-p multipleExpLogn -s 256 -b 32 -A "350 0.006 14.45 0.35 100 5000000 25000000000" -n expLogn'

    # Web
    '-p web -s 256 -b 32 -A "1000000 8.37 1.37 100 2000000 6.17 2.36 50 2000000 1.1 2.0 55.0 7.69 0.033" -n webTrf'
)

############################################################
# 2. Define the DPDK experiments (type + optional tuning flags), should be aligned with exp-auto_grid.sh input parsing !!!
##  ##  ##  usage() {
##  ##  ##      cat <<EOF
##  ##  ##  Usage: $0 [options]
##  ##  ##  
##  ##  ##  Pattern/size options:
##  ##  ##    -t TYPE       DPDK mode (baseline|interrupt-only|pause|hybrid)
##  ##  ##    -s SIZE       Packet size(s)
##  ##  ##    -r RATE       Rate(s)
##  ##  ##    -p PATTERN    Traffic pattern
##  ##  ##    -b BURST      Burst size
##  ##  ##    -A ARGS       Extra args for custom patterns (web/gaming/multipleExpLogn)
##  ##  ##    -n NAME       Experiment name suffix
##  ##  ##  
##  ##  ##  Tunable DPDK parameters:
##  ##  ##    -m VALUE      minConsEmpty            (default: $minConsEmpty)
##  ##  ##    -M VALUE      maxIntTimeout           (default: $maxIntTimeout in milliseconds!!!)
##  ##  ##    -g VALUE      gracePollCount          (default: $gracePollCount)
##  ##  ##    -B VALUE      baseline_pause_duration (default: $baseline_pause_duration)
##  ##  ##    -q VALUE      pause_duration          (default: $pause_duration)
##  ##  ##  
##  ##  ##  Pattern-specific options:
##  ##  ##    -T PAUSE_TIME        Burst pause time
##  ##  ##    -S/-E/-d/-C          Sawtooth: start/end/duration/steps
##  ##  ##    -U/-N/-R             ON/OFF: ton/toff/rate
##  ##  ##    -L/-K                Poisson: lambda/burst
##  ##  ##    -D/-F/-G/-I          DC: base/burst/duration/interval
##  ##  ##    -v/-w/-x/-y/-z       LogNormal: rate/on_mu/on_sigma/off_mu/off_sigma
##  ##  ##    -W/-X                TLOGN: rate_mu/rate_sigma
##  ##  ##    -P PARAMS            Generic extra pattern params
##  ##  ##  
##  ##  ##    -h            Show this help
##  ##  ##  EOF
##  ##  ##      exit 0
##  ##  ##  }
############################################################
EXPERIMENTS=(
    # Baseline - vary baseline_pause_duration (nanoseconds)
    "baseline -B 1"
    "baseline -B 30"
    "baseline -B 50"
    "baseline -B 100"
    "baseline -B 200"
    "baseline -B 300"
    "baseline -B 500"
    "baseline -B 700"
    "baseline -B 1000"
    "baseline -B 2000"
    # Pause - vary pause_duration (nanoseconds)
    "pause -q 1"
    "pause -q 2"
    "pause -q 3"
    "pause -q 10"
    "pause -q 30"
    "pause -q 50"
    "pause -q 100"
    "pause -q 200"
    "pause -q 300"
    "pause -q 500"
    "pause -q 1000"
    "pause -q 1500"
    "pause -q 2000"   
    # Interrupt-only (no extra flags)
    "interrupt-only"
    # Hybrid - vary minConsEmpty
    "hybrid -m 1000 -M 10 -g 1000"
    "hybrid -m 10000 -M 10 -g 1000"
    # Hybrid - vary maxIntTimeout (microseconds)
    "hybrid -m 10000 -M 50 -g 1000"
    "hybrid -m 10000 -M 100 -g 1000"
    "hybrid -m 10000 -M 1000 -g 1000"
    # Hybrid - vary gracePollCount
    "hybrid -m 10000 -M 10 -g 1000"
    "hybrid -m 10000 -M 10 -g 2000"
    "hybrid -m 10000 -M 10 -g 5000"
)

## | Mode                     | Parameter                       | Unit                          | Default              |
## |--------------------------|---------------------------------|-------------------------------|----------------------|
## | Hybrid short sleep       | --max/min-small-sleep           | µs                            | 10–50 µs             |
## | Hybrid interrupt timeout | --max-interrupt-timeout         | µs                            | 300 µs               |
## | Interrupt-only           | (not configurable)              | µs                            | 1 or 300 µs hardcoded|
## | PMD pause (clb_pause)    | --pause-duration                | ns (modified from upstream)   | 200 ns               |
## | Baseline pause           | --busypolling_pause_duration_ns | ns                            | 50 ns                |

EXPERIMENTS_SMOL=(
    # Baseline - vary baseline_pause_duration
    "baseline -B 30"
    "baseline -B 50"
)

############################################################
# 3. Main loop
############################################################
exec > >(tee -a "$WRAPPER_LOG") 2>&1
echo "========== Parameter sweep started at $(date) =========="


TARGET_FREQUENCIES=(1200000 1400000 1600000 1800000 2000000 2200000 2400000)
TARGET_FREQUENCIES_SMOL=(1200000 1400000)


for target_freq in "${TARGET_FREQUENCIES[@]}"; do
    sudo $FREQ_SCRIPT --enable-cstates $target_freq $target_freq

    echo "=== === === === === === ==="
    echo "=== === === === === === ==="
    echo "=== === === === === === ==="
    echo "=== === === === === === ==="
    echo "=== === === === === === ==="
    echo "Starting iterating on userspace governor with frequency $target_freq"

    for traffic in "${TRAFFICS[@]}"; do
        # Extract the traffic's own name from the -n flag (e.g., "turbo_tuned_tlogn")
        traffic_name=""
        if [[ "$traffic" =~ -n[[:space:]]+([^[:space:]]+) ]]; then
            traffic_name="${BASH_REMATCH[1]}"
            # Remove the original -n <name> to avoid conflict
            traffic=$(echo "$traffic" | sed -E 's/-n[[:space:]]+[^[:space:]]+//')
        fi
        # Fallback if no name was given
        [[ -z "$traffic_name" ]] && traffic_name="traffic"

        echo "--- --- --- --- --- --- --- --- ---"
        echo "--- --- --- --- --- --- --- --- ---"
        echo "--- --- --- --- --- --- --- --- ---"
        echo "--- --- --- --- --- --- --- --- ---"
        echo "--- --- --- --- --- --- --- --- ---"
        echo "Starting iterating on traffic $traffic"

        for exp in "${EXPERIMENTS[@]}"; do
            read -r type extra_flags <<< "$exp"

            # Build a suffix that includes both traffic name and frequency
            suffix="${traffic_name}_freq${target_freq}"

            echo "----------------------------------------------------------------"
            echo "Traffic : $traffic (name: $traffic_name)"
            echo "Experiment: type=$type, flags=$extra_flags"
            echo "Suffix: $suffix"
            echo "----------------------------------------------------------------"

            echo "---"
            echo "killing any leftover <dpdk-l3fwd-power> instance on server A before starting a new one"
            # Kill any leftover instance before starting a new one
            sudo pkill -TERM -f "dpdk-l3fwd-power" 2>/dev/null || true
            sleep 5   
            sudo killall -9 dpdk-l3fwd-power 2>/dev/null || true
            sleep 5 
            sudo rm -rf /var/run/dpdk/* /dev/shm/dpdk* 2>/dev/null || true
            sleep 3

            # Construct and execute the command, adding -n $suffix
            cmd="$AUTO_SCRIPT $traffic -t $type $extra_flags -n $suffix"
            eval "$cmd"   # eval is safe here; variables are controlled

            echo "Finished. Sleeping $SLEEP_BETWEEN seconds..."
            sleep "$SLEEP_BETWEEN"
            echo "Finished sleeping ."
        done
    done
    echo "Finished iterating on userspace governor with frequency $target_freq"
done


echo "========== Parameter sweep completed at $(date) =========="