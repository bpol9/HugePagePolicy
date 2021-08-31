#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/perf_event.h>
#include <errno.h>
#include <string.h>
#include <asm/unistd.h>
#include "header.h"

#define SIZE_LONG	8
unsigned long DTLB_LOAD_MISSES_WALK_DURATION = 0;
unsigned long DTLB_STORE_MISSES_WALK_DURATION = 0;
unsigned long CPU_CLK_UNHALTED = 0;
unsigned long CYCLE_ACTIVITY_STALLS_L2_MISS = 0;
//unsigned long DTLB_LOAD_MISSES_WALK_COMPLETED = 0;
//unsigned long DTLB_STORE_MISSES_WALK_COMPLETED = 0;
//unsigned long MEM_LOAD_UOPS_L3_MISS_RETIRED_LOCAL_DRAM = 0;
//unsigned long MEM_LOAD_UOPS_L3_MISS_RETIRED_REMOTE_DRAM = 0;

static long perf_event_open(struct perf_event_attr *hw_event,
		pid_t pid, int cpu, int group_fd, unsigned long flags)
{
	int ret;

	ret = syscall(__NR_perf_event_open, hw_event, pid, cpu,
						group_fd, flags);
	return ret;
}

/*
 * Write event codes depending on the microarchitecture we are on.
 * Event codes are hardcoded.
 */
int init_perf_event_masks()
{
	char march[50];

	/*
	FILE *fp;
	fp = popen("cat /sys/devices/cpu/caps/pmu_name", "r");
	if (fp == NULL) {
		printf("Can't read /sys/devices/cpu/caps/pmu_name\n");
		return -1;
	}
	fgets(march, sizeof(march), fp); // read microarchitecture
	march[strlen(march) - 1] = '\0'; // convert newline character to terminating null byte
	printf("Detected microarchitecture: %s\n", march);
	*/
	strcpy(march, "broadwell");
	
	if (strcmp(march, "broadwell") == 0) {
		printf("Setting event masks for %s microarchitecture.\n", march);
		DTLB_LOAD_MISSES_WALK_DURATION = 0x1531008;
		DTLB_STORE_MISSES_WALK_DURATION = 0x1531049;
		CPU_CLK_UNHALTED = 0x53003C;
		CYCLE_ACTIVITY_STALLS_L2_MISS = 0x55305a3;
		//DTLB_LOAD_MISSES_WALK_COMPLETED = 0x530e08;
		//DTLB_STORE_MISSES_WALK_COMPLETED = 0x530e49;
		//MEM_LOAD_UOPS_L3_MISS_RETIRED_LOCAL_DRAM = 0x5301d3;
		//MEM_LOAD_UOPS_L3_MISS_RETIRED_REMOTE_DRAM = 0x5304d3;
	}
	else if (strcmp(march, "skylake") == 0) {
		DTLB_LOAD_MISSES_WALK_DURATION = 0x531008;
		DTLB_STORE_MISSES_WALK_DURATION = 0x531049;
		CPU_CLK_UNHALTED = 0x53003C;
		CYCLE_ACTIVITY_STALLS_L2_MISS = 0;
		//DTLB_LOAD_MISSES_WALK_COMPLETED = 0x530e08;
		//DTLB_STORE_MISSES_WALK_COMPLETED = 0x530e49;
		//MEM_LOAD_UOPS_L3_MISS_RETIRED_LOCAL_DRAM = 0x5301d3;
		//MEM_LOAD_UOPS_L3_MISS_RETIRED_REMOTE_DRAM = 0x5302d3;
	}

	//fclose(fp);
	return 0;
}

static int set_perf_raw_event(struct perf_event_attr *attr, unsigned long event)
{
	memset(attr, 0, sizeof(struct perf_event_attr));
	attr->type = PERF_TYPE_RAW;
	attr->size = sizeof(struct perf_event_attr);
	attr->config = event;
	attr->disabled = 1;
	attr->exclude_kernel = 1;
	attr->exclude_hv = 1;
}

static int set_perf_hw_event_cycles(struct perf_event_attr *attr)
{
	memset(attr, 0, sizeof(struct perf_event_attr));
	attr->type = PERF_TYPE_HARDWARE;
	attr->size = sizeof(struct perf_event_attr);
	attr->config = PERF_COUNT_HW_CPU_CYCLES;
	attr->disabled = 1;
	attr->exclude_kernel = 1;
	attr->exclude_hv = 1;
}

/* The following 3 are utility functions. They reset, start and stop counters respectively given
 * the file descriptors for the events.
 */
static inline void
reset_events(int *fds)
{
	int i;
	for (i=0; i<NUM_PERF_EVENTS; i++) {
		ioctl(fds[i], PERF_EVENT_IOC_RESET, 0);
	}
}

static inline void
enable_events(int *fds)
{
	int i;
	for (i=0; i<NUM_PERF_EVENTS; i++) {
		ioctl(fds[i], PERF_EVENT_IOC_ENABLE, 0);
	}
}

static inline void
disable_events(int *fds)
{
	int i;
	for (i=0; i<NUM_PERF_EVENTS; i++) {
		ioctl(fds[i], PERF_EVENT_IOC_DISABLE, 0);
	}
}

/*
 * Utility function that closes the file descriptors of event counters.
 */
static inline void
close_files(int *fds)
{
	int i;
	for (i=0; i<NUM_PERF_EVENTS; i++) {
		close(fds[i]);
	}
}

