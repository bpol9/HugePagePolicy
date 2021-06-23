#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main() {
	fprintf(stderr, "1\n");
	fprintf(stderr, "2\n");
	void *p = malloc(10000000000);
	fprintf(stderr, "3\n");
	memset(p, 0, 10000000000);
	fprintf(stderr, "Press 'f' to exit: ");
	int c;
	while ((c=getchar()) != 'f') {
		while ((c=getchar()) != '\n') { }
		fprintf(stderr, "Press 'f': ");
	}
	printf("\n");
	fprintf(stderr, "Exiting..\n");

	return 0;
}
