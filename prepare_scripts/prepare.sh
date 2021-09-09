#enable ip
~/.enable_ip2.sh

#fastswap setup
cd ~/fastswap/drivers
rm fastswap_rdma.ko
rm fastswap.ko
make BACKEND=RDMA
#sudo rmmod fastswap.ko
#sudo rmmod fastswap_rdma.ko
sudo insmod fastswap_rdma.ko sport=50000 sip="10.10.10.221" cip="10.10.10.201" nq=8
sudo insmod fastswap.ko

#mount VFS
#cd ~/yanpeng/cfm
#./setup/init_bench_cgroups.sh
#./test_program/mount_all.sh

#disable huge page
echo never > /sys/kernel/mm/transparent_hugepage/enabled

#sudo cset set -s test_program -c 0-11
#sudo cset set -s other -c 12-23
#sudo cset proc --move --fromset=root --toset=other -k --force
