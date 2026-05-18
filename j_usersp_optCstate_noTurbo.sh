#!/bin/bash
# j_fixed_freq_userspace.sh
# Disable Turbo, optionally enable/disable C-states, set governor to userspace,
# and fix the CPU frequency range to user-provided min/max values.

# ----------------------------------------------------------------------
# User settings - must match those used in setup.sh
# ----------------------------------------------------------------------
TARGET_CPUS="all"   # Set to the same value as in setup.sh
# ----------------------------------------------------------------------


##  # Disable Turbo, disable C-states (default), set userspace governor and frequency range
##  sudo ./j_usersp_optCstate_noTurbo.sh 800000 2000000
##  
##  # Disable Turbo, enable C-states, set userspace governor and frequency range
##  sudo ./j_usersp_optCstate_noTurbo.sh --enable-cstates 800000 2000000

sudo sysctl kernel.sched_rt_runtime_us=-1

# Parse optional flag --enable-cstates
ENABLE_CSTATES=0   # default: disabled
while [[ $# -gt 0 ]]; do
    case "$1" in
        --enable-cstates)
            ENABLE_CSTATES=1
            shift
            ;;
        --help)
            echo "Usage: $0 [--enable-cstates] <min_freq_kHz> <max_freq_kHz>"
            echo "  --enable-cstates   Enable all C-states (default: disabled)"
            echo "Example: $0 800000 2000000"
            exit 0
            ;;
        *)
            break
            ;;
    esac
done

# Check for required parameters
if [ $# -ne 2 ]; then
    echo "Usage: $0 [--enable-cstates] <min_freq_kHz> <max_freq_kHz>" >&2
    exit 1
fi

MIN_FREQ=$1
MAX_FREQ=$2

# ----------------------------------------------------------------------
# Helper functions to identify target CPUs (same as original script)
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

        if grep -q userspace $policy/scaling_available_governors; then
            echo userspace > $policy/scaling_governor
            echo "  Governor set to userspace"
        else
            echo "  ERROR: userspace governor not available for policy $policy - aborting."
            exit 1
        fi

        hw_min=$(cat $policy/cpuinfo_min_freq)
        hw_max=$(cat $policy/cpuinfo_max_freq)
        if [ $MIN_FREQ -lt $hw_min ] || [ $MIN_FREQ -gt $hw_max ] || \
           [ $MAX_FREQ -lt $hw_min ] || [ $MAX_FREQ -gt $hw_max ] || \
           [ $MIN_FREQ -gt $MAX_FREQ ]; then
            echo "  ERROR: requested range [$MIN_FREQ, $MAX_FREQ] kHz not within hardware limits [$hw_min, $hw_max] kHz."
            exit 1
        fi

        echo $MIN_FREQ > $policy/scaling_min_freq
        echo $MAX_FREQ > $policy/scaling_max_freq
        echo "  Frequency range set to [$MIN_FREQ, $MAX_FREQ] kHz"

        # Optionally lock frequency to MIN_FREQ (or MAX_FREQ) by writing to scaling_setspeed.
        # Uncomment the following line if you want a fixed frequency.
        # echo $MIN_FREQ > $policy/scaling_setspeed
    else
        echo "Skipping policy $policy (CPUs: $cpus) - not in target set"
    fi
done

# ----------------------------------------------------------------------
# 3. Configure C-states and other per-CPU tunables
# ----------------------------------------------------------------------
echo ""
if [ $ENABLE_CSTATES -eq 1 ]; then
    echo "Enabling all C-states (writing 0 to disable files)"
else
    echo "Disabling all C-states (writing 1 to disable files) - some states may not be disableable"
fi

for cpu in /sys/devices/system/cpu/cpu[0-9]*; do
    n=${cpu#/sys/devices/system/cpu/cpu}
    if cpu_is_target "$n"; then
        if [ -e $cpu/power/energy_perf_bias ]; then
            echo performance > $cpu/power/energy_perf_bias
        fi
        if [ -f $cpu/power/pm_qos_resume_latency_us ]; then
            echo 4294967295 > $cpu/power/pm_qos_resume_latency_us
        fi
        if [ -d $cpu/cpuidle ]; then
            for state in $cpu/cpuidle/state*; do
                if [ -f "$state/disable" ]; then
                    if [ $ENABLE_CSTATES -eq 1 ]; then
                        echo 0 | tee "$state/disable" > /dev/null 2>/dev/null  	# enable state
                    else
                        echo 1 | tee "$state/disable" > /dev/null 2>/dev/null 	# disable state (silent on failure)
                    fi
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
# 5. (Optional) Show C-state status for target CPUs
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


