#include <stdbool.h>
#include "header.h"
#include "lib_perf.h"
#include "lib_numa.h"
//#include "lib_thp.h"


#define max(a, b) (((a) < (b)) ? (b) : (a))

unsigned long current_timestamp = 0;

struct process *procs_init_stage = NULL; // list of processes that are in the initialization stage
struct process *procs_ready_to_prof = NULL; // list of processes that have ended init phase and are ready to be profiled
struct process *procs_profiled = NULL; // list of processes that have already been profiled

/*
 * Computes and stores for process proc the following
 * 1. the size of all anonymous regions
 * 2. the size of anonymous regions that are mapped with huge pages
 * 3. the resident set size (also stores the previous rss value)
 */
static int
update_mem_stats(struct process *proc)
{
	FILE *smaps_file = NULL;
	char line[LINELENGTH], smaps_name[FILENAMELENGTH];
	unsigned long anon_size = 0, anon_thp = 0, rss = 0;
	int pid;
	bool error = false;

	pid = proc->pid;
	/* get smaps file for the given pid */
	sprintf(smaps_name, "/proc/%d/smaps", pid);

	smaps_file = fopen(smaps_name, "r");
	if (smaps_file == NULL) {
		error = true;
		goto exit;
	}

	/* read process map line by line */
	while (fgets(line, LINELENGTH, smaps_file) != NULL) {
		char region[LINELENGTH];
		unsigned long size;
		int n, num_pages;

		/* Skip everything other than anon memory mappings and rss */
		if (!strstr(line, "Anon") && !strstr(line, "Rss"))
			continue;

		n = sscanf(line, "%s %ld", region, &size);
		if (n != 2) {
			printf ("Could not read addresses from the line: %d\n", n);
			continue;
		}
		if (strstr(region, "Anonymous"))
			anon_size += size;

		if (strstr(region, "AnonHugePages"))
			anon_thp += size;

		if (strstr(region, "Rss"))
			rss += size;
	}

	proc->anon_size = anon_size;
	proc->anon_thp = anon_thp;
	proc->rss_kB = rss;

exit:
	if (smaps_file)
		fclose(smaps_file);
	if (error) {
		printf("Could not update mem stats for process with pid %d\n", pid);
		return -1;
	}
	else
		return 0;
}

/*
 * Converts memory amount string to kilobytes.
 */
static unsigned long
convert_to_kB(char *amount)
{
	char *end = NULL;
	unsigned long ret = strtoull(amount, &end, 0);
	switch (*end) {
		case 'G':
		case 'g':
			ret *= 1024;
		case 'M':
		case 'm':
			ret *= 1024;
		case 'K':
		case 'k':
			break;
		default:
			ret = -1;
			break;
	}

	return ret;
}

/*
 * Checks if process with id pid is eligible for monitoring, i.e. if it has already been offline-profiled
 * and its memory overhead with 100% local mappings is known. If it is eligible indeed, the retrieved memory
 * overhead is returned.
 * TODO: read the offline profiler output file once in the begining of the system and store its contents
 * in a suitable array. Then we will only have to search for program name in the array rather than reading
 * the file every time the function is called.
 */
static int
get_offline_stats(struct process *proc)
{
	char comm[50];
	char prog[50];
	char line[LINELENGTH];

	sprintf(comm, "ps -o comm -p %d | sed '1d'", proc->pid); // header line is ommited thanks to sed '1d'
	FILE *fp = popen(comm, "r");
	if (fp == NULL) {
		printf("Could not read process command\n");
		pclose(fp);
		return -1;
	}
	fgets(prog, 50, fp);
	prog[strlen(prog)-1] = '\0'; // remove new line character at the end of the prog string
	pclose(fp);

	//printf("Searching offline execution for program %s\n", prog);
	fp = fopen(OFFLINE_PROFILER_FILE, "r");
	if (fp == NULL) {
		printf("Could not open offline profiler file\n");
		return -1;
	}

	while (fgets(line, LINELENGTH, fp) != NULL) {
		char program_name[50], footprint[20];
		int overhead;

		sscanf(line, "%s %d %s", program_name, &overhead, footprint);
		if (strcmp(program_name, prog) == 0) {
			proc->mem_overhead_local = overhead;
			proc->mem_footprint_kB = convert_to_kB(footprint);
			printf("Got offline stats for %s: local_mem_overhead(%d), mem_footprint_kB(%lu)\n",
				prog, proc->mem_overhead_local, proc->mem_footprint_kB);
			fclose(fp);
			return 0;
		}
	}

	fclose(fp);
	return -1;
}

