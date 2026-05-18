#!/bin/bash
# automated_measurements.sh - Complete measurement automation script for Server A

####################
# Configuration Section
####################

##  # Server B Access
##  SERVER_B_USER="jcarlon"
##  SERVER_B_HOST="cplex3"
##  SERVER_B_SCRIPT_DIR="/home/jcarlon/test/0_dpdk_sender"
##
##  # Server A Configuration
##  L3FWD_PATH="/home/jcarlon/zzz_test/dpdk/build/examples/dpdk-l3fwd-power"
##  RAPL_SCRIPT="/home/jcarlon/measurements/run_rapl.sh"
##  RESULTS_DIR="/home/jcarlon/measurements/results"
##  MEASUREMENT_DURATION=30
##  MAX_BITRATE=10000000000  # 10 Gbps in bits/sec


# Server B Access
SERVER_B_USER="carlon"
SERVER_B_HOST="lace"
SERVER_B_SCRIPT_DIR="/home/carlon/test/0_dpdk_sender"

# Server A Configuration
L3FWD_PATH="/home/carlon/dpdk/build/examples/dpdk-l3fwd-power"
RAPL_SCRIPT="/home/carlon/measurements/run_rapl.sh"
RESULTS_DIR="/home/carlon/measurements/results"
MEASUREMENT_DURATION=30
MAX_BITRATE=100000000000  # 100 Gbps in bits/sec




# --- Pattern configuration ---
PATTERN="uniform"
PATTERN_PARAMS=""
BURST_SIZE=32
PAUSE_TIME=0

# ON/OFF specific parameters
ONOFF_TON=""
ONOFF_TOFF=""
ONOFF_RATE=""

# LogNormal specific parameters
LOGNORMAL_RATE=""
LOGNORMAL_ON_MU=""
LOGNORMAL_ON_SIGMA=""
LOGNORMAL_OFF_MU=""
LOGNORMAL_OFF_SIGMA=""

# TLOGN specific parameters
TLOGN_RATE_MU=""
TLOGN_RATE_SIGMA=""



# --- Size configuration ---
SIZES=( 128 256 512 1024 )
# SIZES=( 1024 )

## RATES=(  305000  610000  915000 1000000 1125000 1220000 ) # size 1024
## RATES=(  610000 1220000 1830000 2000000 2250000 2440000 ) # size 512
## RATES=( 1220000 2440000 3660000 4000000 4500000 4880000 ) # size 256
## RATES=( 2440000 4880000 7320000 8000000 9000000 9760000 ) # size 128



# Advanced Settings
SSH_OPTS="-o StrictHostKeyChecking=no -o ConnectTimeout=5 -o LogLevel=ERROR"
L3FWD_CORES="0"
LATENCY_TEST_CORES="2,4,6"
INITIAL_WAIT=5
COOLDOWN=2
DPDK_QUEUE_CONFIG="(0,0,2),(1,0,2)"
DPDK_CORE_OPTION=2

####################
# Function Definitions
####################


