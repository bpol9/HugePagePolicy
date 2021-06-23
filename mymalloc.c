#define _GNU_SOURCE

#include <stdio.h>
#include <numaif.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <numa.h>
#include <errno.h>
#include <dlfcn.h>
//#include <sys/syscall.h>

struct node_desc {
	unsigned long long huge_page_bytes; // amount of free bytes in the node backed up by huge pages
	int distance_from_local; // distance from node that made the allocation request
	int id;
	unsigned long long bound_bytes; // amount of process virtual memory that has been bound to this node
	struct remote_virtual_range *vr_list;
};

struct node_desc *nodes = NULL; // array that holds node descriptors
int local_nid = -1;
int num_numa_nodes = -1;

/*
 * Sorts node descriptors by id.
 *
 */
void sort_nodes_by_id() {
	int i, j = num_numa_nodes - 1;
	int nr_swaps;
	struct node_desc tmp;

	do {
		nr_swaps = 0;
		for (i=0; i<j; i++) {
			if (nodes[i].id > nodes[i+1].id) {
				tmp = nodes[i];
				nodes[i] = nodes[i+1];
				nodes[i+1] = tmp;
				++nr_swaps;
			}
		}
		--j;
	} while (nr_swaps > 0);
}


/*
 * Sorts node descriptors by distance to local node in ascending order (shorter distances first).
 *
 */
void sort_nodes_by_distance_asc() {
	int i, j = num_numa_nodes - 1;
	int nr_swaps;
	struct node_desc tmp;

	do {
		nr_swaps = 0;
		for (i=0; i<j; i++) {
			if (nodes[i].distance_from_local > nodes[i+1].distance_from_local) {
				tmp = nodes[i];
				nodes[i] = nodes[i+1];
				nodes[i+1] = tmp;
				++nr_swaps;
			}
		}
		--j;
	} while (nr_swaps > 0);
}

/*
 * Sorts node descriptors by distance to local node in descending order (shorter distances first).
 *
 */
void sort_nodes_by_distance_desc() {
	int i, j = num_numa_nodes - 1;
	int nr_swaps;
	struct node_desc tmp;

	do {
		nr_swaps = 0;
		for (i=0; i<j; i++) {
			if (nodes[i].distance_from_local < nodes[i+1].distance_from_local) {
				tmp = nodes[i];
				nodes[i] = nodes[i+1];
				nodes[i+1] = tmp;
				++nr_swaps;
			}
		}
		--j;
	} while (nr_swaps > 0);
}

/*
 * Fills node descriptors with distance from node with id *local_nid*.
 *
 */
void HPP_init_nodes() {
	int i;

	for (i=0; i<num_numa_nodes; i++) {
		nodes[i].id = i;
	}

	for (i=0; i<num_numa_nodes; i++) {
		nodes[i].distance_from_local = numa_distance(nodes[i].id, local_nid);
	}

	return;
}

/*
 * Fills node descriptors with their huge page capacity.
 *
 */
void get_free_huge_page_bytes() {
	size_t TWO_MEGABYTES = 2 * 1024 * 1024;
	size_t FOUR_MEGABYTES = 2 * TWO_MEGABYTES;
	char buffer[BUFSIZ], c;
	int buddy_fd = open("/proc/buddyinfo", O_RDONLY);
	if (buddy_fd < 0) {
		perror("open buddyinfo");
		exit(EXIT_FAILURE);
	}

	sort_nodes_by_id(); // thanks to this, we can use node id as index in the nodes array
	ssize_t length = read(buddy_fd, buffer, sizeof buffer);
	int i = 0;
	int x = 0, y, j;
	int order9, order10;
	while (x < length) {
		while (buffer[x] != ' ') x++; // pass over 'Node' label
		while (buffer[x] == ' ') x++;

		i = buffer[x] - '0'; // node id, doesn't handle the case where id is double digit

		x++;
		while (buffer[x] == ',' || buffer[x] == ' ') x++;
		while (buffer[x] != ' ') x++; // pass over 'zone' label
		while (buffer[x] == ' ') x++;

		y = x;
		while (buffer[y] != ' ') y++;
		buffer[y] = '\0';
		if (strcmp(&buffer[x], "Normal") != 0) { // any other acceptable zone must be added here
			buffer[y] = ' ';
			while (buffer[y] != '\n') y++; // this line contains info about zone we don't care, skip it
			y++;
			x = y;
			//continue;
		}
		else {
			order9 = order10 = 0;
			buffer[y] = ' ';

			x = y;
			while (buffer[x] == ' ') x++;
			// ignore first 9 buddy orders (0-8)
			for (j=0; j<9; j++) { 
				while (buffer[x] != ' ') x++;
				while (buffer[x] == ' ') x++;
			}

			// Take care of order-9 blocks
			while (buffer[x] != ' ') {
				c = buffer[x++];
				//printf("Parsing order9, got char %c\n", c);
				order9 *= 10;
				order9 += c - '0';
			}
			//printf("Order9: %d\n", order9);

			while (buffer[x] == ' ') x++;

			// Take care of order-10 blocks
			while (buffer[x] != ' ' && buffer[x] != '\n') {
				c = buffer[x++];
				//printf("Parsing order10, got char %c\n", c);
				order10 *= 10;
				order10 += c - '0';
			}

			// we can use node id as index in nodes array because of previous sorting by id
			nodes[i].huge_page_bytes = order9 * TWO_MEGABYTES + order10 * FOUR_MEGABYTES;
			//printf("Order10: %d\n", order10);

			while (buffer[x] != '\n') x++;
			x++;
		}
	}
}

