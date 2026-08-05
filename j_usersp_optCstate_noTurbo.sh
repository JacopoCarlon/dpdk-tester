#!/bin/bash
# j_usersp_optCstate_noTurbo.sh
# Disable Turbo, optionally enable/disable C-states, set governor to userspace,
# and fix the CPU frequency range to user-provided min/max values.

# ----------------------------------------------------------------------
# User settings 
TARGET_CPUS="all"   
TARGET_CSTATE_CPUS="all"
TARGET_FREQ_CPUS="all"


##  test with: sudo ./j_usersp_optCstate_noTurbo.sh --cstates POLL,C1 2000000 2000000
# ----------------------------------------------------------------------


# e.g. cstates on whiskey
## carlon@whiskey:~$ {     echo "C-state counters (before RAPL) Timestamp: $(date +%s)";     for core in {2,4,6}; do         cpu_dir="/sys/devices/system/cpu/cpu${core}/cpuidle";         if [ -d "$cpu_dir" ]; then             for state_dir in "$cpu_dir"/state*; do                 [ -d "$state_dir" ] || continue;                 state_name=$(cat "$state_dir/name");                 state_usage=$(cat "$state_dir/usage");                 state_time=$(cat "$state_dir/time");                 echo "cpu${core} $(basename $state_dir): name=${state_name} usage=${state_usage} time=${state_time}";             done;         fi;     done;     echo ""; } > c.txt
## carlon@whiskey:~$ cat c.txt 
##  C-state counters (before RAPL) Timestamp: 1785928897
##  cpu2 state0: name=POLL usage=4148 time=65865
##  cpu2 state1: name=C1 usage=44443 time=5192005
##  cpu2 state2: name=C1E usage=727923 time=117745698
##  cpu2 state3: name=C3 usage=0 time=0
##  cpu2 state4: name=C6 usage=16713415 time=416813286631



##  # e.g. Disable Turbo, disable C-states (default), set userspace governor and frequency range
##  sudo ./j_usersp_optCstate_noTurbo.sh 800000 2000000
##  
##  # e.g. Disable Turbo, enable all C-states, set userspace governor and frequency range
##  sudo ./j_usersp_optCstate_noTurbo.sh --enable-cstates 800000 2000000
##  
##  # e.g. Disable Turbo, enable only POLL and C1E, set userspace governor and frequency range
##  sudo ./j_usersp_optCstate_noTurbo.sh --cstates POLL,C1E 800000 2000000
sudo sysctl kernel.sched_rt_runtime_us=-1

# Parse optional flags
ENABLE_CSTATES=0   # default: disabled
CSTATE_LIST=""     # defualt: is empty -> use ENABLE_CSTATES logic

while [[ $# -gt 0 ]]; do
    case "$1" in
        --enable-cstates)
            ENABLE_CSTATES=1
            ## drop flag --enable-cstates
            shift
            ;;
        --cstates)
            CSTATE_LIST="$2"
            ## drop flag --cstates and its parameter object
            shift 2
            ;;
        --help)
            echo "Usage: $0 [--enable-cstates | --cstates LIST] <min_freq_kHz> <max_freq_kHz>"
            echo "  --enable-cstates    Enable all C-states (default: disabled)"
            echo "  --cstates LIST      Enable only the listed C-states (comma-separated names or indices)"
            echo "                      e.g. --cstates POLL,C1E   or   --cstates 0,2"
            echo "  If neither is given, all C-states are disabled."
            echo "Example: $0 800000 2000000"
            echo "Example: $0 --enable-cstates 800000 2000000"
            echo "Example: $0 --cstates POLL,C1E 800000 2000000"
            exit 0
            ;;
        *)
            break
            ;;
    esac
done