##  ##  printf ("%s [EAL options] -- -p PORTMASK -P"
##  ##      "  [--config (port,queue,lcore)[,(port,queue,lcore]]"
##  ##      "  [--high-perf-cores CORELIST"
##  ##      "  [--perf-config (port,queue,hi_perf,lcore_index)[,(port,queue,hi_perf,lcore_index]]"
##  ##      "  [--max-pkt-len PKTLEN]\n"
##  ##      "  -p PORTMASK: hexadecimal bitmask of ports to configure\n"
##  ##      "  -P: enable promiscuous mode\n"
##  ##      "  -u: set min/max frequency for uncore to minimum value\n"
##  ##      "  -U: set min/max frequency for uncore to maximum value\n"
##  ##      "  -i (frequency index): set min/max frequency for uncore to specified frequency index\n"
##  ##      "  --config (port,queue,lcore): rx queues configuration\n"
##  ##      "  --cpu-resume-latency LATENCY: set CPU resume latency to control C-state selection,"
##  ##      " 0 : just allow to enter C0-state\n"
##  ##      "  --high-perf-cores CORELIST: list of high performance cores\n"
##  ##      "  --perf-config: similar as config, cores specified as indices"
##  ##      " for bins containing high or regular performance cores\n"
##  ##      "  --no-numa: optional, disable numa awareness\n"
##  ##      "  --max-pkt-len PKTLEN: maximum packet length in decimal (64-9600)\n"
##  ##      "  --parse-ptype: parse packet type by software\n"
##  ##      "  --legacy: use legacy interrupt-based scaling\n"
##  ##      " --telemetry: enable telemetry mode, to update"
##  ##      " empty polls, full polls, and core busyness to telemetry\n"
##  ##      " --interrupt-only: enable interrupt-only mode\n"
##  ##      " --hybrid: enable hybrid polling/interrupt mode\n"
##  ##      "  --max-interrupt-timeout TIMEOUT_US: max interrupt sleep time in us (default: 300)\n"
##  ##      "  --grace-poll-count COUNT: quick polls after wakeup (default: 100)\n"
##  ##      "  --grace-poll-interval INTERVAL_US: interval between grace polls in us (default: 1)\n"
##  ##      "  --min-cons-empty COUNT: empty polls before interrupt fallback (default: 100)\n"
##  ##      "  --worst-wake-up US: worst case wake up delay in us (default: 150)\n"
##  ##      "  --max-small-sleep US: max small sleep duration in us (default: 50)\n"
##  ##      "  --min-small-sleep US: min small sleep duration in us (default: 10)\n"
##  ##      "  --no-pkt-ts-off US: threshold for off phase detection in us (default: 500)\n",
##  ##      " --pmd-mgmt MODE: enable PMD power management mode. "
##  ##      "Currently supported modes: baseline, monitor, pause, scale\n"
##  ##      "  --max-empty-polls MAX_EMPTY_POLLS: number of empty polls to"
##  ##      " wait before entering sleep state\n"
##  ##      "  --pause-duration DURATION: set the duration, in microseconds,"
##  ##      " of the pause callback\n"
##  ##      "  --scale-freq-min FREQ_MIN: set minimum frequency for scaling mode for"
##  ##      " all application lcores (FREQ_MIN must be in kHz, in increments of 100MHz)\n"
##  ##      "  --scale-freq-max FREQ_MAX: set maximum frequency for scaling mode for"
##  ##      " all application lcores (FREQ_MAX must be in kHz, in increments of 100MHz)\n",


EXTRA_ARGS=""          # Positional parameters for custom patterns
NAME=""                # Optional experiment name (appended to filename)




# --- Type Configuration --- 
## TYPES=("baseline" "interrupt-only" "pause" "hybrid" )
TYPES=("baseline")

minConsEmpty=10000
maxIntTimeout=10
gracePollCount=1000

## ## sudo chrt -f 99 ./build/examples/dpdk-l3fwd-power -l 0 -- --pmd-mgmt=baseline --busypolling_pause_duration_ns=50    -p 0x3 --config="(0,0,0),(1,0,0)"