/*
 * Reads necessary performance counters and computes memory overhead for the proc process.
 * Then, we subtract from this value the offline-computed memory overhead (with 100% locality)
 * and thus, we make an estimation for the overhead of remote memory accesses.
 */
int get_remote_mem_overhead(struct process *proc)
{
	unsigned long l2_miss_stalls = 0;
	unsigned long cpu_cycles = 0;
	int mem_overhead;
	int ret;

	ret = read(proc->fds[L2_MISS_STALLS], &l2_miss_stalls, SIZE_LONG);
	if (ret != SIZE_LONG)
		goto failure;

	ret = read(proc->fds[CPU_CYCLES], &cpu_cycles, SIZE_LONG);
	if (ret != SIZE_LONG || cpu_cycles == 0)
		goto failure;

	mem_overhead = (l2_miss_stalls * 100) / cpu_cycles;
	//printf("get_remote_mem_overhead: l2_miss_stalls: %lu, cpu_cycles: %lu mem_overhead: %lu, mem_overhead_local: %lu\n",
	//	l2_miss_stalls, cpu_cycles, mem_overhead, proc->mem_overhead_local);
	proc->remote_mem_overhead = mem_overhead - proc->mem_overhead_local;
	return 0;

failure:
	return -1;
}

/*
 * Reads relevant performance counters and computes address translation overhead for 
 * the proc process. The overhead is stored in the struct.
 */
int get_translation_overhead(struct process *proc)
{
	unsigned long load_walk_duration = 0, store_walk_duration = 0;
	unsigned long cpu_cycles = 0;
	int ret;

	ret = read(proc->fds[DTLB_LOAD_MISS_CYCLES], &load_walk_duration, SIZE_LONG);
	if (ret != SIZE_LONG)
		goto failure;

	ret = read(proc->fds[DTLB_STORE_MISS_CYCLES], &store_walk_duration, SIZE_LONG);
	if (ret != SIZE_LONG)
		goto failure;

	ret = read(proc->fds[CPU_CYCLES], &cpu_cycles, SIZE_LONG);
	if (ret != SIZE_LONG || cpu_cycles == 0)
		goto failure;

	/* TODO: Check for overflow conditions */
	//printf("PID: %d  TranslationCycles: %lu  CpuCycles: %lu\n", proc->pid, load_walk_duration+store_walk_duration, cpu_cycles);
	proc->trans_overhead = ((load_walk_duration + store_walk_duration) * 100) / cpu_cycles;
	return 0;

failure:
	//printf("get_translation_overhead failed. load_walk_duration: %lu, store_walk_duration: %lu, cpu_cycles: %lu\n",
	//	load_walk_duration, store_walk_duration, cpu_cycles);
	return -1;
}

/*
 * Starts performance counters for the processes in procs list.
 * The following perf counters are used:
 * 1. DTLB_LOAD_MISSES_WALK_DURATION (cycles at which the page table walker is active servicing a tlb load miss)
 * 2. DTLB_STORE_MISSES_WALK_DURATION (cycles at which the page table walker is active servicing a tlb store miss)
 * 3. CYCLE_ACTIVITY.STALLS_L2_MISS (execution stalls while data from L3 Cache or DRAM is waited for)
 * 4. CPU_CLK_UNHALTED (cpu cycles)
 */
void start_counters(struct process *procs)
{
	struct perf_event_attr pe_load, pe_store, pe_cycles, pe_mem_stalls;
	struct process *curr = procs;
	pid_t pid;
	int i;

	if (curr == NULL)
		return;

	set_perf_raw_event(&pe_load, DTLB_LOAD_MISSES_WALK_DURATION);
	set_perf_raw_event(&pe_store, DTLB_STORE_MISSES_WALK_DURATION);
	set_perf_raw_event(&pe_cycles, CPU_CLK_UNHALTED);
	set_perf_raw_event(&pe_mem_stalls, CYCLE_ACTIVITY_STALLS_L2_MISS);

	// Iterate over processes in the procs_ready_to_prof list and start their counters
	while (curr != NULL) {
		pid = curr->pid;
		curr->fds[DTLB_LOAD_MISS_CYCLES] = perf_event_open(&pe_load, pid, -1, -1, 0);
		curr->fds[DTLB_STORE_MISS_CYCLES] = perf_event_open(&pe_store, pid, -1, -1, 0);
		curr->fds[CPU_CYCLES] = perf_event_open(&pe_cycles, pid, -1, -1, 0);
		curr->fds[L2_MISS_STALLS] = perf_event_open(&pe_mem_stalls, pid, -1, -1, 0);

		for (i=0; i<NUM_PERF_EVENTS; i++) {
			if (curr->fds[i] == -1) {
				perror("perf_event_open");
			}
		}

		printf("Starting counters for process with pid %d\n", pid);
		reset_events(curr->fds);
		enable_events(curr->fds);

		curr = curr->next;
	}
}

/*
 * Stops performance counters for process proc.
 */
void stop_counters(struct process *proc)
{
	struct process *curr = proc;
	while (curr != NULL) {
		printf("Stoping counters for process with pid %d\n", curr->pid);
		disable_events(proc->fds);
		curr = curr->next;
	}
}

/*
 * Closes performance counters for process proc.
 */
void close_counters(struct process *proc)
{
	close_files(proc->fds);
}
