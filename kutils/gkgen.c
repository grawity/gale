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
                "usage: gkgen [-hn] [-s id] [-t nm=val] [-r file] [-u file] id ['comment']\n"
		"flags: -h          Display this message\n"
		"       -n          Do not erase existing keys\n"
		"       -s id       Create a redirector to another id\n"
		"       -t nm=val   Include text fragment 'nm' set to 'val'\n"
		"       -r file     Write pRivate key to this file\n"
		"       -u file     Write pUblic key to this file\n"
                ,GALE_BANNER);
	exit(1);
}

int main(int argc,char *argv[]) {
	struct auth_id *id;
	struct gale_text out_pub = null_text,out_priv = null_text;
	struct gale_group extra = gale_group_empty();
	struct gale_fragment frag;
	int arg,trust;
	int do_wipe = 1;

	gale_init("gkgen",argc,argv);
	disable_gale_akd();

	while ((arg = getopt(argc,argv,"hnr:u:s:t:")) != EOF) {
	struct gale_text str = !optarg ? null_text :
		gale_text_from(gale_global->enc_cmdline,optarg,-1);
	switch (arg) {
	case 'n': do_wipe = 0; break;
	case 'r': out_priv = str; break;
	case 'u': out_pub = str; break;
	case 's': 
		frag.type = frag_text;
		frag.name = G_("key.redirect");
		frag.value.text = str;
		gale_group_add(&extra,frag);
	        break;
	case 't': 
		frag.type = frag_text;
		frag.name = null_text;
		gale_text_token(str,'=',&frag.name);
		frag.value.text = frag.name;
		if (!gale_text_token(str,'=',&frag.value.text))
			frag.value.text = null_text;
		gale_group_add(&extra,frag);
		break;

	case 'h':
	case '?': usage();
	} }

	frag.type = frag_text;
	frag.name = G_("key.owner");
	switch (argc - optind) {
	case 2: 
		frag.value.text = gale_text_from(
			gale_global->enc_cmdline,
			argv[optind + 1],-1); 
		break;
	case 1: 
		frag.value.text = null_text; 
		break;
	default:  
		usage();
	}
	gale_group_add(&extra,frag);

	init_auth_id(&id,gale_text_from(gale_global->enc_cmdline,argv[optind],-1));
	if (!gale_text_compare(G_("ROOT"),_ga_signer(auth_id_name(id))))
		gale_alert(GALE_WARNING,G_("making top-level domain key!"),0);
	if (do_wipe) {
		while (auth_id_public(id) && _ga_erase_inode(id->pub_inode)) {
			gale_alert(GALE_NOTICE,gale_text_concat(3,
				G_("erased old public key file \""),
				id->pub_inode.name,
				G_("\"")),0);
		}
		while (auth_id_private(id) &&_ga_erase_inode(id->priv_inode)) {
			gale_alert(GALE_NOTICE,gale_text_concat(3,
				G_("erased old private key file \""),
				id->priv_inode.name,
				G_("\"")),0);
		}
	}

	auth_id_gen(id,extra);
	trust = _ga_trust_pub(id); /* mildly expensive to call */
	if (0 != out_pub.l || !trust) {
		struct inode inode;
		struct gale_text dir = null_text,fn = null_text;
		struct gale_data blob;

		_ga_export_pub(id,&blob,EXPORT_TRUSTED);
		if (0 == blob.l) 
			gale_alert(GALE_ERROR,G_("no public key to write!"),0);
		fn = out_pub;
		if (0 == fn.l) {
			assert(!trust);
			dir = gale_global->dot_gale;
			fn = gale_text_concat(2,id->name,G_(".unsigned"));
		}

		if (!_ga_save_file(dir,fn,0644,blob,&inode))
			gale_alert(GALE_ERROR,G_("couldn't write public key"),0);

		if (trust)
			gale_alert(GALE_NOTICE,gale_text_concat(3,
				G_("copying public key to \""),
				inode.name,
				G_("\", as directed")),0);
		else
			gale_alert(GALE_NOTICE,gale_text_concat(5,
				G_("saving *unsigned* public key in \""),
				dir,(dir.l ? G_("/") : dir),inode.name,
				(out_pub.l ? G_("\", as directed") : G_("\""))),0);
	}

	if (0 != out_priv.l) {
		struct inode inode;
		struct gale_data blob;

		_ga_export_priv(id,&blob);
		if (0 == blob.l) 
			gale_alert(GALE_ERROR,G_("no private key to write!"),0);

		if (!_ga_save_file(null_text,out_priv,0600,blob,&inode))
			gale_alert(GALE_ERROR,G_("couldn't write private key"),0);

		gale_alert(GALE_NOTICE,gale_text_concat(3,
			G_("copying private key to \""),
			inode.name,
			G_("\", as directed")),0);
	}

	return 0;
}