DPDK_STARTUP_DELAY=5
start_l3fwd() {
    echo "---"
    echo "---"
    echo "[$(date +%T)] Starting l3fwd-power on Server A"
    mode=$1
    ## sudo sysctl kernel.sched_rt_runtime_us=-1
    case $mode in
        baseline)
            sudo chrt -f 99  $L3FWD_PATH -l $DPDK_CORE_OPTION -- --pmd-mgmt=baseline --busypolling_pause_duration_ns=50  -p 0x3 --config=$DPDK_QUEUE_CONFIG > "$RESULTS_DIR/l3fwd_baseline.log" 2>&1 &
            L3FWD_PID=$!
            sleep $DPDK_STARTUP_DELAY
            ;;
        interrupt-only)
            sudo chrt -f 99  $L3FWD_PATH -l $DPDK_CORE_OPTION -- --interrupt-only -p 0x3 --config=$DPDK_QUEUE_CONFIG > "$RESULTS_DIR/l3fwd.log" 2>&1 &
            L3FWD_PID=$!
            sleep $DPDK_STARTUP_DELAY
            ;;
        hybrid)
            # sudo $L3FWD_PATH -l $DPDK_CORE_OPTION -- --hybrid  -p 0x3 --config=$DPDK_QUEUE_CONFIG > "$RESULTS_DIR/l3fwd.log" 2>&1 &
            sudo chrt -f 99  $L3FWD_PATH -l $DPDK_CORE_OPTION -- --hybrid  --min-cons-empty=$minConsEmpty  --max-interrupt-timeout=$maxIntTimeout  --grace-poll-count=$gracePollCount  -p 0x3 --config=$DPDK_QUEUE_CONFIG > "$RESULTS_DIR/l3fwd.log" 2>&1 &
            L3FWD_PID=$!
            sleep $DPDK_STARTUP_DELAY
            ;;
        pause)
            # pause_duration=$2
            pause_duration=1
            sudo chrt -f 99  $L3FWD_PATH -l $DPDK_CORE_OPTION -- --pmd-mgmt=pause --pause-duration=$pause_duration -p 0x3 --config=$DPDK_QUEUE_CONFIG > "$RESULTS_DIR/l3fwd.log" 2>&1 &
            L3FWD_PID=$!
            sleep $DPDK_STARTUP_DELAY
            ;;
        scale)
            scale_freq_min=$2
            scale_freq_max=$3
            max_empty_polls=$4 
            sudo chrt -f 99  $L3FWD_PATH -l $DPDK_CORE_OPTION -- --pmd-mgmt=scale --pause-duration=1    -p 0x3 --config=$DPDK_QUEUE_CONFIG > "$RESULTS_DIR/l3fwd.log" 2>&1 &
            L3FWD_PID=$!
            sleep $DPDK_STARTUP_DELAY
            ;;
    esac
    sleep 3
}


stop_l3fwd() {
    echo "[$(date +%T)] Stopping l3fwd-power"
    sudo kill -TERM $L3FWD_PID 2>/dev/null
    sleep 2
}


generate_pattern_string() {
    case $1 in
        burst)
            echo "_${BURST_SIZE}burst_${PAUSE_TIME}s"
            ;;
        sawtooth)
            echo "_${SAWTOOTH_START}-${SAWTOOTH_END}over${SAWTOOTH_STEPS}steps"
            ;;
        onoff)
            [[ -n "$ONOFF_TON" && -n "$ONOFF_TOFF" && -n "$RATES" ]] && \
            echo "_${ONOFF_TON}U_${ONOFF_TOFF}N_${RATES}R" || \
            echo ""
            ;;
        poisson)
            echo "_${POISSON_LAMBDA}lambda_${POISSON_BURST}burst"
            ;;
        dc)
            echo "_${DC_BASE_RATE}base_${DC_BURST_RATE}burst_${DC_BURST_DURATION}dur_${DC_BURST_INTERVAL}int"
            ;;
        lognormal)
            echo "_${LOGNORMAL_RATE}R_${LOGNORMAL_ON_MU}onM_${LOGNORMAL_ON_SIGMA}onS_${LOGNORMAL_OFF_MU}offM_${LOGNORMAL_OFF_SIGMA}offS"
            ;;
        tlogn)
            echo "_${LOGNORMAL_ON_MU}onM_${LOGNORMAL_ON_SIGMA}onS_${LOGNORMAL_OFF_MU}offM_${LOGNORMAL_OFF_SIGMA}offS_${TLOGN_RATE_MU}rateM_${TLOGN_RATE_SIGMA}rateS"
            ;;
        multipleExpLogn)
            local args=($EXTRA_ARGS)
            local joined=$(IFS=_ ; echo "${args[*]}")
            echo "_multi_${joined}"
            ;;
        web)
            local args=($EXTRA_ARGS)
            local joined=$(IFS=_ ; echo "${args[*]}")
            echo "_web_${joined}"
            ;;
        gaming)
            local args=($EXTRA_ARGS)
            local joined=$(IFS=_ ; echo "${args[*]}")
            echo "_gaming_${joined}"
            ;;
        *)
            echo ""
            ;;
    esac
}






