#include "gale/all.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

int do_name_only = 0;
int do_verbose = 0;
int do_trust_input = 0;
int is_found = 0;

void print(const struct gale_key_assertion *ass) {
	struct gale_key *owner = gale_key_owner(ass);
	struct gale_group data = gale_key_data(ass);
	if (NULL == owner) return;

	if (do_name_only) {
		if (!is_found) {
			gale_print(stdout,0,gale_key_name(owner));
			gale_print(stdout,0,G_("\n"));
			is_found = 1;
		}
		return;
	}

	is_found = 1;
	if (gale_group_compare(data,gale_crypto_public(data)))
		gale_print(stdout,0,G_("Private key:"));
	else
		gale_print(stdout,0,G_("Public key:"));

	while (NULL != owner) {
		struct gale_fragment frag;
		struct gale_group data = gale_key_data(ass);

		gale_print(stdout,0,G_(" <"));
		gale_print(stdout,0,gale_key_name(owner));
		gale_print(stdout,0,G_(">"));

		if (gale_group_lookup(data,G_("key.owner"),frag_text,&frag)
		&&  frag.value.text.l > 0) {
			gale_print(stdout,0,G_(" ("));
			gale_print(stdout,0,frag.value.text);
			gale_print(stdout,0,G_(")"));
		}

		if (gale_key_trusted(ass))
			gale_print(stdout,0,G_(" [trusted]"));

		gale_print(stdout,0,G_("\n"));

		if (do_verbose) {
			gale_print(stdout,0,G_("  "));
			gale_print(stdout,0,
				gale_print_group(data,2));
			gale_print(stdout,0,G_("\n\n"));
		}

		ass = gale_key_signed(ass);
		owner = gale_key_owner(ass);
		if (NULL != owner)
			gale_print(stdout,0,G_("Signed by:"));
	}
}

void *on_key(oop_source *oop,struct gale_key *key,void *user) {
	print(gale_key_public(key,gale_time_now()));
	print(gale_key_private(key));
	return OOP_CONTINUE;
}

void *on_parent(oop_source *oop,struct gale_key *key,void *user) {
	const struct gale_data * const data = (const struct gale_data *) user;
	if (NULL == key) return OOP_CONTINUE;
	print(gale_key_assert(*data,0));
	return OOP_CONTINUE;
}

void usage(void) {
        fprintf(stderr,
                "%s\n"
                "usage: gkinfo [-hivx] (id ... | [-t] < keyfile)\n"
		"flags: -h          Display this message\n"
		"       -i          Output key ID only\n"
		"       -t          Trust keyfile contents\n"
		"       -v          Verbose output\n"
		"       -x          Disable remote key retrieval\n"
                ,GALE_BANNER);
	exit(1);
}

int main(int argc,char *argv[]) {
	int arg;
	int flags = search_all;

	gale_init("gkinfo",argc,argv);

	while ((arg = getopt(argc,argv,"hitvxdD")) != EOF) switch (arg) {
	case 'i': do_name_only = 1; break;
	case 't': do_trust_input = 1; break;
	case 'v': do_verbose = 1; break;
	case 'x': flags &= ~search_slow; break;
	case 'd': ++gale_global->debug_level; break;
	case 'D': gale_global->debug_level += 5; break;
	case 'h':
	case '?': usage();
	}

	if (do_trust_input && argc > optind) usage();

	if (argc > optind) {
		oop_source_sys * const sys = oop_sys_new(); 
		while (argc != optind) {
			struct gale_key *handle = gale_key_handle(
				gale_text_from(gale_global->enc_cmdline,
					argv[optind++],-1));
			gale_key_search(oop_sys_source(sys),
				handle,flags,
				on_key,NULL);
		}
		oop_sys_run(sys);
		oop_sys_delete(sys);
	} else {
		struct gale_key_assertion *ass;
		struct gale_data key;

		if (isatty(0)) usage();
		key = gale_read_from(0,0);
		if (0 == key.l)
			gale_alert(GALE_ERROR,G_("could not read stdin"),0);

		ass = gale_key_assert(key,1);
		if (do_trust_input) 
			print(ass);
		else {
			oop_source_sys * const sys = oop_sys_new();
			struct gale_key * parent = gale_key_owner(ass);
			if (NULL == parent)
				gale_alert(GALE_ERROR,
				           G_("could not decode key"),0);

			parent = gale_key_parent(parent);
			if (NULL == parent)
				gale_alert(GALE_ERROR,G_("key is ROOT"),0);

			gale_key_retract(ass);
			gale_key_search(oop_sys_source(sys),
				parent,flags,
				on_parent,&key);
			oop_sys_run(sys);
			oop_sys_delete(sys);
		}
	}

	if (!is_found)
		gale_alert(GALE_ERROR,G_("could not find key"),0);

	return 0;
}
