NFS_IP=192.168.122.1
app=$1
if [ $app == 'tf' ]; then
	dir1=tensorflow_logs
	dir2=tf
elif [ $app == 'gc' ]; then
	dir1=graphchi_logs
        dir2=gc
elif [ $app == 'ma' ]; then
	dir1=memcached_a_logs
        dir2=ma
elif [ $app == 'mc' ]; then
	dir1=memcached_c_logs
        dir2=mc
else
	echo "invalid workload"
fi

sudo mount ${NFS_IP}:/media/data_ssds/${dir1} /root/artifact_eval/cfm/test_program/${dir2}
