!!!

in order to switch tests between unipi and retis, the following files need changing: 



generator/Makefile          (the dpdk path)
generator/latency_test.c    (the MAC addresses)

collector/test_runner.sh    (the throughputs)
collector/exp_auto_grid.sh  (ssh links, and limits)



[Interface]
PrivateKey = GBmMXHXcxGcvfBbUUnukOos9bre0TT3nO4YruopXhkc=
Address = 10.30.30.85/24
DNS = 10.30.30.1

[Peer]
PublicKey = mXBT6GcugGbr9chP7EExjX+DCSNthODf4zCl5sQsUn4=
EndPoint = 131.114.55.144:443
AllowedIPs = 10.0.64.0/24, 192.168.64.0/24, 10.30.30.1/32
