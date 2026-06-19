!!!

in order to switch tests between unipi and retis, the following files need changing: 



generator/Makefile          (the dpdk path)
generator/latency_test.c    (the MAC addresses)

collector/test_runner.sh    (the throughputs)
collector/exp_auto_grid.sh  (ssh links, and limits)



