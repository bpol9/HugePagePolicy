/*
 * Dummy program to check the malloc wrapper of rhp (remote huge pages).
 * First, (assuming a numa machine with 10G per node) cause huge page fragmentation to local node with
 * numactl --physcpubind=<some_cpu_of_node_x> --localalloc -- ./break_huge_pages 7G 4K
 * Secondly, set the LD_PRELOAD environment variable
 * export LD_PRELOAD="/usr/lib/x86_64-linux-gnu/libdl.so /path/to/rhp.so"
 * Finally, run the program with
 * numactl --physcpubind=<some_cpu_of_node_x> --localalloc -- ./test_rhp
 * and check the memory placement from /proc/<pid_of_test_rhp>/numa_maps 
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main() {
	size_t size = 5UL * (1 << 30);
	fprintf(stderr, "Allocating 5 GB..\n");
	void *p = malloc(size);
	memset(p, 0, size);
	fprintf(stderr, "Done.\nPress 'f' to exit: ");

	int c;
	while ((c=getchar()) != 'f') {
		while ((c=getchar()) != '\n') { }
		fprintf(stderr, "Press 'f': ");
	}
	fprintf(stderr, "\nExiting..\n");

	return 0;
}
