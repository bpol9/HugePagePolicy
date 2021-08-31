#ifndef _HEADER_H

#define _HEADER_H

#define FILENAMELENGTH		50
#define LINELENGTH		2000
#define PAGE_SIZE		4096
/* For a process to be eligible for THP considerations,
 * it must have anon memory size of at least this value.
 * Its current value is 300MB (based on heuristics).
 */
#define	ELIGIBILITY_THRESHOLD	      1000000
#define	IS_CONSIDERABLE		      	  1
#define THRESHOLD_REMOTE_MEMORY_RATIO 10
#define RSS_DIFF_KB 5120 // 5MB

#define OFFLINE_PROFILER_FILE "/home/bill/hpp-offline-profiler/local_mem_overheads.txt"

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
 * skip		This is an optional field to ignore processes
 *	 		that should not be considered for THP. An example usage
 * 			can be small memory processes (e.g. < 200MB ). It can be
 * 			used to filter out background services (if any).
 *			Currently, we are seeing that some processes do not have an
 *			entry in /proc. We can't do anything about such processes at this
 *			point. However, it is still being kept aroud just in case a proc
 * 			entry appears for it later.
 *
 * rss[2]   rss[0] is the last rss value retrieved for the process (current),
 *          rss[1] is the last but one rss value (previous). We keep track of
 *          these values so that we can compare them. If rss[0] <= rss[1],
 *          we assume that the initialization phase of the process is over.
 *
 * mem_overhead_local   The memory overhead of the process when executed 
 * 					    with 100% of its virtual memory mapped to local node.
 						This is provided from the results of an offline exeuction.

 * skip_rss_check   This field indicates not to check for rss changes in this
 *					process because it was just discovered.
 *
 * local_node_id  The id of the numa node where the process is executed.
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


