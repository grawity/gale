#include "common.h"
#include "init.h"
#include "file.h"
#include "key.h"
#include "id.h"

#include "gale/all.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>

void usage(void) {
        fprintf(stderr,
                "%s\n"
                "usage: gksign [-h] [id] < key > signed-key\n"
		"flags: -h          Display this message\n"
                ,GALE_BANNER);
	exit(1);
}

int main(int argc,char *argv[]) {
	struct gale_data key;
	struct auth_id *id;
	struct gale_text check = null_text;
	struct inode inode = _ga_init_inode();
	int arg;

	gale_init("gksign",argc,argv);
	_ga_init();

	while ((arg = getopt(argc,argv,"h")) != EOF)
	switch (arg) {
	case 'h':
	case '?': usage();
	}

	if (optind != argc) check = gale_text_from_local(argv[optind++],-1);
	if (optind != argc || isatty(0) || isatty(1)) usage();

	if (getuid() != geteuid()) {
		struct passwd *pwd = getpwuid(getuid());
		struct gale_text domain = gale_var(G_("GALE_DOMAIN"));
		if (!pwd) gale_alert(GALE_ERROR,"who are you?",0);
		if (check.p)
			gale_alert(GALE_WARNING,"ignoring specified key",0);
		check = gale_text_concat(3,
			gale_text_from_local(pwd->pw_name,-1),
			G_("@"),domain);
	}

	if (!_ga_load(0,&key)) 
		gale_alert(GALE_ERROR,"could not read input",errno);
	_ga_import_pub(&id,key,&inode,IMPORT_TRUSTED);
	if (!id) gale_alert(GALE_ERROR,"could not import public key",0);

	if (check.p && gale_text_compare(check,id->name))
		gale_alert(GALE_ERROR,"permission denied to sign key",0);
	if (id->sig.id)
		_ga_warn_id(G_("key \"%\" already signed"),id);

	_ga_sign_pub(id,gale_time_forever()); /* change me! */
	if (!id->sig.id) gale_alert(GALE_ERROR,"cannot sign key",0);

	_ga_export_pub(id,&key,EXPORT_NORMAL);
	if (!_ga_save(1,key))
		gale_alert(GALE_ERROR,"could not write output",errno);

	return 0;
}
