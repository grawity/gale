#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "gale/all.h"

void *gale_malloc(size_t size) { return malloc(size); }
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
	gale_init("gkeys",argc,argv);
	if (argc > 2) usage();
	if (argc > 1) {
		if (argv[1][0] == '-') usage();
		user_id = lookup_id(gale_text_from_local(argv[1],-1));
	}
	gale_keys();
	return 0;
}