run_latency_test() {
    local size=$1
    local pattern=$2
    local type=$3

    ## rate is gotten only if param is not sawtooth !
    local rate_param=""
    local onoff_params=""
    local burst_param=""
    local byterate=0
    local pattern_specific_params=""
    
    # Handle rate parameters based on pattern
    if [[ "$pattern" == "sawtooth" ]]; then
        local rate=$SAWTOOTH_END
        byterate=$((size * SAWTOOTH_END * 8))
        rate_param=""  
        burst_param=""
    elif [[ "$pattern" == "burst" ]]; then
        local rate= 1
        byterate=$((size * rate * 8))
        rate_param=""  
        burst_param=" -b $BURST_SIZE"
    elif [[ "$pattern" == "onoff" ]]; then
        local rate=$4
        # Validate required parameters
        [[ -z "$ONOFF_TON" || -z "$ONOFF_TOFF" || -z "$RATES" ]] && \
            { echo "ON/OFF pattern requires -U and -N parameters"; exit 1; }
        byterate=$((size * rate * 8))
        rate_param="-r $rate"
        onoff_params="-U $ONOFF_TON -N $ONOFF_TOFF -R $RATES "
        burst_param=""
    elif [[ "$pattern" == "poisson" ]]; then
        if [[ -z "$POISSON_LAMBDA" || -z "$POISSON_BURST" ]]; then
            echo "Error: Poisson requires -L (lambda) and -K (burst)"
            exit 1
        fi
        byterate=$(( size * POISSON_LAMBDA * POISSON_BURST * 8 ))
        ## <POISSON_LAMBDA> is number of bursts per second
        ## <POISSON_BURST> is bumber of packets per burst
        ## <size> is number of bytes per packet
        rate_param=""  
        burst_param=""
        onoff_params=""
        pattern_specific_params="-P $POISSON_LAMBDA -k $POISSON_BURST"
    elif [[ "$pattern" == "dc" ]]; then
        if [[ -z "$DC_BASE_RATE" || -z "$DC_BURST_RATE" || -z "$DC_BURST_DURATION" || -z "$DC_BURST_INTERVAL" ]]; then
            echo "Error: DC requires -D (base), -F (burst), -G (duration), -I (interval)"
            exit 1
        fi
        rate_param=""  
        burst_param=""
        onoff_params=""
        pattern_specific_params="-D $DC_BASE_RATE -F $DC_BURST_RATE -G $DC_BURST_DURATION -I $DC_BURST_INTERVAL"
    elif [[ "$pattern" == "lognormal" ]]; then
        if [[ -z "$LOGNORMAL_RATE" || -z "$LOGNORMAL_ON_MU" || -z "$LOGNORMAL_ON_SIGMA" || -z "$LOGNORMAL_OFF_MU" || -z "$LOGNORMAL_OFF_SIGMA" ]]; then
            echo "Error: LogNormal requires -v (rate), -w (ON_MU), -x (ON_SIGMA), -y (OFF_MU), -z (OFF_SIGMA)"
            exit 1
        fi
        byterate=$((size * LOGNORMAL_RATE * 8))
        pattern_specific_params="-L $LOGNORMAL_RATE $LOGNORMAL_ON_MU $LOGNORMAL_ON_SIGMA $LOGNORMAL_OFF_MU $LOGNORMAL_OFF_SIGMA"
        rate_param=""
        burst_param=""
    elif [[ "$pattern" == "tlogn" ]]; then
        if [[ -z "$LOGNORMAL_ON_MU" || -z "$LOGNORMAL_ON_SIGMA" || -z "$LOGNORMAL_OFF_MU" || -z "$LOGNORMAL_OFF_SIGMA" || -z "$TLOGN_RATE_MU" || -z "$TLOGN_RATE_SIGMA" ]]; then
            echo "Error: TLOGN requires -w (ON_MU), -x (ON_SIGMA), -y (OFF_MU), -z (OFF_SIGMA), -W (RATE_MU), -X (RATE_SIGMA)"
            exit 1
        fi
        byterate=$((size * 1000000 ))  # Placeholder, actual rate depends on lognormal distribution
        pattern_specific_params="-T $LOGNORMAL_ON_MU $LOGNORMAL_ON_SIGMA $LOGNORMAL_OFF_MU $LOGNORMAL_OFF_SIGMA $TLOGN_RATE_MU $TLOGN_RATE_SIGMA"
        rate_param=""
        burst_param=""
    elif [[ "$pattern" == "multipleExpLogn" || "$pattern" == "web" ]]; then
        # Use the extra arguments as-is (positional)
        pattern_specific_params="$EXTRA_ARGS"
        # Nominal bitrate (approx 1 Mpps) – adjust if needed
        byterate=$((size * 1000000 * 8))
        rate_param=""
        burst_param=" -B $BURST_SIZE"   # include burst size
        onoff_params=""
    elif [[ "$pattern" == "gaming" ]]; then
        pattern_specific_params="$EXTRA_ARGS"
        byterate=$((size * 1000000 * 8))   # nominal 1 Mpps
        rate_param=""
        burst_param=" -B $BURST_SIZE"
        onoff_params=""
    else
        local rate=$4
        byterate=$((size * rate * 8))
        rate_param="-r $rate"
        burst_param=" -B $BURST_SIZE" 
    fi
    
    # Skip configurations exceeding 10Gbps
    if (( byterate > MAX_BITRATE )); then
        echo "Skipping configuration: ${size}B @ ${rate}pps = $((byterate/1000000))Mbps (>10Gbps)"
        return 
    fi

    # Generate filenames

    local hybrid_params_suffix=""
    if [[ "$type" == "hybrid" ]]; then
        hybrid_params_suffix="_Hybrid_minConE${minConsEmpty}_maxIntT${maxIntTimeout}_gracePollC${gracePollCount}"
    fi



    # Update filename template with pattern parameters
    local pattern_str=$(generate_pattern_string $pattern)
    local base_name="${RESULTS_DIR}/${type}_${pattern}${pattern_str}_${BURST_SIZE}pkt_${size}B_${byterate}b${hybrid_params_suffix}"

    if [[ -n "$NAME" ]]; then
        base_name="${base_name}_${NAME}"
    fi

    local power_file="${base_name}_power"
    local latency_file="${base_name}_latency"
    local remote_log="/tmp/latency_temp_${type}_${size}_${rate}.log"
    local remote_pid="/tmp/latency_pid_${pattern}_${size}_${rate}.pid"

    echo "============================================================"
    echo "[$(date +%T)] Starting test: Type=$type, Pattern=$pattern, Size=${size}B"
    echo "           Bitrate: $((byterate/1000000)) Mbps"
    echo "============================================================"

    # Start latency test on Server B


    ssh $SSH_OPTS ${SERVER_B_USER}@${SERVER_B_HOST} "\
	sudo pkill latency_test || true ; \
        sudo rm -f $remote_log $remote_pid ; \
	sudo rm -rf /var/run/dpdk/* /dev/shm/dpdk* ; \
	sudo sleep 5 ; \
        cd $SERVER_B_SCRIPT_DIR && \
        ( nohup sudo ./latency_test -l $LATENCY_TEST_CORES -- -s $size  $burst_param  -p $pattern  $pattern_specific_params  $rate_param  $onoff_params  $PATTERN_PARAMS  > $remote_log 2>&1 & ) ; \
        sleep 1 ; \
	pid=\$(pgrep -f 'latency_test -l') ; \
        echo \$pid > $remote_pid ; \
	sleep 1 ; \
        sudo chmod 644 $remote_log $remote_pid"

    sleep $INITIAL_WAIT


    # Start power measurement on Server A
    echo "[$(date +%T)] Starting power measurement..."
    $RAPL_SCRIPT -y -r -c $((MEASUREMENT_DURATION + 2)) -s 1 "$power_file"


    # Stop latency test gracefully
    echo "[$(date +%T)] Stopping latency test and collecting results..."
    ssh $SSH_OPTS ${SERVER_B_USER}@${SERVER_B_HOST} "\
        pid=\$(cat $remote_pid 2>/dev/null) && \
        sudo kill -USR1 \$pid 2>/dev/null && \
        echo 'Sent SIGUSR1, waiting for process exit...' >> $remote_log && \
        timeout 5 tail --pid=\$pid -f /dev/null && \
        sleep 2 && \
	sudo sync && \
	sleep 3 && \
	sudo rm -f $remote_pid"


    # Extra assurance wait
    sleep 5

    # Copy latency log with retries
    for attempt in {1..5}; do
        scp $SSH_OPTS ${SERVER_B_USER}@${SERVER_B_HOST}:$remote_log "$latency_file" >/dev/null 2>&1
        if grep -q "Overall StdDev latency:" "$latency_file" 2>/dev/null; then
            break
        else
            sleep 3
	    ssh $SSH_OPTS ${SERVER_B_USER}@${SERVER_B_HOST} \
                " sudo pkill -f 'latency_test -l' || true "
	fi
        sleep 3
    done


    # Cleanup
    ssh $SSH_OPTS ${SERVER_B_USER}@${SERVER_B_HOST} "sudo rm -f $remote_log" >/dev/null 2>&1

    # Validation
    [ -s "$power_file" ] || echo "Warning: Power measurement failed for ${base_name}"
    [ -s "$latency_file" ] || echo "Warning: Latency log not captured for ${base_name}"
    
    sleep $COOLDOWN
}

