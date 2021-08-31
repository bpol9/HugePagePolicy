#ifndef _HEADER_H

#define _HEADER_H

#define FILENAMELENGTH		50
#define LINELENGTH		2000
#define PAGE_SIZE		4096
#define	ELIGIBILITY_THRESHOLD	      1000000
#define	IS_CONSIDERABLE		      	  1
#define THRESHOLD_REMOTE_MEMORY_RATIO 10
#define RSS_DIFF_KB 5120 // 5MB

#define OFFLINE_PROFILER_FILE "/home/bill/RHP/profiler/offline_stats.txt"

/*
 * These are used as indexes to fds array-field of struct process.
 */
enum PERF_EVENTS {
	DTLB_LOAD_MISS_CYCLES,
	DTLB_STORE_MISS_CYCLES,
	L2_MISS_STALLS,
	CPU_CYCLES,
	NUM_PERF_EVENTS
};

/*
 * mem_overhead_local   The main memory overhead of the process when executed 
 * 		 	with 100% of its virtual memory mapped to the local node.
 *			This is provided from the results of an offline exeuction.
 *
 * local_node_id  	The id of the numa node where the process is executed.
 *
 * rss_kB		The current value of the resident set size of the process
 * 			in KB. It is compared against the total memory requirements
 * 			of the process (which are known from the offline exexution),
 * 			to determine when the initialization phase is over.
 *
 * fds			File descriptors to read performance counters for the process.
 */
struct process {
	int pid;
	unsigned int anon_size;
	unsigned int anon_thp;
	long rss_kB; // resident set size of the process
	int local_node_id;
	int mem_overhead_local;
	long mem_footprint_kB;
	int trans_overhead;
	int remote_mem_overhead;
	unsigned long timestamp;
	int fds[NUM_PERF_EVENTS];
	struct process *next;
};

#endif