/*
 * Adds the process with id pid in the list of processes that are in the initialization stage.
 */
static int
handle_new_process(pid_t pid)
{
	bool error = false;;
	int ret;
	struct process *proc = NULL;
	proc = (struct process *)malloc(sizeof(struct process));
	proc->pid = pid;
	if (get_offline_stats(proc) < 0) {
		error = true;
		goto exit;
	}
	int local_nid = get_local_node_of_process(pid);
	if (local_nid < 0) {
		printf("Could not get node of process with pid %d\n", pid);
		error = true;
		goto exit;
	}

	proc->local_node_id = local_nid;
	proc->rss_kB = 0;
	update_mem_stats(proc);

exit:
	if (error) {
		proc->next = procs_profiled;
		procs_profiled = proc;
		ret = -1;
	}
	else {
		printf("Discovered new process with pid %d.\n", pid);
		proc->next = procs_init_stage;
		procs_init_stage = proc;
		ret = 0;
	}

	return ret;
}

/*
 * Checks if process with id pid has already been discovered or not.
 * Essentially, it scans all the lists of processes to find one with that pid.
 */
static bool
is_new_process(pid_t pid)
{
	struct process *curr = procs_init_stage;
	while (curr != NULL) {
		if (curr->pid == pid)
			return false;
		curr = curr->next;
	}

	curr = procs_ready_to_prof;
	while (curr != NULL) {
		if (curr->pid == pid)
			return false;
		curr = curr->next;
	}

	curr = procs_profiled;
	while (curr != NULL) {
		if (curr->pid == pid)
			return false;
		curr = curr->next;
	}

	return true;

}

/*
 * For every proccess in the procs_init_stage list, it is checked whether initialization phase is over.
 * If it is, the procces is added to the procs_ready_to_prof list.
 */
static void
handle_procs_at_init_stage()
{
	struct process *curr = procs_init_stage;
	if (curr == NULL)
		return;

	struct process *prev = curr;
	curr = curr->next; // it makes code simpler to examine the first process of the list at the end
	while (curr != NULL) {
		update_mem_stats(curr); // get a fresh rss value

		// If current rss value is close to total footprint as reported by offline profiling, assume initialization phase is over.
		if (curr->mem_footprint_kB - curr->rss_kB < RSS_DIFF_KB) { 
			printf("Process with pid %d finished initialization.\n", curr->pid);
			// Remove the process from the list of processes at init phase
			prev->next = curr->next;

			// Add current process in list of procs that are ready to be profiled
			curr->next = procs_ready_to_prof;
			procs_ready_to_prof = curr;

			curr = prev->next;
		}
		else { // initialization phase not over yet
			prev = curr;
			curr = curr->next;
		}

		/*
		if (curr->skip_rss_check) {
			curr->skip_rss_check = false;
			prev = curr;
			curr = curr->next;
		}
		else if (curr->rss[0] <= curr->rss[1]) { // RSS didn't increase, so *assume* that initilization phase is over.
			printf("Process with pid %d finished initialization. Old RSS value: %u, New RSS value: %u. "
			       "Moving it to procs_ready list.\n", curr->pid, curr->rss[1], curr->rss[0]);
			// Remove the process from the list of initializing processes
			prev->next = curr->next;

			// Add current process in list of procs that are ready to be profiled
			curr->next = procs_ready_to_prof;
			procs_ready_to_prof = curr;

			curr = prev->next;
		}
		else { // RSS did increase, so initialization phase is not over yet.
			prev = curr;
			curr = curr->next;
		}
		*/
	}

	// Time to check the first item of the list that was skipped for code simplicity
	curr = procs_init_stage;
	update_mem_stats(curr);
	if (curr->mem_footprint_kB - curr->rss_kB < RSS_DIFF_KB) {
		printf("Process with pid %d finished initialization.\n", curr->pid);
		procs_init_stage = curr->next;
		curr->next = procs_ready_to_prof;
		procs_ready_to_prof = curr;
	}
}

