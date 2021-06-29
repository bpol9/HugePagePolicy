#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main() {
	size_t size = 20UL * (1 << 30);
	fprintf(stderr, "Allocating 20 GB..\n");
	void *p = malloc(size);
	memset(p, 0, size);
	fprintf(stderr, "Done.\n");
	fprintf(stderr, "Press 'f' to exit: ");
	int c;
	while ((c=getchar()) != 'f') {
		while ((c=getchar()) != '\n') { }
		fprintf(stderr, "Press 'f': ");
	}
	fprintf(stderr, "\n");
	fprintf(stderr, "Exiting..\n");

	return 0;
}
