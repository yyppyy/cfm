// Test program to allocate new memory
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
//#include "../../include/disagg/config.h"
#include <fstream>
#include <cassert>
#include <map>

#define __TEST_TIME_MEASUREMENT__
#define CPUOFF 0
#define TEST_ALLOC_FLAG MAP_PRIVATE|MAP_ANONYMOUS	// default: 0xef
#define TEST_INIT_ALLOC_SIZE (unsigned long)8 * 1024 * 1024 * 1024 // default: 16 GB
#define TEST_METADATA_SIZE 128

#define LOG_NUM_ONCE (unsigned long)1000
#define LOG_NUM_TOTAL (unsigned long)500000//00
#define MMAP_ADDR_MASK 0xffffffffffff
#define MAX_NUM_THREAD 16
#define SLEEP_THRES_NANOS 10
#define TEST_TO_APP_SLOWDOWN 1
#define TIMEWINDOW_US 100000//00
//mmap log loading
#define LOG_MAP_ALIGN (15 * 4096)

// Test configuration
// #define single_thread_test
//#define meta_data_test

using namespace std;

struct log_header_5B {
        char op;
        unsigned int usec;
}__attribute__((__packed__));

struct RWlog {
        char op;
        union {
                struct {
                        char pad[6];
                        unsigned long usec;
                }__attribute__((__packed__));
                unsigned long addr;
        }__attribute__((__packed__));
}__attribute__((__packed__));

struct Mlog {
        struct log_header_5B hdr;
        union {
                unsigned long start;
                struct {
                        char pad[6];
                        unsigned len;
                }__attribute__((__packed__));
        }__attribute__((__packed__));
}__attribute__((__packed__));

struct Blog {
        char op;
        union {
                struct {
                        char pad[6];
                        unsigned long usec;
                }__attribute__((__packed__));
                unsigned long addr;
        }__attribute__((__packed__));
}__attribute__((__packed__));

struct Ulog {
        struct log_header_5B hdr;
        union {
                unsigned long start;
                struct {
                        char pad[6];
                        unsigned len;
                }__attribute__((__packed__));
        }__attribute__((__packed__));
}__attribute__((__packed__));

#define CDF_BUCKET_NUM 512
struct trace_t {
	/*
	char *access_type;
	unsigned long *addr;
	unsigned long *ts;
	*/
	char *logs;
	unsigned long offset;//mmap offset to log file
	unsigned long len;
	bool done;
	bool write_res;
	char *meta_buf;
	char *data_buf;
	int node_idx;
	int num_nodes;
	int master_thread;
	int tid;
	unsigned long time;
	unsigned long dt;
	FILE *cdf_fp;
	unsigned long cdf_cnt_r[CDF_BUCKET_NUM] = {0};
    	unsigned long cdf_cnt_w[CDF_BUCKET_NUM] = {0};
	int pass;
};
struct trace_t args[MAX_NUM_THREAD];

struct load_arg_t {
	int fd;
	struct trace_t *arg;
	unsigned long ts_limit;
};
struct load_arg_t load_args[MAX_NUM_THREAD];

struct metadata_t {
	unsigned int node_mask;
	unsigned int fini_node_pass[8];
};

// int first;
int num_nodes;
int node_id = -1;
int num_threads;
string progress_file_name;
string latency_file_name;

static int latency_to_bkt(unsigned long lat_in_us)
{
    if (lat_in_us < 100)
        return (int)lat_in_us;
    else if (lat_in_us < 1000)
        return 100 + ((lat_in_us - 100) / 10);
    else if (lat_in_us < 10000)
        return 190 + ((lat_in_us - 1000) / 100);
    else if (lat_in_us < 100000)
        return 280 + ((lat_in_us - 10000) / 1000);
    else if (lat_in_us < 1000000)
        return 370 + ((lat_in_us - 100000) / 10000);
    return CDF_BUCKET_NUM - 1;    // over 1 sec
}