# Check for required parameters
if [ $# -ne 2 ]; then
    echo "Usage: $0 [--enable-cstates | --cstates LIST] <min_freq_kHz> <max_freq_kHz>" >&2
    exit 1
fi

MIN_FREQ=$1
MAX_FREQ=$2

# ----------------------------------------------------------------------
# Helper functions to identify target CPUs
# ----------------------------------------------------------------------
declare -A CPU_TARGET
if [[ "$TARGET_CPUS" == "all" ]]; then
    for cpu in /sys/devices/system/cpu/cpu[0-9]*; do
        n=${cpu#/sys/devices/system/cpu/cpu}
        CPU_TARGET[$n]=1
    done
else
    IFS=',' read -ra parts <<< "$TARGET_CPUS"
    for part in "${parts[@]}"; do
        if [[ $part =~ ^([0-9]+)-([0-9]+)$ ]]; then
            for ((i=${BASH_REMATCH[1]}; i<=${BASH_REMATCH[2]}; i++)); do
                CPU_TARGET[$i]=1
            done
        elif [[ $part =~ ^[0-9]+$ ]]; then
            CPU_TARGET[$part]=1
        fi
    done
fi

cpu_is_target() { [[ -n ${CPU_TARGET[$1]} ]]; }
any_cpu_target() {
    local c
    for c in $1; do
        if cpu_is_target "$c"; then
            return 0
        fi
    done
    return 1
}



declare -A CSTATE_CPU_TARGET
if [[ "$TARGET_CSTATE_CPUS" == "all" ]]; then
    for cpu in /sys/devices/system/cpu/cpu[0-9]*; do
        n=${cpu#/sys/devices/system/cpu/cpu}
        CSTATE_CPU_TARGET[$n]=1
    done
else
    IFS=',' read -ra cstate_parts <<< "$TARGET_CSTATE_CPUS"
    for part in "${cstate_parts[@]}"; do
        if [[ $part =~ ^([0-9]+)-([0-9]+)$ ]]; then
            for ((i=${BASH_REMATCH[1]}; i<=${BASH_REMATCH[2]}; i++)); do
                CSTATE_CPU_TARGET[$i]=1
            done
        elif [[ $part =~ ^[0-9]+$ ]]; then
            CSTATE_CPU_TARGET[$part]=1
        fi
    done
fi

cpu_is_target_cstate() { [[ -n ${CSTATE_CPU_TARGET[$1]} ]]; }




declare -A FREQ_CPU_TARGET
if [[ "$TARGET_FREQ_CPUS" == "all" ]]; then
    for cpu in /sys/devices/system/cpu/cpu[0-9]*; do
        n=${cpu#/sys/devices/system/cpu/cpu}
        FREQ_CPU_TARGET[$n]=1
    done
else
    IFS=',' read -ra freq_parts <<< "$TARGET_FREQ_CPUS"
    for part in "${freq_parts[@]}"; do
        if [[ $part =~ ^([0-9]+)-([0-9]+)$ ]]; then
            for ((i=${BASH_REMATCH[1]}; i<=${BASH_REMATCH[2]}; i++)); do
                FREQ_CPU_TARGET[$i]=1
            done
        elif [[ $part =~ ^[0-9]+$ ]]; then
            FREQ_CPU_TARGET[$part]=1
        fi
    done
fi

cpu_is_target_freq() { [[ -n ${FREQ_CPU_TARGET[$1]} ]]; }

any_cpu_target_freq() {
    local c
    for c in $1; do
        if cpu_is_target_freq "$c"; then
            return 0
        fi
    done
    return 1
}

# ----------------------------------------------------------------------
# Resolve C-state whitelist (if any)
# ----------------------------------------------------------------------
declare -A ALLOWED_CSTATES   # key = state name, value = 1 if allowed

##  carlon@whiskey:/sys/devices/system/cpu/cpu0/cpuidle$ ls
##  state0  state1  state2  state3  state4

##  carlon@whiskey:/sys/devices/system/cpu/cpu0/cpuidle$ cat state0/name 
##  POLL


if [[ -n "$CSTATE_LIST" ]]; then
    # since (if) cstate is passed by index, 
    #   we need to find index-to-name map, take it from first cpu available
    declare -A STATE_INDEX_TO_NAME
    for cpu in /sys/devices/system/cpu/cpu[0-9]*; do
        n=${cpu#/sys/devices/system/cpu/cpu}
        if cpu_is_target "$n" && [ -d "$cpu/cpuidle" ]; then
            for state in "$cpu"/cpuidle/state*; do
                idx=${state##*state}                     # e.g. "0"
                name=$(cat "$state/name" 2>/dev/null)
                STATE_INDEX_TO_NAME[$idx]=$name
            done
            break
        fi
    done

    # Parse the comma-separated list
    IFS=',' read -ra TOKENS <<< "$CSTATE_LIST"
    for tok in "${TOKENS[@]}"; do
        tok=$(echo "$tok" | xargs)   # remove trailing/leading whitespace with no args to xargs (this should not be needed tho..)
        if [[ "$tok" =~ ^[0-9]+$ ]]; then
            ## if token is index, we map it to name
            name="${STATE_INDEX_TO_NAME[$tok]}"
            if [[ -n "$name" ]]; then
                ALLOWED_CSTATES["$name"]=1
            else
                echo "Warning: C-state index $tok not found on this system, ignoring."
            fi
        else
            ## else if it is name already, ok.
            ALLOWED_CSTATES["$tok"]=1
        fi
    done

    if [[ ${#ALLOWED_CSTATES[@]} -eq 0 ]]; then
        echo "Warning: --cstates list did not match any known C-state. No states will be enabled."
    else
        echo "C-state whitelist: ${!ALLOWED_CSTATES[*]}"
    fi
fi

# ----------------------------------------------------------------------
# 1. Disable Turbo Boost (system-wide)
# ----------------------------------------------------------------------
if [ -d /sys/devices/system/cpu/intel_pstate ]; then
    if [ -e /sys/devices/system/cpu/intel_pstate/no_turbo ]; then
        echo 1 > /sys/devices/system/cpu/intel_pstate/no_turbo
        echo "Turbo disabled (intel_pstate)"
    fi
    [ -f /sys/devices/system/cpu/intel_pstate/max_perf_pct ] && echo 100 > /sys/devices/system/cpu/intel_pstate/max_perf_pct
    [ -f /sys/devices/system/cpu/intel_pstate/min_perf_pct ] && echo 0   > /sys/devices/system/cpu/intel_pstate/min_perf_pct
fi

if [ -f /sys/devices/system/cpu/cpufreq/boost ]; then
    echo 0 > /sys/devices/system/cpu/cpufreq/boost
    echo "Turbo disabled (cpufreq boost)"
fi

# ----------------------------------------------------------------------
# 2. Set governor to userspace and apply frequency range on target policies
# ----------------------------------------------------------------------
if [ ! -d /sys/devices/system/cpu/cpufreq ]; then
    echo "ERROR: cpufreq directory not found - cannot set governor."
    exit 1
fi

for policy in /sys/devices/system/cpu/cpufreq/policy[0-9]*; do
    if [ -f $policy/affected_cpus ]; then
        cpus=$(cat $policy/affected_cpus)
    else
        cpus=${policy##*/policy}
    fi

    if any_cpu_target "$cpus"; then
        echo "Configuring policy $policy (CPUs: $cpus)"

        # decide which frequency to use
        USE_MIN=""
        USE_MAX=""
        if any_cpu_target_freq "$cpus"; then
            USE_MIN=$MIN_FREQ
            USE_MAX=$MAX_FREQ
            echo "  Using user-specified frequency"
        else
            USE_MIN=1200000
            USE_MAX=1200000
            echo "  Using default frequency 1200000-1200000 (kHz)"
        fi

        if grep -q userspace $policy/scaling_available_governors; then
            echo userspace > $policy/scaling_governor
            echo "  Governor set to userspace"
        else
            echo "  ERROR: userspace governor not available for policy $policy - aborting."
            exit 1
        fi

        hw_min=$(cat $policy/cpuinfo_min_freq)
        hw_max=$(cat $policy/cpuinfo_max_freq)
        if [ $USE_MIN -lt $hw_min ] || [ $USE_MIN -gt $hw_max ] || \
            [ $USE_MAX -lt $hw_min ] || [ $USE_MAX -gt $hw_max ] || \
            [ $USE_MIN -gt $USE_MAX ]; then
            echo "  ERROR: requested range [$USE_MIN, $USE_MAX] kHz not within hardware limits [$hw_min, $hw_max] kHz."
            exit 1
        fi

        echo $USE_MIN > $policy/scaling_min_freq
        echo $USE_MAX > $policy/scaling_max_freq
        echo "  Frequency range set to [$USE_MIN, $USE_MAX] kHz"
    else
        echo "Skipping policy $policy (CPUs: $cpus) - not in target set"
    fi
done

# ----------------------------------------------------------------------
# 3. Configure C-states 
# ----------------------------------------------------------------------
echo ""
if [[ ${#ALLOWED_CSTATES[@]} -gt 0 ]]; then
    echo "Configuring C-states: enabling only ${!ALLOWED_CSTATES[*]}"
elif [ $ENABLE_CSTATES -eq 1 ]; then
    echo "Enabling all C-states (writing 0 to disable files)"
else
    echo "Disabling all C-states (writing 1 to disable files) - some states may not be disableable"
fi

for cpu in /sys/devices/system/cpu/cpu[0-9]*; do
    n=${cpu#/sys/devices/system/cpu/cpu}

    if cpu_is_target_cstate "$n"; then
        # apply cstate controls on target-CPUs of the cstate control.
        if [ -e $cpu/power/energy_perf_bias ]; then
            echo performance > $cpu/power/energy_perf_bias
        fi
        if [ -f $cpu/power/pm_qos_resume_latency_us ]; then
            echo 0 > $cpu/power/pm_qos_resume_latency_us
        fi
        if [ -d $cpu/cpuidle ]; then
            for state in $cpu/cpuidle/state*; do
                if [ -f "$state/disable" ]; then
                    name=$(cat "$state/name" 2>/dev/null)
                    if [[ ${#ALLOWED_CSTATES[@]} -gt 0 ]]; then
                        if [[ -n "${ALLOWED_CSTATES[$name]}" ]]; then
                            echo 0 | sudo tee "$state/disable" > /dev/null 2>/dev/null
                        else
                            echo 1 | sudo tee "$state/disable" > /dev/null 2>/dev/null
                        fi
                    else
                        if [ $ENABLE_CSTATES -eq 1 ]; then
                            echo 0 | sudo tee "$state/disable" > /dev/null 2>/dev/null
                        else
                            echo 1 | sudo tee "$state/disable" > /dev/null 2>/dev/null
                        fi
                    fi
                fi
            done
        fi
    else
        # force all C-states enabled on non-target CPUs
        #   i think this is the most logical thing to do, so they don't get high consumnption randomly
        if [ -d $cpu/cpuidle ]; then
            for state in $cpu/cpuidle/state*; do
                if [ -f "$state/disable" ]; then
                    echo 0 | sudo tee "$state/disable" > /dev/null 2>/dev/null
                fi
            done
        fi
    fi
done



# ----------------------------------------------------------------------
# 4. Show current settings for target CPUs
# ----------------------------------------------------------------------
echo ""
echo "#CPU scaling_governor scaling_cur_freq scaling_min_freq scaling_max_freq energy_perf_pref energy_perf_bias resume_latency"
for cpu in /sys/devices/system/cpu/cpu[0-9]*; do
    n=${cpu#/sys/devices/system/cpu/cpu}
    if cpu_is_target "$n"; then
        echo $cpu \
            $(cat $cpu/cpufreq/scaling_governor 2>/dev/null) \
            $(cat $cpu/cpufreq/scaling_cur_freq 2>/dev/null) \
            $(cat $cpu/cpufreq/scaling_min_freq 2>/dev/null) \
            $(cat $cpu/cpufreq/scaling_max_freq 2>/dev/null) \
            $(cat $cpu/cpufreq/energy_performance_preference 2>/dev/null) \
            $(cat $cpu/power/energy_perf_bias 2>/dev/null) \
            $(cat $cpu/power/pm_qos_resume_latency_us 2>/dev/null)
    fi
done



# ----------------------------------------------------------------------
# 5. Show C-state status for target CPUs
# ----------------------------------------------------------------------
echo ""
echo "C-state status (disable=1 means disabled):"
for cpu in /sys/devices/system/cpu/cpu[0-9]*; do
    n=${cpu#/sys/devices/system/cpu/cpu}
    if cpu_is_target "$n" && [ -d $cpu/cpuidle ]; then
        echo -n "CPU$n: "
        for state in $cpu/cpuidle/state*; do
            name=$(cat "$state/name" 2>/dev/null)
            dis=$(cat "$state/disable" 2>/dev/null)
            echo -n "$name:$dis "
        done
        echo
    fi
done


