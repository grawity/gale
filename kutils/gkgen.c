#include "gale/all.h"

#include <errno.h>

static int do_generate = 1;
static struct gale_text priv_file,pub_file;

static void usage(void) {
	fprintf(stderr,
		"%s\n"
		"usage: gkgen [-hnw] [-m id] [-s id] [-t nm=val] [-r file] [-u file] id [/\"name\"]\n"
		"flags: -h          Display this message\n"
		"       -n          Create a sterile key (requires one of -w, -m or -s)\n"
		"       -w          Add the world to the membership list\n"
		"       -m id       Include another id in the membership list (multiple use ok)\n"
		"       -s id       Create a redirector to another id (implies -n)\n"
		"       -t nm=val   Set text fragment 'nm' to 'val'\n"
		"       -r file     Write pRivate key to this file\n"
		"       -u file     Write pUblic key to this file\n"
		"       /\"name\"     Set the key comment text\n" 
		,GALE_BANNER);
	exit(1);
}

static void *on_ignore(oop_source *oop,struct gale_key *key,void *user) {
	return OOP_CONTINUE;
}

static void *on_generate(oop_source *oop,struct gale_key *key,void *user) {
	const struct gale_time now = gale_time_now();
	const struct gale_key_assertion * const pub = gale_key_public(key,now);
	const struct gale_key_assertion * const priv = gale_key_private(key);
	const int is_signed = (NULL != gale_key_signed(pub));

	if (NULL == pub)
		gale_alert(GALE_ERROR,G_("key generation failed!"),0);

	if (0 != priv_file.l) {
		if (gale_write_file(priv_file,gale_key_raw(priv),1,NULL))
			gale_alert(GALE_NOTICE,gale_text_concat(3,
				G_("saved private key to \""),priv_file,
				G_("\", as directed")),0);
		else
			gale_alert(GALE_WARNING,priv_file,errno);
	}

	if (0 != pub_file.l) {
		if (gale_write_file(pub_file,gale_key_raw(pub),0,NULL))
			gale_alert(GALE_NOTICE,gale_text_concat(5,
				G_("saved"),
				(is_signed ? G_(" ") : G_(" *unsigned* ")),
				G_("public key to \""),pub_file,
				G_("\", as directed")),0);
		else
			gale_alert(GALE_WARNING,pub_file,errno);
	}

	if (is_signed)
		gale_key_search(
			oop,key,search_all & ~search_slow,
			on_ignore,NULL);

	return OOP_CONTINUE;
}

int main(int argc,char *argv[]) {
	struct gale_group data = gale_group_empty();
	struct gale_fragment frag;
	struct gale_key *key = NULL;
	int arg;

	gale_init("gkgen",argc,argv);
	priv_file = pub_file = null_text;

	if (argc <= 1) usage();
	while ((arg = getopt(argc,argv,"hnwm:s:t:r:u:")) != EOF) {
	const struct gale_text str = !optarg ? null_text :
		gale_text_from(gale_global->enc_cmdline,optarg,-1);
	switch (arg) {
	case 'n': do_generate = 0; break;

	case 'w':
	case 'm': 
		frag.type = frag_text;
		frag.name = G_("key.member");
		frag.value.text = str;
		gale_group_add(&data,frag);
		break;

	case 't':
		frag.type = frag_text;
		frag.name = null_text;
		gale_text_token(str,'=',&frag.name);
		frag.value.text = frag.name;
		if (!gale_text_token(str,'=',&frag.value.text))
			frag.value.text = null_text;
		gale_group_add(&data,frag);
		break;

	case 's':
		do_generate = 0;
		frag.type = frag_text;
		frag.name = G_("key.redirect");
		frag.value.text = str;
		gale_group_add(&data,frag);
		break;

	case 'r': priv_file = str; break;
	case 'u': pub_file = str; break;
	case 'h':
	case '?': usage();
	} }

	if (!do_generate && gale_group_null(data))
		gale_alert(GALE_ERROR,G_("sterile key without a purpose!"),0);

	while (argc != optind) {
		const struct gale_text str = gale_text_from(
			gale_global->enc_cmdline,argv[optind++],-1);
		if (str.l > 0 && '/' == str.p[0]) {
			frag.type = frag_text;
			frag.name = G_("key.owner");
			frag.value.text = gale_text_right(str,-1);
			gale_group_add(&data,frag);
		} else {
			if (NULL != key) gale_alert(GALE_ERROR,
				G_("multiple keys specified!"),0);
			key = gale_key_handle(str);
		}
	}

	if (NULL == key) 
		gale_alert(GALE_ERROR,G_("no key id specified!"),0);

	if (NULL == gale_key_parent(key))
		gale_alert(GALE_WARNING,G_("making ROOT key!!!"),0);
	else if (NULL == gale_key_parent(gale_key_parent(key)))
		gale_alert(GALE_WARNING,G_("makeing top-level domain key!"),0);

	if (do_generate) {
		struct gale_group k = gale_crypto_generate(gale_key_name(key));
		gale_group_append(&data,k);
	}

	{
		oop_source_sys * const sys = oop_sys_new();
		gale_key_generate(oop_sys_source(sys),
			key,data,on_generate,NULL);
		oop_sys_run(sys);
		oop_sys_delete(sys);
	}

	return 0;
}