static inline void record_time(struct trace_t *trace, unsigned long dt_op, int is_read)
{
#ifdef __TEST_TIME_MEASUREMENT__
    // if (trace->cdf_fp)
    if (trace)
    {
        if (is_read)
        {
            trace->cdf_cnt_r[latency_to_bkt(dt_op)]++;
            // fprintf(trace->cdf_fp, "R, %lu\n", dt_op);
        }
        else
        {
            trace->cdf_cnt_w[latency_to_bkt(dt_op)]++;
            // fprintf(trace->cdf_fp, "W, %lu\n", dt_op);
        }
    }
#endif
}

static inline void flush_cdf_record(struct trace_t *trace)
{
#ifdef __TEST_TIME_MEASUREMENT__
    if (trace->cdf_fp)
        fflush(trace->cdf_fp);
#endif
}

static void print_cdf(struct trace_t *trace)
{
// #ifdef __TEST_TIME_MEASUREMENT__
    // char progress_text[256] = "";
    int i = 0;
    if (trace && trace->cdf_fp)
    {
	fprintf(trace->cdf_fp, "Pass: %lu\n", trace->pass);
        // read
        fprintf(trace->cdf_fp, "Read:\n");
        for (i = 0; i < CDF_BUCKET_NUM; i++)
            fprintf(trace->cdf_fp, "%lu\n", trace->cdf_cnt_r[i]);
        // write
        fprintf(trace->cdf_fp, "Write:\n");
        for (i = 0; i < CDF_BUCKET_NUM; i++)
            fprintf(trace->cdf_fp, "%lu\n", trace->cdf_cnt_w[i]);
        fprintf(trace->cdf_fp, "\n");
    }
    flush_cdf_record(trace);
// #endif
}

unsigned fls(unsigned x) {
    unsigned r = 32;
    if (!x)
        return 0;
    if (!(x & 0xffff0000u))
        x <<= 16, r -= 16;
    if (!(x & 0xff000000u))
        x <<= 8, r -= 8;
    if (!(x & 0xf0000000u))
        x <<= 4, r -= 4;
    if (!(x & 0xc0000000u))
        x <<= 2, r -= 2;
    if (!(x & 0x80000000u))
        x <<= 1, r -= 1;
    return r;
}

static int calc_mask_sum(unsigned int mask)
{
	int sum = 0;
	while (mask > 0)
	{
		if (mask & 0x1)
			sum++;
		mask >>= 1;
	}
	return sum;
}

int init(struct trace_t *trace)
{
	if(trace && trace->meta_buf)
	{
		struct metadata_t *meta_ptr = (struct metadata_t *)trace->meta_buf;
		// write itself
		if (trace->master_thread)
		{
			unsigned int node_mask = (1 << (trace->node_idx));
			meta_ptr->node_mask |= node_mask;
		}
		// check nodes
		int i = 0;
		while (calc_mask_sum(meta_ptr->node_mask) < trace->num_nodes)
		{
			//if (i % 100 == 0)
			//	printf("Waiting nodes: %d [0x%x]\n", trace->num_nodes, meta_ptr->node_mask);
#ifdef meta_data_test
			meta_ptr->node_mask |= (1 << (trace->node_idx));	// TEST PURPOSE ONLY
#endif
			usleep(20 * 1000);	// wait 20 ms
			i++;
		}
		//printf("All nodes are initialized: %d [0x%x]\n", trace->num_nodes ,meta_ptr->node_mask);
		return 0;
	}
	return -1;
}

int fini(struct metadata_t *meta_buf, int num_nodes, int node_id, int pass) {
	meta_buf->node_mask &= ~(1 << node_id);
	meta_buf->fini_node_pass[node_id] = pass;

	bool all_done = false;
        int i = 0;
        while (!all_done) {
		all_done = true;
		for (int j = 0; j < num_nodes; ++j)
			if (meta_buf->fini_node_pass[j] != pass)
				all_done = false;
                if (i % 100 == 0) {
                        //printf("Waiting for next pass\n");
                        usleep(20 * 1000);      // wait 20 ms
                }
                ++i;
        }
	return 0;
}

int pin_to_core(int core_id)
{
    int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (core_id < 0 || core_id >= num_cores)
        return -1;

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    pthread_t current_thread = pthread_self();
    return pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
}

