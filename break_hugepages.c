#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/time.h>
#include <signal.h>
#include <err.h>
#include <fcntl.h>

//#define HUGE_PAGE_SIZE (2 << 20)

void usage(char *msg, FILE *out)
{
	fprintf(out, "Usage %s allocsize mapsize\n", msg);
	fprintf(out, "allocsize should be in the form integer[KMG] and mapsize in the form integer[KM] and less than 2M\n");
	fprintf(out, "Only mapsize of every 2MB of the array will remain mapped. The rest will be freed.\n");
	fprintf(out, "For example, ./break_hugepages 50G 4K will initially allocate 50*512 2MB pages and will keep mapped only 4KB from each.\n");
	exit(out == stderr);
}

void usr_handler(int signal) 
{
	printf("catch signal\n");
	exit(-1);
}

int main(int argc, char **argv)
{
	unsigned long long HUGE_PAGE_SIZE = 2*1024*1024;
	unsigned long long bytes_total, stride, huge_pages_num, free_size;
	int i, c;
	char *end = NULL;
	void *data;
	sigset_t set;
	struct sigaction sa;

	if (argc < 3) {
		usage(argv[0], stderr);
	}

	bytes_total = strtoull(argv[1], &end, 0);
	switch (*end) {
		case 'g':
		case 'G':
			bytes_total *= 1024;
		case 'm':
		case 'M':
			bytes_total *= 1024;
		case 'k':
		case 'K':
			bytes_total *= 1024;
			break;
		default:
			usage(argv[0], stderr);
			break;
	}

	stride = strtoull(argv[2], &end, 0);
	switch (*end) {
		case 'm':
		case 'M':
			stride *= 1024;
		case 'k':
		case 'K':
			stride *= 1024;
			break;
		default:
			usage(argv[0], stderr);
			break;
	}

	if (stride == 0 || stride >= HUGE_PAGE_SIZE) {
		usage(argv[0], stderr);
	}

	free_size = HUGE_PAGE_SIZE - stride;

	//strides_num = bytes_total / stride;
	huge_pages_num = bytes_total / HUGE_PAGE_SIZE;

	sa.sa_flags = 0;
	sa.sa_handler = usr_handler;

	if (sigaction(SIGUSR1, &sa, NULL) == -1)
		errx(1, "sigaction");

	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);

	printf("Creating a single virtual memory area of %llu bytes.\n", bytes_total);
	printf("Number of hugepages: %llu\n", huge_pages_num);
	printf("%lluKB to be unmapped for every 2MB\n", free_size/1024);
	data = mmap(NULL, bytes_total, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (data == (void *)-1) {
		perror("mmap\n");
		exit(1);
	}

	memset(data, 1, bytes_total);

	printf("\nMemory has been mapped\n");
	printf("Press 'f' to cause fragmenation: ");
	while ((c=getchar()) != 'f') {
		while ((c=getchar()) != '\n') { }
		printf("Press 'f' to cause fragmenation: ");
	}
	printf("\n");

	void *start = (void *)((long long)data & ~(HUGE_PAGE_SIZE-1));
	//printf("data pointer as returned by mmap: %p\n", data);
	//printf("start pointer (huge page aligned): %p\n", start);
	//printf("setting stride to 2MB\n");
	//stride = 2*1024*1024;

	for (i=0; i<huge_pages_num-1; i++) {
		if (munmap(start + i*HUGE_PAGE_SIZE, free_size) < 0) {
			perror("munmap");
			exit(1);
		}
	}

	printf("Memory has been fragmented. Pausing... Press Ctlr-C to exit...\n");
	pause();
	sigwaitinfo(&set, NULL);

	return 0;
}
