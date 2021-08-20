# Introduction
This repo is a modified version of fastswap cfm to compare fastswap and MIND.

# Setup and pre-requisites

On the client node the fastswap kernel and driver must be loaded. On the far memory node the server binary `rmserver` must be running. Please see https://github.com/clusterfarmem/fastswap for more details.

## General pre-requisites

You'll need python3, grpcio, grpcio-tools, numpy and scipy to execute various parts of our framework. Please make sure your python environment can see these modules.

## Workload setup (for single and multi-workload benchmarks)

* test_program
    * test program that replay memory access logs


## General pre-requisites
trace files must be first collected(see more at https://github.com/shsym/mind_ae/tree/master/tools/prepare_traces), then storaged under one directory altogether and named as 1, 2,...,(# cores - 1) respectively.

## Setting up cgroups
### Disable cgroup v1
* Open /boot/grub/grub.cfg in your editor of choice
* Find the `menuentry` for the fastswap kernel
* Add `cgroup_no_v1=memory` to the end of the line beginning in `linux   /boot/vmlinuz-4.11.0-sswap`
* Save and exit the file
* Run: sudo update-grub
* Reboot

### Enable cgroup v2
The framework and scripts rely on the cgroup system to be mounted at /cgroup2. Perform the following actions:
* Run `sudo mkdir /cgroup2` to create root mount point
* Execute `setup/init_bench_cgroups.sh`
    * Mounts cgroup system
    * Changes ownership of the mount point (and all nested files) to the current user
    * Enables prefetching

## Protocol Buffers
We use [the grpc framework](https://grpc.io) and [protocol buffers](https://developers.google.com/protocol-buffers/docs/pythontutorial) to communicate between the scheduler and servers. The messages that we've defined are in `protocol/protocol.proto`. To generate them the corresponding `.py` files, execute the following command in the `protocol` directory:
    
    source gen_protocol.sh

# Run evaluation
## Get started

Always use the following command to run the evaluation.

    ./benchmark.py test_program 1

## Parameters
All parameters in our test program are defined as members of class `Testprogram` under `lib/workloads.py`.
* cfm_dir: the location of your local repo
* trace_dir: the location of trace files
* num_threads: number of cores assigned to the test program
* loc_mem: local memory size in MB
* app: application to replay, currently including `tf(tensorflow)`, `gc(graphchi)`, `ma(memcached+YCSB workload a)` and `mc(memcached+YCSB workload c)` 

## Results
the test program will generate two files that record overall running time and memory access latency cdf respectively under `./test_program/`