inline void interval_between_access(long delta_time_usec) {
	if (delta_time_usec <= 0)
		return;
	else {
		struct timespec ts;
		unsigned long app_time_nsec = (delta_time_usec << 1) / 3;
		if (app_time_nsec > SLEEP_THRES_NANOS) {
			ts.tv_sec = 0;
			ts.tv_nsec = app_time_nsec * TEST_TO_APP_SLOWDOWN;
			nanosleep(&ts, NULL);
		}
	}
}

static inline unsigned long calculate_dt(struct timeval *ts)
{
    unsigned long old_t = ts->tv_sec * 1000000 + ts->tv_usec;
    gettimeofday(ts, NULL);
    return ts->tv_sec * 1000000 + ts->tv_usec - old_t;

}

static inline void measure_time_start(struct timeval *ts)
{
#ifdef __TEST_TIME_MEASUREMENT__
    gettimeofday(ts, NULL);
#endif
}

static inline unsigned long measure_time_end(struct timeval *ts)
{
#ifdef __TEST_TIME_MEASUREMENT__
    return calculate_dt(ts);
#endif
}


void do_log(void *arg)
{
	struct trace_t *trace = (struct trace_t*) arg;

	//pin to core first
	pin_to_core(trace->tid + CPUOFF);
	//cache off

	unsigned len = trace->len;
	if (init(trace))
	{
		fprintf(stderr, "Initialization error!\n");
		return;
	}

#ifndef meta_data_test
	multimap<unsigned int, void *> len2addr;
	unsigned long old_ts = 0;
	unsigned long i = 0;

	struct timeval ts;
	struct timeval ts_op;
	gettimeofday(&ts, NULL);
	struct timespec start, end;
	char *cur;
	//prefetch pages
	//memset(trace->data_buf, 0, TEST_INIT_ALLOC_SIZE - TEST_METADATA_SIZE);
	for (i = 0; i < trace->len ; ++i) {
		volatile char op = trace->logs[i * sizeof(RWlog)];
		cur = &(trace->logs[i * sizeof(RWlog)]);
		if (op == 'R') {
			struct RWlog *log = (struct RWlog *)cur;
			interval_between_access(log->usec - old_ts);
			//assert((log->addr & MMAP_ADDR_MASK) < TEST_INIT_ALLOC_SIZE);
			//char val = trace->data_buf[log->addr & MMAP_ADDR_MASK];
			char *data_buf = trace->data_buf;
			unsigned long addr = log->addr & MMAP_ADDR_MASK;
			measure_time_start(&ts_op);
			char val = data_buf[addr];
			unsigned long dt_op = measure_time_end(&ts_op);
			record_time(trace, dt_op, 1);
			old_ts = log->usec;
		} else if (op == 'W') {
			struct RWlog *log = (struct RWlog *)cur;
			interval_between_access(log->usec - old_ts);
			//assert((log->addr & MMAP_ADDR_MASK) < TEST_INIT_ALLOC_SIZE);
			//trace->data_buf[log->addr & MMAP_ADDR_MASK] = 0;
			char *data_buf = trace->data_buf;
			unsigned long addr = log->addr & MMAP_ADDR_MASK;
			measure_time_start(&ts_op);
			data_buf[addr] = 0;
			unsigned long dt_op = measure_time_end(&ts_op);
			record_time(trace, dt_op, 0);
			old_ts = log->usec;
		} else if (op == 'M') {
			struct Mlog *log = (struct Mlog *)cur;
			interval_between_access(log->hdr.usec);
			void *ret_addr = mmap((void *)(log->start & MMAP_ADDR_MASK), log->len, PROT_READ|PROT_WRITE, TEST_ALLOC_FLAG, -1, 0);
			unsigned int len = log->len;
			len2addr.insert(pair<unsigned int, void *>(len, ret_addr));
			old_ts += log->hdr.usec;
		} else if (op == 'B') {
			struct Blog *log = (struct Blog *)cur;
			interval_between_access(log->usec - old_ts);
			brk((void *)(log->addr & MMAP_ADDR_MASK));
			old_ts = log->usec;
		} else if (op == 'U') {
			struct Ulog *log = (struct Ulog *)cur;
			interval_between_access(log->hdr.usec);
			multimap<unsigned int, void *>::iterator itr = len2addr.find(log->len);
			if (itr == len2addr.end())
//				printf("no mapping to unmap\n");
				;
			else {
				munmap(itr->second, log->len);
				len2addr.erase(itr);
			}
			old_ts += log->hdr.usec;
		} else {
			//printf("unexpected log: %c at line: %lu\n", op, i);
		}
		//if (i % 1000000 == 0)
		//	printf("%lu\n", i);
	}

/*
	volatile unsigned j;
	for (j = 0; j < len; ++j);
*/
	unsigned long old_t = ts.tv_sec * 1000000 + ts.tv_usec;
	gettimeofday(&ts, NULL);
	unsigned long dt = ts.tv_sec * 1000000 + ts.tv_usec - old_t;
	
#endif
	//printf("done in %lu us\n", dt);
	trace->time += dt;
	trace->dt = dt;
	//printf("total run time is %lu us\n", trace->time);
	
	//for mmap log loading
	//munmap(trace->logs, trace->len * sizeof(RWlog));
	print_cdf(trace);
}

