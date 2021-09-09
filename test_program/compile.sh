#1: workload either tensorflow or voltdb
#2: num nodes
#3: node id
#4: num log files
#5~#8: log file id

#PRE=/root/yanpeng/cfm/test_program/$1/$1_
#SUF=_0

g++ ./test_program_fastswap.cpp -O2 -g -o ./test_program_fastswap -lpthread -std=c++11

#./test_program_fastswap $2 $3 $4 ${PRE}${5}${SUF} ${PRE}${6}${SUF} ${PRE}${7}${SUF} ${PRE}${8}${SUF} ${PRE}${9}${SUF} ${PRE}${10}${SUF} ${PRE}${11}${SUF} ${PRE}${12}${SUF} ${PRE}${13}${SUF} ${PRE}${14}${SUF} ${PRE}${15}${SUF} ${PRE}${16}${SUF} ${PRE}${17}${SUF} ${PRE}${18}${SUF} ${PRE}${19}${SUF} ${PRE}${20}${SUF}
