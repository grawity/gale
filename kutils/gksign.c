#include "gale/all.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>

static void usage(void) {
        fprintf(stderr,
                "%s\n"
                "usage: gksign [-h] [id] < public-key > signed-public-key\n"
		"flags: -h          Display this message\n"
                ,GALE_BANNER);
	exit(1);
}

static void *on_key(oop_source *oop,struct gale_key *key,void *user) {
	const struct gale_key_assertion * const priv = gale_key_private(key);
	struct gale_group data = * (struct gale_group *) user;
	struct gale_group signer = gale_key_data(priv);
	if (NULL == priv) gale_alert(GALE_ERROR,gale_text_concat(3,
		G_("need private key \""),gale_key_name(key),G_("\"")),0);

	{
		const struct gale_time now = gale_time_now();
		struct gale_fragment frag;

		frag.type = frag_data;
		frag.name = G_("key.source");
		frag.value.data = gale_key_raw(gale_key_public(key,now));
		gale_group_replace(&signer,frag);

		frag.type = frag_time;
		frag.name = G_("key.signed");
		frag.value.time = now;
		gale_group_replace(&data,frag);

		if (!gale_crypto_sign(1,&signer,&data))  
			gale_alert(GALE_ERROR,G_("could not sign key"),0);
	}

	{
		const struct gale_key_assertion * const ass = 
			gale_key_assert_group(data,
                                G_("signed locally"),gale_time_now(),0);
		const struct gale_data bits = gale_key_raw(ass);
		if (0 == bits.l) 
			gale_alert(GALE_ERROR,G_("cannot generate key"),0); 
		if (!gale_write_to(1,bits))
			gale_alert(GALE_ERROR,G_("cannot write key"),errno);
	}

	return OOP_CONTINUE;
}

int main(int argc,char *argv[]) {
	struct gale_text check = null_text;
	struct gale_key_assertion *ass;
	struct gale_key *key;

	gale_init("gksign",argc,argv);

	while (getopt(argc,argv,"h") != EOF) usage();

	if (optind != argc) check = gale_text_from(
		gale_global->enc_cmdline,
		argv[optind++],-1);

	if (optind != argc)
		usage();
	if (isatty(0))
		gale_alert(GALE_ERROR,G_("won't read the key from terminal"),0);
	if (isatty(1))
		gale_alert(GALE_ERROR,G_("won't write the key to terminal"),0);

	if (getuid() != geteuid()) {
		struct passwd * const pwd = getpwuid(getuid());
		const struct gale_text domain = gale_var(G_("GALE_DOMAIN"));
		struct gale_text verify;
		if (!pwd) gale_alert(GALE_ERROR,G_("who are you?"),0);

		verify = gale_text_concat(3,
			gale_text_from(gale_global->enc_sys,pwd->pw_name,-1),
			G_("@"),domain);
		if (0 != check.l) gale_alert(GALE_WARNING,gale_text_concat(5,
			G_("ignoring \""),check,
			G_("\", using \""),verify,G_("\" instead")),0);
		check = verify;
	}

	{
		struct gale_data key_bits = gale_read_from(0,0);
		if (0 == key_bits.l)
			gale_alert(GALE_ERROR,G_("could not read input"),errno);
		ass = gale_key_assert(key_bits,G_("read from gksign"),gale_time_forever(),1);
	}

	key = gale_key_owner(ass);
	if (ass != gale_key_public(key,gale_time_now()))
		gale_alert(GALE_ERROR,G_("could not decode key"),0);

	if (check.l && gale_text_compare(check,gale_key_name(key)))
		gale_alert(GALE_ERROR,G_("permission denied to sign key"),0);

	if (NULL != gale_key_signed(ass))
		gale_alert(GALE_WARNING,G_("key is already signed"),0);

	key = gale_key_parent(key);
	if (NULL == key) gale_alert(GALE_ERROR,G_("key is ROOT"),0);

	{
		struct gale_group key_data = 
			gale_crypto_original(gale_key_data(ass));
		oop_source_sys * const sys = oop_sys_new();
		gale_key_retract(ass,1);
		gale_key_search(oop_sys_source(sys),
			key,search_private,
			on_key,&key_data);
		oop_sys_run(sys);
		oop_sys_delete(sys);
	}

	return 0;
}