int load_trace(void *void_arg) {
	
	struct load_arg_t *load_arg = (struct load_arg_t *)void_arg;
	int fd = load_arg->fd;
	struct trace_t *arg = load_arg->arg;	
	unsigned long ts_limit = load_arg->ts_limit;

	//printf("ts_limit: %lu, offset: %lu\n", ts_limit, arg->offset);
	assert(sizeof(RWlog) == sizeof(Mlog));
	assert(sizeof(RWlog) == sizeof(Blog));
	assert(sizeof(RWlog) == sizeof(Ulog));

	if (arg->logs) {
	//	printf("munmap %p\n", arg->logs);
		munmap(arg->logs, LOG_NUM_TOTAL * sizeof(RWlog));
	}
	arg->logs = (char *)mmap(NULL, LOG_NUM_TOTAL * sizeof(RWlog), PROT_READ, MAP_PRIVATE, fd, arg->offset);
	//printf("arg->logs: %p, fd: %d\n", arg->logs, fd);

	unsigned long new_offset = 0;
	//walk through logs to find the end of timewindow also trigger demand paging
	for (char *cur = arg->logs; cur != arg->logs + LOG_NUM_TOTAL * sizeof(RWlog); cur += sizeof(RWlog)) {
		if (*cur == 'R' || *cur == 'W' || *cur == 'B') {
			if (((struct RWlog *)cur)->usec >= ts_limit && !new_offset)
				new_offset = (arg->offset + (cur - arg->logs)) / LOG_MAP_ALIGN * LOG_MAP_ALIGN;
			if (new_offset)
				break;
		} else if (*cur == 'M' || *cur == 'U') {
			continue;
		} else {
			new_offset = (arg->offset + (cur - sizeof(RWlog) - arg->logs)) / LOG_MAP_ALIGN * LOG_MAP_ALIGN;
			//printf("unexpected op %c\n", *cur);
			arg->done = true;
                       	break;
		}
	}
	//if offset is the same as the old one due to align
	if (arg->offset != new_offset)
		assert(new_offset);
	else
		new_offset = arg->offset + LOG_MAP_ALIGN;
	//printf("new_offset: %lu\n", new_offset);
	arg->len = (new_offset - arg->offset) / sizeof(RWlog);
	arg->offset = new_offset;
	/*
	for (char *buf = chunk; true; buf += LOG_NUM_ONCE * sizeof(RWlog)) {
		size_t dsize = read(fd, buf, LOG_NUM_ONCE * sizeof(RWlog));
		if (dsize == 0)
			break;
		if (dsize % sizeof(RWlog) != 0)
			printf("dsize is :%lu\n", dsize);
		size += dsize;
		
		char *tail = buf + dsize - sizeof(RWlog);
		unsigned long last_ts = 0;
		while (tail - buf >= 0) {
			if (*tail == 'R' || *tail == 'W' || *tail == 'B')
				last_ts = ((struct RWlog *)tail)->usec;
			else if (*tail == 'M' || *tail == 'U') {
				tail -= sizeof(RWlog);
				continue;
			} else
				printf("unexpected op %c\n", *tail);
			break;
		}
		if (last_ts >= ts_limit)
			break;
	}
	assert(size <= LOG_NUM_TOTAL * sizeof(RWlog));
	//assert(size % sizeof(RWlog) == 0);
	arg->len = size / (sizeof(RWlog));
*/
	//printf("finish loading %lu logs\n", arg->len);

	return 0;
}
/*
void print_res(char *trace_name, struct trace_t *trace) {
	FILE *fp;
	fp = fopen(trace_name, "w");
	if (!fp) {
		printf("fail to open res file\n");
		return;
	}
	//printf("%d\n", size);
	for (int i = 0; i < trace->len; ++i) {
	//	printf("%d %hhu\n", i, buf[i]);
		//fprintf(fp, "%c %lu %hhu\n", trace->access_type[i], trace->addr[i], trace->val[i]);
	}
	fclose(fp);
}
*/
enum
{
	arg_node_cnt = 1,
	arg_node_id = 2,
	arg_num_threads = 3,
	arg_progress_file = 4,
	arg_latency_file = 5,
	arg_log1 = 6,
};

