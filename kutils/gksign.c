#include "common.h"
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
                "usage: gksign [-h] [id] < public-key > signed-public-key\n"
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

	while ((arg = getopt(argc,argv,"h")) != EOF)
	switch (arg) {
	case 'h':
	case '?': usage();
	}

	if (optind != argc) check = gale_text_from(gale_global->enc_cmdline,argv[optind++],-1);
	if (optind != argc || isatty(0) || isatty(1)) usage();

	if (getuid() != geteuid()) {
		struct passwd *pwd = getpwuid(getuid());
		struct gale_text domain = gale_var(G_("GALE_DOMAIN"));
		if (!pwd) gale_alert(GALE_ERROR,G_("who are you?"),0);
		if (check.l)
			gale_alert(GALE_WARNING,G_("ignoring specified key name"),0);
		check = gale_text_concat(3,
			gale_text_from(gale_global->enc_sys,pwd->pw_name,-1),
			G_("@"),domain);
	}

	if (!_ga_load(0,&key)) 
		gale_alert(GALE_ERROR,G_("could not read input"),errno);
	_ga_import_pub(&id,key,&inode,IMPORT_TRUSTED);
	if (!id) gale_alert(GALE_ERROR,G_("could not import public key"),0);

	if (check.l && gale_text_compare(check,id->name))
		gale_alert(GALE_ERROR,G_("permission denied to sign public key"),0);
	if (id->pub_signer)
		_ga_warn_id(G_("key \"%\" already signed"),id);

	_ga_sign_pub(id,gale_time_forever()); /* change me! */
	if (!id->pub_signer) gale_alert(GALE_ERROR,G_("cannot sign public key"),0);

	_ga_export_pub(id,&key,EXPORT_NORMAL);
	if (!_ga_save(1,key))
		gale_alert(GALE_ERROR,G_("could not write signed public key"),errno);

	return 0;
}
