ip addr add 10.10.10.201/32 dev ens9
ifconfig ens9 mtu 9000
ifconfig ens9 up

# local host and vm
# ping -c 3 10.10.10.101
# ping -c 3 10.10.21.51 # switch vm
ip route add 10.10.10.0/24 dev ens9
arp -i ens9 -s 10.10.10.221 04:3f:72:a2:b7:3a
#arp -i ens9 -s 10.10.10.201 04:3f:72:a2:b4:a2
arp -i ens9 -s 10.10.10.212 04:3f:72:a2:b4:3b
ping -c 3 10.10.10.221  # switch vm
# ping -c 3 10.10.10.201  # compute vm (may not be used an#

ibv_devinfo
