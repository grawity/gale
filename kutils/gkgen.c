#include "gale/all.h"

#include "id.h"
#include "key.h"
#include "file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

void usage(void) {
        fprintf(stderr,
                "%s\n"
                "usage: gkgen [-h] [-s id] [-r file] [-u file] id 'comment'\n"
		"flags: -h          Display this message\n"
		"       -n          Do not erase existing keys\n"
		"       -s id       Create a redirector to another id\n"
		"       -r file     Write pRivate key to this file\n"
		"       -u file     Write pUblic key to this file\n"
                ,GALE_BANNER);
	exit(1);
}

int main(int argc,char *argv[]) {
	struct auth_id *id;
	struct gale_text out_pub = null_text,out_priv = null_text,comment;
	struct auth_id *redirect = NULL;
	int arg,trust;
	int do_wipe = 1;

	gale_init("gkgen",argc,argv);
	disable_gale_akd();

	while ((arg = getopt(argc,argv,"hnr:u:s:")) != EOF)
	switch (arg) {
	case 'n': do_wipe = 0; break;
	case 'r': out_priv = gale_text_from_local(optarg,-1); break;
	case 'u': out_pub = gale_text_from_local(optarg,-1); break;
	case 's': init_auth_id(&redirect,gale_text_from_local(optarg,-1)); 
	          break;
	case 'h':
	case '?': usage();
	}

	if (argc - optind != 2) usage();
	comment = gale_text_from_local(argv[optind + 1],-1);

	init_auth_id(&id,gale_text_from_local(argv[optind],-1));
	if (do_wipe) {
		while (auth_id_public(id) && _ga_erase_inode(id->pub_inode)) {
			gale_alert(GALE_NOTICE,
			gale_text_to_local(gale_text_concat(3,
				G_("erased old public key file \""),
				id->pub_inode.name,
				G_("\""))),0);
		}
		while (auth_id_private(id) &&_ga_erase_inode(id->priv_inode)) {
			gale_alert(GALE_NOTICE,
			gale_text_to_local(gale_text_concat(3,
				G_("erased old private key file \""),
				id->priv_inode.name,
				G_("\""))),0);
		}
	}

	if (NULL == redirect)
		auth_id_gen(id,comment);
	else
		auth_id_redirect(id,comment,redirect);

	trust = _ga_trust_pub(id); /* mildly expensive to call */
	if (0 != out_pub.l || !trust) {
		struct inode inode;
		struct gale_text dir = null_text,fn = null_text;
		struct gale_data blob;

		_ga_export_pub(id,&blob,EXPORT_TRUSTED);
		if (0 == blob.l) gale_alert(GALE_ERROR,"no public key to write!",0);
		fn = out_pub;
		if (0 == fn.l) {
			assert(!trust);
			dir = gale_global->dot_gale;
			fn = gale_text_concat(2,id->name,G_(".unsigned"));
		}

		if (!_ga_save_file(dir,fn,0644,blob,&inode))
			gale_alert(GALE_ERROR,"couldn't write public key",0);

		if (trust)
			gale_alert(GALE_NOTICE,
			gale_text_to_local(gale_text_concat(3,
				G_("copying public key to \""),
				inode.name,
				G_("\", as directed"))),0);
		else
			gale_alert(GALE_NOTICE,
			gale_text_to_local(gale_text_concat(5,
				G_("saving *unsigned* public key in \""),
				dir,(dir.l ? G_("/") : dir),inode.name,
				(out_pub.l ? G_("\", as directed") : G_("\"")))),0);
	}

	if (0 != out_priv.l) {
		struct inode inode;
		struct gale_data blob;

		_ga_export_priv(id,&blob);
		if (0 == blob.l) gale_alert(GALE_ERROR,"no private key to write!",0);

		if (!_ga_save_file(null_text,out_priv,0600,blob,&inode))
			gale_alert(GALE_ERROR,"couldn't write private key",0);

		gale_alert(GALE_NOTICE,
		gale_text_to_local(gale_text_concat(3,
			G_("copying private key to \""),
			inode.name,
			G_("\", as directed"))),0);
	}

	return 0;
}
