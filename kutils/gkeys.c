#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "gale/all.h"

void usage(void) {
	fprintf(stderr,
		"%s\n"
		"usage: gkeys [-h] [id]\n"
		"flags: -h          Display this message\n"
		"gkeys generates a key pair if one doesn't exist.\n"
		,GALE_BANNER);
	exit(1);
}

int main(int argc,char *argv[]) {
	if (argc > 2 || (argc > 1 && !strcmp(argv[1],"-h"))) usage();
	if (argc > 1) {
		if (argv[1][0] == '-') usage();
		gale_set(G_("GALE_ID"),gale_text_from_local(argv[1],-1));
	}
	gale_init("gkeys",argc,argv);
	gale_user();
	return 0;
}
