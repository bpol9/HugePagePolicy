/*
 * Compile with -lnuma
 */

#include <stdio.h>
#include <stdlib.h> // malloc
#include <numaif.h> // migrate_pages
#include <numa.h>   // numa_*
#include "header.h"

/*
 * Creates an array of nodemasks, excluding the mask corresponding to node id nid.
 * The returned array has to be freed by the caller.
 */
static unsigned long *
get_remote_nodemasks(int nid)
{
	int max_nid = numa_max_node();
	unsigned long *nodemasks = (unsigned long *)malloc(sizeof(unsigned long) * max_nid);
	int i=0, k=0;

	while (i <= max_nid) {
		if (i != nid) {
			nodemasks[k++] = 1 << i;
		}
		++i;
	}

	return nodemasks;
}

/*
 * Migrates all remotely mapped memory of process proc back to the local node.
 */
long migrate_mem_to_local_node(struct process *proc)
{
	pid_t pid = proc->pid;
	unsigned long new_nodes[1];
        new_nodes[0] = 1 << proc->local_node_id;
	unsigned long *old_nodes = get_remote_nodemasks(proc->local_node_id);

	long ret = migrate_pages(pid, 8, old_nodes, new_nodes); // 8 is arbitrary, needs fixing.
	free(old_nodes);
	return ret;
}

/*
 * Gets the id of the node where the process with id pid is executed.
 */
int get_local_node_of_process(pid_t pid)
{
	char comm[50];
	char buf[50];
	int cpu_num;

	sprintf(comm, "ps -o psr -p %d | sed '1d'", pid); // | sed '1d' is used to ommit the first header line of ps
	FILE *fp = popen(comm, "r");
	if (fp == NULL) {
		printf("Could not determine node of process with pid %d\n", pid);
		return -1;
	}

	fgets(buf, 50, fp);
	sscanf(buf, "%d", &cpu_num);

	return numa_node_of_cpu(cpu_num);
}