int main(int argc, char **argv)
{
	// const int ALLOC_SIZE = 9999 * 4096;
	//starts from few pages, dense access
   	int ret;
	char *buf_test = NULL;
	if (argc < arg_log1)
	{
		fprintf(stderr, "Incomplete args\n");
		return 1;
	}
	num_nodes = atoi(argv[arg_node_cnt]);
	node_id = atoi(argv[arg_node_id]);
	num_threads = atoi(argv[arg_num_threads]);
	progress_file_name = string(argv[arg_progress_file]);
	latency_file_name = string(argv[arg_latency_file]);
	//printf("Num Nodes: %d, Num Threads: %d\n", num_nodes, num_threads);
	if (argc != arg_log1 + num_threads) {
		fprintf(stderr, "thread number and log files provided not match\n");
                return 1;
	}

	//open files
	for (int i = 0; i < num_threads; ++i) {
		load_args[i].fd = open(argv[arg_log1 + i], O_RDONLY);
		if (load_args[i].fd < 0) {
			printf("fail to open log file\n");
			return 1;
		}
		load_args[i].arg = &args[i];
	}
	FILE *res = fopen("tmp.txt", "w");
	FILE *progress = fopen(progress_file_name.c_str(), "w");
	
	//get start ts
	struct RWlog first_log;
	unsigned long start_ts = -1;
	for (int i = 0; i < num_threads; ++i) {
                int size = read(load_args[i].fd, &first_log, sizeof(RWlog));
		start_ts = min(start_ts, first_log.usec);
        }
	//fprintf(res, "start ts is: %lu\n", start_ts);

	// init traces
	// ====== NOTE =====
        // If we make access to the memory ranges other than this size with TEST_ALLOC_FLAG flag,
        // it will generate segmentation fault
        // =================
        char *meta_buf = (char *)mmap(NULL, TEST_INIT_ALLOC_SIZE, PROT_READ | PROT_WRITE, TEST_ALLOC_FLAG, -1, 0);
        if (!meta_buf) {
                printf("can not allocate test buff\n");
                //return 1;
        } else {
                //printf("meta buf: %p\n", meta_buf);
		//memset(meta_buf, 0, TEST_INIT_ALLOC_SIZE);
        }
//	for(int i = 0; i < 10; ++i) {
//		printf("sleeping %d, please run the same executable on other machines now...\n", i);
//		sleep(1);
//	}


	/* put this back when run with switch */
	/*
	buf_test = (char *)mmap(NULL, TEST_INIT_ALLOC_SIZE, PROT_READ | PROT_WRITE, TEST_ALLOC_FLAG, -1, 0);
	// arg1.buf = arg2.buf = (char *)mmap(NULL, ALLOC_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	//memset(arg.buf, -1, ALLOC_SIZE);
	printf("protocol testing buf addr is: %p\n", arg1.meta_buf);
	if (buf_test != arg1.meta_buf)
	{
		fprintf(stderr, "Protocol testing buf verification failed: %p <-> %p\n", arg1.meta_buf, buf_test);
		return 1;
	}
	*/
        for (int i = 0; i < num_threads; ++i) {
                args[i].node_idx = node_id;
                args[i].meta_buf = meta_buf;
                args[i].data_buf = meta_buf + TEST_METADATA_SIZE;
                args[i].num_nodes = num_nodes;
                args[i].master_thread = (i == 0);
                args[i].tid = i;
		args[i].time = 0;
		args[i].dt = 0;
		args[i].cdf_fp = fopen((latency_file_name + to_string(i)).c_str(), "w");
		if (!args[i].cdf_fp) {
			printf("can not open timer file\n");
			return 1;
		}
		//args[i].logs = (char *)malloc(LOG_NUM_TOTAL * sizeof(RWlog));
        	//if (!args[i].logs)
                //	printf("fail to alloc buf to hold logs\n");
        }

	//start load and run logs in time window
	unsigned long pass = 0;
	unsigned long ts_limit = start_ts;
	unsigned long tot_run_time = 0;
	while (pass < 1500000) {
		ts_limit += TIMEWINDOW_US;

		pthread_t thread[MAX_NUM_THREAD];

		fprintf(progress, "Pass[%lu] Node[%d]: ", pass, node_id);

		unsigned long longest_time = 0;
		for (int i = 0; i < num_threads; ++i) {
			longest_time = max(longest_time, args[i].dt);
		}
		tot_run_time += longest_time;

		fprintf(progress, "%lu", tot_run_time);
		for (int i = 0; i < num_threads; ++i) {
			fprintf(progress, " %lu", args[i].time);
		}
		fprintf(progress, "\n");
		fflush(progress);		

		for (int i = 0; i < num_threads; ++i) {
			//printf("Thread[%d]: loading log...\n", i);
			//ret = load_trace(fd[i], &args[i], ts_limit);
			//if (ret) {
    			//	printf("fail to load trace\n");
			//}
			load_args[i].ts_limit = ts_limit;
			if (pthread_create(&thread[i], NULL, (void *(*)(void *))load_trace, &load_args[i]))
			{
                                        printf("Error creating thread %d\n", i);
                                        return 1;
			}
		}
		for (int i = 0; i < num_threads; ++i) {
			if (pthread_join(thread[i], NULL)) {
    					printf("Error joining thread %d\n", i);
    					return 2;
			}
		}

#ifdef single_thread_test
		num_threads = 1;
#endif
		for (int i = 0; i < num_threads; ++i) {
			if (!args[i].done) {
				args[i].pass = pass;
				if (pthread_create(&thread[i], NULL, (void *(*)(void *))do_log, &args[i]))
        			{
                			printf("Error creating thread %d\n", i);
                			return 1;
        			}
			} else if (!args[i].write_res){
				fprintf(res, "%lu\n", args[i].time);
				args[i].write_res = true;
			}
		}
		for (int i = 0; i < num_threads; ++i) {
			if (!args[i].done) {
				if (pthread_join(thread[i], NULL)) {
    					printf("Error joining thread %d\n", i);
    					return 2;
				}
			}
		}

		//sync on the end of the time window
#ifdef PROFILE
		if (pass % 1000 == 0) {
			fprintf(timerfile, "Pass[%u] Read\n", pass);
			unsigned ii = 0;
			unsigned tt = 1;
			for (; ii < 33; ++ii, tt *= 2)
				fprintf(timerfile, "%4u [%12u, %12u)  %16u\n", ii, tt >> 1, tt, rcnts[ii]);
			ii = 0;
			tt = 1;
			fprintf(timerfile, "Pass[%u] Write\n", pass);
			for (; ii < 33; ++ii, tt *= 2)
				fprintf(timerfile, "%4u [%12u, %12u)  %16u\n", ii, tt >> 1, tt, wcnts[ii]);
			fflush(timerfile);
		}		
#endif
		//sync on the end of the time window
		++pass;
		fini((metadata_t *)meta_buf, num_nodes, node_id, pass);

		bool all_done = true;
		for (int i = 0; i < num_threads; ++i)
			if (!args[i].done)
				all_done = false;
		if (all_done)
			break;
	}

	for (int i = 0; i < num_threads; ++i) {
		close(load_args[i].fd);
		fclose(args[i].cdf_fp);
	}
	fclose(res);

	//while(1)
	//	sleep(30);

	return 0;
}

