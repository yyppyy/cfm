#!/bin/bash
ip addr add 10.10.10.221/32 dev ens10
ifconfig ens10 mtu 9000
ifconfig ens10 up

# local host and vm
# ping -c 3 10.10.10.101
# ping -c 3 10.10.21.51 # switch vm
ip route add 10.10.10.0/24 dev ens10
# arp -i ens9 -s 10.10.10.221 04:3f:72:a2:b7:3a
arp -i ens10 -s 10.10.10.201 04:3f:72:a2:b4:a2
ping -c 3 10.10.10.201  # switch vm
# ping -c 3 10.10.10.201  # compute vm (may not be used anymore)

ibv_devinfo