main() {
    mkdir -p "$RESULTS_DIR"
    echo "Saving all results to: $RESULTS_DIR"

    for type in "${TYPES[@]}"; do

        start_l3fwd $type

        # Pattern-specific handling
        case $PATTERN in
            sawtooth)
                # Sawtooth runs once per size with pattern parameters
                for size in "${SIZES[@]}"; do
                    run_latency_test $size $PATTERN $type
                done
                ;;

            burst)
                # burst runs once per size with pattern parameters
                for size in "${SIZES[@]}"; do
                    run_latency_test $size $PATTERN $type
                done
                ;;
            onoff)
                # ON/OFF runs once per size with fixed rate
                [[ -z "$RATES" ]] && { echo "ON/OFF pattern requires -r parameter"; exit 1; }
                for size in "${SIZES[@]}"; do
                    run_latency_test $size $PATTERN $type $ONOFF_RATE
                done
                ;;
            poisson|dc|lognormal|tlogn)
                # ON/OFF runs once per size with fixed rate
                for size in "${SIZES[@]}"; do
                    run_latency_test $size $PATTERN $type 
                done
                ;;
            multipleExpLogn|web|gaming)
                for size in "${SIZES[@]}"; do
                    run_latency_test $size $PATTERN $type
                done
                ;;
            *)
                # Default handling for uniform/burst patterns
                for size in "${SIZES[@]}"; do
                    # Set rates only if not provided
                    local current_rates=()
                    if [ ${#RATES[@]} -eq 0 ]; then
                        case $size in
                            128) current_rates=(2440000 4880000 7320000 8000000 9000000 9760000) ;;
                            256) current_rates=(1220000 2440000 3660000 4000000 4500000 4880000) ;;
                            512) current_rates=(610000 1220000 1830000 2000000 2250000 2440000) ;;
                            1024) current_rates=(305000 610000 915000 1000000 1125000 1220000) ;;
                            *) echo "Error: Unknown packet size $size"; exit 1 ;;
                        esac
                    else
                        current_rates=("${RATES[@]}")
                    fi

                    for rate in "${current_rates[@]}"; do
                        run_latency_test $size $PATTERN $type $rate
                    done
                done
            ;;
        esac
    
    stop_l3fwd
    done

    echo "[$(date +%T)] All tests completed!"
}

