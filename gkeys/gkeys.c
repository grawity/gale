#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "gale/all.h"

void usage(void) {
	fprintf(stderr,
		"%s\n"
		"Usage: gkeys [id]\n"
		"Generates a key pair (if one doesn't exist).\n"
		,GALE_BANNER);
	exit(1);
}

int main(int argc,char *argv[]) {
	if (argc > 2) usage();
	if (argc > 1) {
		char *tmp;
		if (argv[1][0] == '-') usage();
		tmp = gale_malloc(strlen(argv[1]) + 30);
		sprintf(tmp,"GALE_ID=%s",argv[1]);
		putenv(tmp);
	}
	gale_init("gkeys",argc,argv);
	gale_user();
	return 0;
}