void * malloc_with_huge_pages(size_t bytes) {
	size_t HUGE_PAGE_SIZE = 2 * 1024 * 1024UL; // 2MB
	//size_t BASE_PAGE_SIZE = 4 * 1024UL; // 4KB

	get_free_huge_page_bytes();
	sort_nodes_by_distance_asc();

	void *ret;
	int res = posix_memalign(&ret, HUGE_PAGE_SIZE, bytes); 
	if (res != 0) {
		return NULL;
	}

	size_t bytes_left = bytes;
	size_t bind_len;
	unsigned long nodemask;
	void *start = ret;
	int i = 0;
	//printf("[malloc_with_huge_pages] num_numa_nodes: %d\n", num_numa_nodes);
	while (bytes_left > 0 && i < num_numa_nodes) {
		if (nodes[i].huge_page_bytes > 0) {
			bind_len = nodes[i].huge_page_bytes > bytes_left ? bytes_left : nodes[i].huge_page_bytes;
			nodemask = 1 << nodes[i].id;
			//printf("[malloc_with_huge_pages] nodemask: %lu\n", nodemask);
			if (mbind(start, bind_len, MPOL_PREFERRED, &nodemask, 8, 0) == 0) {
				if (i == 0) { // i=0 when we are on local node
					//mem_info.num_base_pages_local += bind_len / BASE_PAGE_SIZE;
				}
				else { // we are on a remote node
					//add_range_to_list(start, bind_len, nodes[i].id);
					//mem_info.num_base_pages_remote += bind_len / BASE_PAGE_SIZE;
				}

				start += bind_len;
				bytes_left -= bind_len;
			}
		}

		++i;
	}

	//numa_set_strict(0);
	//numa_tonode_memory(ret, bytes, 1);
	
	memset(ret, 0, bytes);

	return ret;
}

void *malloc(size_t size)
{
    //unsigned long HUGE_PAGE_SIZE = 2 * 1024 * 1024;
    static void *(*mallocp)(size_t size);
    char *error;
    void *ptr = NULL;
    //unsigned long nodemask = 1 << 1;
    //unsigned long mod;

    //fprintf(stderr, "malloc_wrapper entered\n");
    /* get address of libc malloc */
    if (!mallocp) {
	mallocp = dlsym(RTLD_NEXT, "malloc");
	if ((error = dlerror()) != NULL) {
	    fputs(error, stderr);
	    exit(1);
	}
    }
    /*
    ptr = mallocp(size);
    mod = (unsigned long)ptr % HUGE_PAGE_SIZE;
    if (mod != 0) {
	    ptr = (void *)((unsigned long)ptr - mod + HUGE_PAGE_SIZE);
	    size = size - (HUGE_PAGE_SIZE - mod);
    }
    posix_memalign(&ptr, HUGE_PAGE_SIZE, size);
    if (size > 500 * (1 << 20)) {
       fprintf(stderr, "malloc(%ld) = %p\n", size, ptr);
       //fprintf(stderr, "malloc: tid: %ld\n", syscall(SYS_gettid));
       if (mbind(ptr, size, MPOL_PREFERRED, &nodemask, 8, 0) < 0) {
          fprintf(stderr, "mbind: %s\n", strerror(errno));
       }
    }
    */
    if (size > 500 * (1 << 20)) {
	    if (local_nid == -1) {
		fprintf(stderr, "HPP initialization\n");
		local_nid = numa_node_of_cpu(sched_getcpu());
		num_numa_nodes = numa_max_node() + 1;
		nodes = (struct node_desc *)mallocp(num_numa_nodes * sizeof(struct node_desc));
		HPP_init_nodes();
	    }
	    fprintf(stderr, "Calling malloc_with_huge_pages\n");
	    ptr = malloc_with_huge_pages(size);
    }
    else {
	    ptr = mallocp(size);
    }
    //fprintf(stderr, "malloc(%ld) = %p\n", size, ptr);     
    return ptr;
}

void free(void *ptr)
{
    static void (*freep)(void *);
    char *error;

    /* get address of libc free */
    if (!freep) {
	freep = dlsym(RTLD_NEXT, "free");
	if ((error = dlerror()) != NULL) {
	    fputs(error, stderr);
	    exit(1);
	}
    }
    //fprintf(stderr, "free(%p)\n", ptr);     
    freep(ptr);
}