####################
# Execution Section
####################

while getopts "t:s:r:p:b:T:S:E:d:C:P:U:N:R:L:K:D:F:G:I:v:w:x:y:z:W:X:A:n:h" opt; do
    case $opt in
        t) TYPES=("$OPTARG") ;;
        s) SIZES=("$OPTARG") ;;
        r) RATES=("$OPTARG") ;;
        p) PATTERN="$OPTARG" ;;
        b) BURST_SIZE="$OPTARG" ;;
        T) PAUSE_TIME="$OPTARG"  # For burst-pause
           PATTERN_PARAMS+="-t $PAUSE_TIME " ;;
        S) SAWTOOTH_START="$OPTARG"  # For sawtooth
           PATTERN_PARAMS+="-S $SAWTOOTH_START " ;;
        E) SAWTOOTH_END="$OPTARG"
           PATTERN_PARAMS+="-E $SAWTOOTH_END " ;;
        d) SAWTOOTH_DURATION="$OPTARG"
           PATTERN_PARAMS+="-d $SAWTOOTH_DURATION " ;;
        C) SAWTOOTH_STEPS="$OPTARG"
           PATTERN_PARAMS+="-c $SAWTOOTH_STEPS " ;;
        U) ONOFF_TON="$OPTARG" ;;
        N) ONOFF_TOFF="$OPTARG" ;;
        R) ONOFF_RATE="$OPTARG" ;;
        L) POISSON_LAMBDA="$OPTARG" ;;
        K) POISSON_BURST="$OPTARG" ;;
        D) DC_BASE_RATE="$OPTARG" ;;
        F) DC_BURST_RATE="$OPTARG" ;;
        G) DC_BURST_DURATION="$OPTARG" ;;
        I) DC_BURST_INTERVAL="$OPTARG" ;;
        P) PATTERN_PARAMS+="$OPTARG " ;;  # Generic pattern params
        v) LOGNORMAL_RATE="$OPTARG" ;;
        w) LOGNORMAL_ON_MU="$OPTARG" ;;
        x) LOGNORMAL_ON_SIGMA="$OPTARG" ;;
        y) LOGNORMAL_OFF_MU="$OPTARG" ;;
        z) LOGNORMAL_OFF_SIGMA="$OPTARG" ;;
        W) TLOGN_RATE_MU="$OPTARG" ;;
        X) TLOGN_RATE_SIGMA="$OPTARG" ;;
        A) EXTRA_ARGS="$OPTARG" ;;   # Extra arguments for custom patterns
        n) NAME="$OPTARG" ;;         # Experiment name
        h) echo "Usage: $0 [-t type] [-s size] [-r rate] [-A 'args'] [-n name]"; exit 0 ;;
        *) echo "Invalid option"; exit 1 ;;
    esac
