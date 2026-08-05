#!/bin/bash

# author: Tommaso Cucinotta <tommaso.cucinotta@santannapisa.it>
#   possibly expanded by : me.

## WIP: Work In Progress

### --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- 
### --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- 
### --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- 

set_hrtick() {
    for sf in /sys/kernel/debug/sched_features /sys/kernel/debug/sched/features; do
        if [ -f $sf ]; then
            echo HRTICK > $sf
            if grep -q HRTICK_DL $sf; then
                echo HRTICK_DL > $sf
            fi
            break;
        fi
    done
}

unset_hrtick() {
    for sf in /sys/kernel/debug/sched_features /sys/kernel/debug/sched/features; do
        if [ -f $sf ]; then
            if grep -q HRTICK_DL $sf; then
                echo NO_HRTICK_DL > $sf
            fi
            echo NO_HRTICK > $sf
            break;
        fi
    done
}


### --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- 
### --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- 
### --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- 


disable_hyperthreads() {
    for c in $(seq 0 $[ $(nproc --all) - 1 ]); do
        if grep '^'$c'\($\|,\|-\)' /sys/devices/system/cpu/cpu$c/topology/core_cpus_list > /dev/null 2>&1; then
            echo "Leaving CPU $c enabled"
        else
            echo "Disabling CPU $c"
            echo 0 > /sys/devices/system/cpu/cpu$c/online
        fi
    done
}

enable_hyperthreads() {
    for c in $(seq 0 $[ $(nproc --all) - 1 ]); do
        if grep '^'$c'\($\|,\|-\)' /sys/devices/system/cpu/cpu$c/topology/core_cpus_list > /dev/null 2>&1; then
            echo "Leaving CPU $c enabled"
        else
            echo "Enabling CPU $c"
            echo 1 > /sys/devices/system/cpu/cpu$c/online
        fi
    done
}


### --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- 
### --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- 
### --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- 



### --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- 
### --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- 
### --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- 



### --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- 
### --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- 
### --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- 



### --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- 
### --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- 
### --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- 



disable_turbo_boost() {
    if [ -f /sys/devices/system/cpu/cpufreq/boost ]; then
        echo 0 > /sys/devices/system/cpu/cpufreq/boost
    fi

    if [ -d /sys/devices/system/cpu/intel_pstate ]; then
        if [ -e /sys/devices/system/cpu/intel_pstate/no_turbo ]; then
	    echo 1 > /sys/devices/system/cpu/intel_pstate/no_turbo
        fi
    fi
}

enable_turbo_boost() {
    if [ -f /sys/devices/system/cpu/cpufreq/boost ]; then
        echo 1 > /sys/devices/system/cpu/cpufreq/boost
    fi

    if [ -d /sys/devices/system/cpu/intel_pstate ]; then
        if [ -e /sys/devices/system/cpu/intel_pstate/no_turbo ]; then
	    echo 0 > /sys/devices/system/cpu/intel_pstate/no_turbo
        fi
    fi
}



### --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- 
### --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- 
### --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- 


set_max_freq() {
    if [ -f /sys/devices/system/cpu/intel_pstate/max_perf_pct ]; then
        echo 100 > /sys/devices/system/cpu/intel_pstate/max_perf_pct
        echo 100 > /sys/devices/system/cpu/intel_pstate/min_perf_pct
    fi

    grep "" /sys/devices/system/cpu/intel_pstate/*

    if [ -d /sys/devices/system/cpu/cpufreq ]; then
        for p in /sys/devices/system/cpu/cpufreq/policy[0-9]*; do
            if [ -f $p/base_frequency ]; then
                maxfreq=$(cat $p/base_frequency)
            else
                maxfreq=$(cat $p/cpuinfo_max_freq)
            fi
            if grep userspace $p/scaling_available_governors; then
                echo userspace > $p/scaling_governor
                echo $maxfreq > $p/scaling_setspeed
            else
	        echo performance > $p/scaling_governor
	        if [ -f $p/scaling_max_freq ]; then
	            echo $maxfreq > $p/scaling_max_freq
	            echo $maxfreq > $p/scaling_min_freq
	        fi
	        if [ -e $p/energy_performance_preference ]; then
	            echo performance > $p/energy_performance_preference
	        fi
            fi
        done
    fi

    for c in /sys/devices/system/cpu/cpu[0-9]*; do
        if [ -e $c/power/energy_perf_bias ]; then
	    echo performance > $c/power/energy_perf_bias
        fi
    done
}

unset_max_freq() {
    if [ -f /sys/devices/system/cpu/intel_pstate/max_perf_pct ]; then
        echo 100 > /sys/devices/system/cpu/intel_pstate/max_perf_pct
        echo 0 > /sys/devices/system/cpu/intel_pstate/min_perf_pct
    fi

    grep "" /sys/devices/system/cpu/intel_pstate/*

    if [ -d /sys/devices/system/cpu/cpufreq ]; then
        for p in /sys/devices/system/cpu/cpufreq/policy[0-9]*; do
            echo powersave > $p/scaling_governor
	    if [ -f $p/scaling_max_freq ]; then
	        cat $p/cpuinfo_max_freq > $p/scaling_max_freq
	        cat $p/cpuinfo_min_freq > $p/scaling_min_freq
	    fi
	    if [ -e $p/energy_performance_preference ]; then
	        echo balance_performance > $p/energy_performance_preference
	    fi
        done
    fi

    for c in /sys/devices/system/cpu/cpu[0-9]*; do
        if [ -e $c/power/energy_perf_bias ]; then
	    echo normal > $c/power/energy_perf_bias
        fi
    done
}



### --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- 
### --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- 
### --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- 


disable_idle_states() {
    for c in /sys/devices/system/cpu/cpu[0-9]*; do
        if [ -f $c/power/pm_qos_resume_latency_us ]; then
	    echo "n/a" > $c/power/pm_qos_resume_latency_us
        fi
        if [ -d $c/cpuidle ]; then
            for s in $c/cpuidle/state*; do
                echo 1 > $s/disable
            done
        fi
    done
}

enable_idle_states() {
    for c in /sys/devices/system/cpu/cpu[0-9]*; do
        if [ -f $c/power/pm_qos_resume_latency_us ]; then
	    echo 0 > $c/power/pm_qos_resume_latency_us
        fi
        if [ -d $c/cpuidle ]; then
            for s in $c/cpuidle/state*; do
                echo 0 > $s/disable
            done
        fi
    done
}



### --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- 
### --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- 
### --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- --- 

show_settings() {
    echo "#CPU online scaling_governor scaling_cur_freq energy_perf_pref energy_perf_bias resume_latency"
    for c in /sys/devices/system/cpu/cpu[0-9]*; do
        echo $c $(cat $c/online 2> /dev/null || echo 1) $(cat $c/cpufreq/scaling_governor) $(cat $c/cpufreq/scaling_cur_freq) $(cat $c/cpufreq/energy_performance_preference 2> /dev/null) $(cat $c/power/energy_perf_bias 2> /dev/null) $(cat $c/power/pm_qos_resume_latency_us 2> /dev/null)
    done
}
