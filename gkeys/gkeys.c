#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "gale/all.h"

void *gale_malloc(int size) { return malloc(size); }
void gale_free(void *ptr) { free(ptr); }

void usage(void) {
	fprintf(stderr,
		"%s\n"
		"Usage: gkeys [id]\n"
		"Generates a key pair (if one doesn't exist).\n"
		,GALE_BANNER);
	exit(1);
}

int main(int argc,char *argv[]) {
	gale_init("gkeys");
	if (argc > 2) usage();
	if (argc > 1) {
		if (argv[1][0] == '-') usage();
		user_id = lookup_id(argv[1]);
	}
	gale_keys();
	return 0;
}