done


case $PATTERN in
    sawtooth)
        if [[ -z $SAWTOOTH_START || -z $SAWTOOTH_END || -z $SAWTOOTH_DURATION || -z $SAWTOOTH_STEPS ]]; then
            echo "Sawtooth pattern requires -S, -E, -d, -c parameters"
            exit 1
        fi
        ;;
    burst)
        if [[ -z $PAUSE_TIME ]]; then
            echo "Burst-pause pattern requires -T parameter"
            exit 1
        fi
        ;;
    onoff)
        if [[ -z "$ONOFF_TON" || -z "$ONOFF_TOFF" || -z "$RATES" ]]; then
            echo "ON/OFF pattern requires -U, -N and -r parameters"
            exit 1
        fi
        ;;
    poisson)
        if [[ -z "$POISSON_LAMBDA" || -z "$POISSON_BURST" ]]; then
            echo "Poisson pattern requires -L (lambda) and -K (burst)"
            exit 1
        fi
        ;;
    dc)
        if [[ -z "$DC_BASE_RATE" || -z "$DC_BURST_RATE" || -z "$DC_BURST_DURATION" || -z "$DC_BURST_INTERVAL" ]]; then
            echo "DC pattern requires -D (base), -F (burst), -G (duration), -I (interval)"
            exit 1
        fi
        ;;
    lognormal)
        if [[ -z "$LOGNORMAL_RATE" || -z "$LOGNORMAL_ON_MU" || -z "$LOGNORMAL_ON_SIGMA" || -z "$LOGNORMAL_OFF_MU" || -z "$LOGNORMAL_OFF_SIGMA" ]]; then
            echo "LogNormal pattern requires -v (rate), -w (ON_MU), -x (ON_SIGMA), -y (OFF_MU), -z (OFF_SIGMA)"
            exit 1
        fi
        ;;
    tlogn)
        if [[ -z "$LOGNORMAL_ON_MU" || -z "$LOGNORMAL_ON_SIGMA" || -z "$LOGNORMAL_OFF_MU" || -z "$LOGNORMAL_OFF_SIGMA" || -z "$TLOGN_RATE_MU" || -z "$TLOGN_RATE_SIGMA" ]]; then
            echo "TLOGN pattern requires -w (ON_MU), -x (ON_SIGMA), -y (OFF_MU), -z (OFF_SIGMA), -W (RATE_MU), -X (RATE_SIGMA)"
            exit 1
	    fi
        ;;
    multipleExpLogn|web|gaming)
        if [[ -z "$EXTRA_ARGS" ]]; then
            echo "Pattern '$PATTERN' requires extra arguments via -A"
            exit 1
        fi
        ;;