/*
 * It reads performance counters for the profiled processes and determines what to do
 * with their remote memoey, i.e. migrate it back to local node or not.
 */
static void
handle_profiled_procs()
{
	struct process *curr = procs_ready_to_prof;
	struct process *tmp;

	// Iterate over the just profiled processes and do the following:
	// 1. Read perf counters and compute overhead of remote mem accesses and translation overhead
	// 2. Compare the two overheads and if the former is bigger than the latter, migrate all proc memory to local node.
	// 3. Put the process in the list of profiled processes
	while (curr != NULL) {
		// Part 1
		get_translation_overhead(curr);
		get_remote_mem_overhead(curr);
		printf("PID: %5d  Translation_Overhead: %3d  Remote_Mem_Overhead: %3d\n",
			curr->pid, curr->trans_overhead, curr->remote_mem_overhead);

		// Part 2
		if (curr->remote_mem_overhead > curr->trans_overhead) {
			fprintf(stderr, "Migrating remote memory of process with pid %d to local node... ", curr->pid);
			fflush(stderr);
			migrate_mem_to_local_node(curr);
			printf("Done\n");
		}
		else {
			printf("Memory placement of process with pid %d will remain the same\n", curr->pid);
		}

		// Part 3
		tmp = curr;
		curr = curr->next;
		procs_ready_to_prof = curr;
		tmp->next = procs_profiled;
		procs_profiled = tmp;
	}
}

static void profile_forever(char *usr, int interval)
{
	FILE *fp;
	struct process *head = NULL;
	char line[LINELENGTH], command[LINELENGTH];

	sprintf(command, "ps -o pid -u %s | sed '1d'", usr);
	/*
	if (init_perf_event_masks() < 0) {
		printf("Could not initialize perf event masks. Exiting\n");
		exit(EXIT_FAILURE);
	}
	*/

	while (true) {
		/* get all processes of the current user*/
		fp = popen(command, "r");
		if (fp == NULL) {
			printf("Could not get process list\n");
			exit(EXIT_FAILURE);
		}

		while (fgets(line, 100, fp) != NULL) {
			int pid;

			/* Ignore background deamons */
			if (strstr(line, "sshd") || strstr(line, "bash"))
				continue;

			sscanf(line, "%d", &pid);
			if (is_new_process(pid)) {
				//printf("Got new process with pid %d\n", pid);
				handle_new_process(pid);
				//printf("Handled process with pid %d\n", pid);
			}
		}

		handle_procs_at_init_stage();
		//printf("Handled init phase processes.\n");
		start_counters(procs_ready_to_prof);
		//printf("Started counters for ready processes.\n");
		sleep(interval);
		stop_counters(procs_ready_to_prof);
		//printf("Stopped counters.\n");
		handle_profiled_procs();

		pclose(fp);
		current_timestamp += 1;
		printf("Profiler period %lu completed\n", current_timestamp);
	}
}

int main(int argc, char **argv)
{
	char *usr = NULL;
	int c, interval = 10;

	while ((c = getopt(argc, argv, "i:u:")) != -1) {
		switch(c) {
			case 'u':
				usr = optarg;
				break;
			case 'i':
				interval = atoi(optarg);
				break;
			default:
				printf("Usage: %s [-u username] [-i interval]\n", argv[0]);
				exit(EXIT_FAILURE);
		}
	}

	if (usr == NULL) {
		usr = malloc(sizeof(LINELENGTH));
		if (getlogin_r(usr, LINELENGTH)) {
			printf("Could not retrieve login name.\n");
			exit(EXIT_FAILURE);
		}
	}

	if(init_perf_event_masks()) {
		printf("Unknown machine type\n");
		exit(EXIT_FAILURE);
	}

	profile_forever(usr, interval);
}
