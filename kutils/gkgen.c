#include "gale/all.h"

#include "id.h"
#include "key.h"
#include "file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void usage(void) {
        fprintf(stderr,
                "%s\n"
                "usage: gkgen [-h] id 'comment' > key\n"
		"flags: -h          Display this message\n"
                ,GALE_BANNER);
	exit(1);
}

int main(int argc,char *argv[]) {
	struct auth_id *id;
	struct gale_data blob;
	int arg;

	gale_init("gkgen",argc,argv);
	while ((arg = getopt(argc,argv,"h")) != EOF)
	switch (arg) {
	case 'h':
	case '?': usage();
	}

	if (argc != 3 || isatty(1)) usage();

	init_auth_id(&id,gale_text_from_local(argv[1],-1));
	auth_id_gen(id,gale_text_from_local(argv[2],-1));
	_ga_export_pub(id,&blob,EXPORT_TRUSTED);
	if (blob.p) _ga_save(1,blob);
	return 0;
}