esac

   



main 2>&1 | tee "${RESULTS_DIR}/measurement_$(date +%Y%m%d_%H%M%S).log"

# Final cleanup
ssh $SSH_OPTS ${SERVER_B_USER}@${SERVER_B_HOST} \
    "sudo pkill -f latency_test" >/dev/null 2>&1



#   # tlogn
#   ./exp_auto_grid.sh -p tlogn -s 256 -b 256 -w -3.5 -x 0.1 -y -3.5 -z 0.1 -W -9.9 -X 0.1 -n turbo_tuned_tlogn
#   
#   # multipleExpLogn with name "exp1"
#   ./exp_auto_grid.sh -p multipleExpLogn -s 256 -b 256 -A "350 0.006 14.45 0.35 100 5000000 25000000000" -n turbo_tuned_expLogn
#       -> will run ./latency_test -l 2,4,6 -- -s 256 -B 256 -p multipleExpLogn  -- 350 0.006 14.45 0.35 100 5000000 25000000000 
#   
#   # web with name "web1"
#   ./exp_auto_grid.sh -p web -s 256 -b 256 -A "1000000 8.37 1.37 100 2000000 6.17 2.36 50 2000000 1.1 2.0 55.0 7.69 0.033" -n turbo_tuned_web
#       -> will run ./latency_test -l 2,4,6 -- -s 256 -p web -B 256 -- 1000000 8.37 1.37 100 2000000 6.17 2.36 50 2000000 1.1 2.0 55.0 7.69 0.033 
#   


