
jcarlon@treebeard:~/zzz_test/dpdk-tester/collector$ cat README.txt 
## avere privilegi root
## avere settato 'chmod +x ./test_runner.sh'

nohup ./test_runner.sh &

questo dovrebbe continuare a girare per sempre su treebeard until completion
jcarlon@treebeard:~/zzz_test/dpdk-tester/collector$ nohup ./test_runner.sh & 
[1] 2977779
jcarlon@treebeard:~/zzz_test/dpdk-tester/collector$ nohup: ignoring input and appending output to 'nohup.out'

jcarlon@treebeard:~/zzz_test/dpdk-tester/collector$ 
jcarlon@treebeard:~/zzz_test/dpdk-tester/collector$ ps aux | grep test_runner
jcarlon  2977779  0.0  0.0  10472  3200 pts/0    S    12:12   0:00 /bin/bash ./test_runner.sh
jcarlon  2980195  0.0  0.0   9680  1920 pts/0    S+   12:12   0:00 grep --color=auto test_runner
jcarlon@treebeard:~/zzz_test/dpdk-tester/collector$ ps aux | grep dpdk
jcarlon  2980234  0.0  0.0   8720  1600 pts/0    S    12:12   0:00 tee /home/jcarlon/zzz_test/dpdk-tester/collector/res/measurement_20260523_121300.log
jcarlon  2980310  0.0  0.0   9676  1920 pts/0    S+   12:13   0:00 grep --color=auto dpdk
jcarlon@treebeard:~/zzz_test/dpdk-tester/collector$ ps aux | grep dpdk
jcarlon  2980234  0.0  0.0   8720  1600 pts/0    S    12:12   0:00 tee /home/jcarlon/zzz_test/dpdk-tester/collector/res/measurement_20260523_121300.log
jcarlon  2980324  0.0  0.0   9676  1920 pts/0    S+   12:13   0:00 grep --color=auto dpdk
jcarlon@treebeard:~/zzz_test/dpdk-tester/collector$ ps aux | grep dpdk
jcarlon  2980234  0.0  0.0   8720  1600 pts/0    S    12:12   0:00 tee /home/jcarlon/zzz_test/dpdk-tester/collector/res/measurement_20260523_121300.log
jcarlon  2980335  0.0  0.0   9676  1920 pts/0    S+   12:13   0:00 grep --color=auto dpdk
jcarlon@treebeard:~/zzz_test/dpdk-tester/collector$ ps aux | grep dpdk
jcarlon  2980234  0.0  0.0   8720  1600 pts/0    S    12:12   0:00 tee /home/jcarlon/zzz_test/dpdk-tester/collector/res/measurement_20260523_121300.log
root     2980348  0.0  0.0  19796  6720 pts/0    S    12:13   0:00 sudo chrt -f 99 /home/jcarlon/zzz_test/dpdk-tester/collector/../../dpdk/build/examples/dpdk-l3fwd-power -l 2 -- --pmd-mgmt=baseline --busypolling_pause_duration_ns=1 -p 0x3 --config=(0,0,2),(1,0,2)
root     2980350  0.0  0.0  19796  2172 pts/1    Ss+  12:13   0:00 sudo chrt -f 99 /home/jcarlon/zzz_test/dpdk-tester/collector/../../dpdk/build/examples/dpdk-l3fwd-power -l 2 -- --pmd-mgmt=baseline --busypolling_pause_duration_ns=1 -p 0x3 --config=(0,0,2),(1,0,2)
root     2980351 91.0  0.0 134394716 13120 pts/1 RLl  12:13   0:10 /home/jcarlon/zzz_test/dpdk-tester/collector/../../dpdk/build/examples/dpdk-l3fwd-power -l 2 -- --pmd-mgmt=baseline --busypolling_pause_duration_ns=1 -p 0x3 --config=(0,0,2),(1,0,2)
jcarlon  2980412  9.7  0.0  17784  8320 pts/0    S    12:13   0:00 ssh -o StrictHostKeyChecking=no -o ConnectTimeout=5 -o LogLevel=ERROR jcarlon@cplex3 ?sudo pkill latency_test || true ;         sudo rm -f /tmp/latency_temp_baseline_256_2440000.log /tmp/latency_pid_uniform_256_2440000.pid ; ?sudo rm -rf /var/run/dpdk/* /dev/shm/dpdk* ; ?sudo sleep 5 ;         cd /home/jcarlon/test/dpdk-tester/generator &&         ( nohup sudo ./latency_test -l 2,4,6 -- -s 256   -B 32  -p uniform    -r 2440000      > /tmp/latency_temp_baseline_256_2440000.log 2>&1 & ) ;         sleep 1 ; ?pid=$(pgrep -f 'latency_test -l') ;         echo $pid > /tmp/latency_pid_uniform_256_2440000.pid ; ?sleep 1 ;         sudo chmod 644 /tmp/latency_temp_baseline_256_2440000.log /tmp/latency_pid_uniform_256_2440000.pid
jcarlon  2980419  0.0  0.0   9676  1920 pts/0    S+   12:13   0:00 grep --color=auto dpdk
jcarlon@treebeard:~/zzz_test/dpdk-tester/collector$ ps aux | grep dpdk
jcarlon  2980234  0.0  0.0   8720  1600 pts/0    S    12:12   0:00 tee /home/jcarlon/zzz_test/dpdk-tester/collector/res/measurement_20260523_121300.log
root     2980348  0.0  0.0  19796  6720 pts/0    S    12:13   0:00 sudo chrt -f 99 /home/jcarlon/zzz_test/dpdk-tester/collector/../../dpdk/build/examples/dpdk-l3fwd-power -l 2 -- --pmd-mgmt=baseline --busypolling_pause_duration_ns=1 -p 0x3 --config=(0,0,2),(1,0,2)
root     2980350  0.0  0.0  19796  2172 pts/1    Ss+  12:13   0:00 sudo chrt -f 99 /home/jcarlon/zzz_test/dpdk-tester/collector/../../dpdk/build/examples/dpdk-l3fwd-power -l 2 -- --pmd-mgmt=baseline --busypolling_pause_duration_ns=1 -p 0x3 --config=(0,0,2),(1,0,2)
root     2980351 95.9  0.0 134394716 13120 pts/1 RLl  12:13   0:22 /home/jcarlon/zzz_test/dpdk-tester/collector/../../dpdk/build/examples/dpdk-l3fwd-power -l 2 -- --pmd-mgmt=baseline --busypolling_pause_duration_ns=1 -p 0x3 --config=(0,0,2),(1,0,2)
jcarlon  2980534  0.0  0.0  10472  3520 pts/0    S    12:13   0:00 /bin/bash /home/jcarlon/zzz_test/dpdk-tester/collector/run_rapl.sh -y -r -c 32 -s 1 /home/jcarlon/zzz_test/dpdk-tester/collector/res/baseline_uniform_32pkt_256B_4997120000b_Baseline_pauseDuration1_uniform_baseline_freq1200000_power
root     2980538  0.0  0.0  19792  6720 pts/0    S    12:13   0:00 sudo ./rapl_logger.sh 32 /home/jcarlon/zzz_test/dpdk-tester/collector/res/data_baseline_uniform_32pkt_256B_4997120000b_Baseline_pauseDuration1_uniform_baseline_freq1200000_power 1
root     2980539  0.0  0.0  19792  1968 pts/2    Ss+  12:13   0:00 sudo ./rapl_logger.sh 32 /home/jcarlon/zzz_test/dpdk-tester/collector/res/data_baseline_uniform_32pkt_256B_4997120000b_Baseline_pauseDuration1_uniform_baseline_freq1200000_power 1
root     2980540  0.0  0.0  10340  3200 pts/2    S    12:13   0:00 /bin/bash ./rapl_logger.sh 32 /home/jcarlon/zzz_test/dpdk-tester/collector/res/data_baseline_uniform_32pkt_256B_4997120000b_Baseline_pauseDuration1_uniform_baseline_freq1200000_power 1
jcarlon  2980561  0.0  0.0   9676  1920 pts/0    S+   12:13   0:00 grep --color=auto dpdk
jcarlon@treebeard:~/zzz_test/dpdk-tester/collector$ ps aux | grep dpdk
jcarlon  2980234  0.0  0.0   8720  1600 pts/0    S    12:12   0:00 tee /home/jcarlon/zzz_test/dpdk-tester/collector/res/measurement_20260523_121300.log
root     2980348  0.0  0.0  19796  6720 pts/0    S    12:13   0:00 sudo chrt -f 99 /home/jcarlon/zzz_test/dpdk-tester/collector/../../dpdk/build/examples/dpdk-l3fwd-power -l 2 -- --pmd-mgmt=baseline --busypolling_pause_duration_ns=1 -p 0x3 --config=(0,0,2),(1,0,2)
root     2980350  0.0  0.0  19796  2172 pts/1    Ss+  12:13   0:00 sudo chrt -f 99 /home/jcarlon/zzz_test/dpdk-tester/collector/../../dpdk/build/examples/dpdk-l3fwd-power -l 2 -- --pmd-mgmt=baseline --busypolling_pause_duration_ns=1 -p 0x3 --config=(0,0,2),(1,0,2)
root     2980351 97.4  0.0 134394716 13120 pts/1 RLl  12:13   0:36 /home/jcarlon/zzz_test/dpdk-tester/collector/../../dpdk/build/examples/dpdk-l3fwd-power -l 2 -- --pmd-mgmt=baseline --busypolling_pause_duration_ns=1 -p 0x3 --config=(0,0,2),(1,0,2)
jcarlon  2980534  0.0  0.0  10472  3520 pts/0    S    12:13   0:00 /bin/bash /home/jcarlon/zzz_test/dpdk-tester/collector/run_rapl.sh -y -r -c 32 -s 1 /home/jcarlon/zzz_test/dpdk-tester/collector/res/baseline_uniform_32pkt_256B_4997120000b_Baseline_pauseDuration1_uniform_baseline_freq1200000_power
root     2980538  0.0  0.0  19792  6720 pts/0    S    12:13   0:00 sudo ./rapl_logger.sh 32 /home/jcarlon/zzz_test/dpdk-tester/collector/res/data_baseline_uniform_32pkt_256B_4997120000b_Baseline_pauseDuration1_uniform_baseline_freq1200000_power 1
root     2980539  0.0  0.0  19792  1968 pts/2    Ss+  12:13   0:00 sudo ./rapl_logger.sh 32 /home/jcarlon/zzz_test/dpdk-tester/collector/res/data_baseline_uniform_32pkt_256B_4997120000b_Baseline_pauseDuration1_uniform_baseline_freq1200000_power 1
root     2980540  0.1  0.0  10340  3200 pts/2    S    12:13   0:00 /bin/bash ./rapl_logger.sh 32 /home/jcarlon/zzz_test/dpdk-tester/collector/res/data_baseline_uniform_32pkt_256B_4997120000b_Baseline_pauseDuration1_uniform_baseline_freq1200000_power 1
jcarlon  2980736  0.0  0.0   9676  1920 pts/0    S+   12:13   0:00 grep --color=auto dpdk
jcarlon@treebeard:~/zzz_test/dpdk-tester/collector$ 


