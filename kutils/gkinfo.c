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
int do_location = 1;
int do_members = 0;

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

void *on_location(struct gale_text name,struct gale_location *loc,void *user) {
	oop_source *oop = (oop_source *) user;
	if (NULL == loc) return OOP_CONTINUE;

	if (do_members) {
		const struct gale_map * const members =
			gale_location_members(loc);
		gale_print(stdout,0,G_("Members of <"));
		gale_print(stdout,0,name);
		if (NULL == members)
			gale_print(stdout,0,G_(">: *everyone*.\n"));
		else {
			struct gale_data key = null_data;
			gale_print(stdout,0,G_(">:\n"));
			while (gale_map_walk(members,&key,&key,NULL)) {
				gale_print(stdout,0,gale_text_from_data(key));
				gale_print(stdout,0,G_("\n"));
			}
		}

		is_found = 1;
		return OOP_CONTINUE;
	}

	return on_key(oop,gale_location_key(loc),NULL);
}

void *on_parent(oop_source *oop,struct gale_key *key,void *user) {
	const struct gale_data * const data = (const struct gale_data *) user;
	if (NULL == key) return OOP_CONTINUE;
	print(gale_key_assert(*data,gale_time_forever(),0));
	return OOP_CONTINUE;
}

void usage(void) {
        fprintf(stderr,
                "%s\n"
                "usage: gkinfo [-hikmvxy] (address [address ...] | [-t] < keyfile)\n"
		"flags: -h          Display this message\n"
		"       -i          Output key ID only\n"
		"       -k          Input exact key ID only (not location)\n"
		"       -m          List all group members\n"
		"       -t          Trust keyfile contents\n"
		"       -v          Verbose output\n"
		"       -x          Disable remote key retrieval; implies -k\n"
		"       -y          Force remote key retrieval; implies -k\n"
                ,GALE_BANNER);
	exit(1);
}

int main(int argc,char *argv[]) {
	int arg;
	int flags = search_all;

	gale_init("gkinfo",argc,argv);

	while ((arg = getopt(argc,argv,"hikmtvxydD")) != EOF) switch (arg) {
	case 'i': do_name_only = 1; break;
	case 'k': do_location = 0; break;
	case 'm': do_members = 1; break;
	case 't': do_trust_input = 1; break;
	case 'v': do_verbose = 1; break;
	case 'x': flags &= ~search_slow; do_location = 0; break;
	case 'y': flags |= search_harder; do_location = 0; break;
	case 'd': ++gale_global->debug_level; break;
	case 'D': gale_global->debug_level += 5; break;
	case 'h':
	case '?': usage();
	}

	if (do_trust_input && argc > optind)
		gale_alert(GALE_ERROR,
			G_("trusted input only meaningful for key file"),0);

	if (do_members && (!do_location || argc == optind))
		gale_alert(GALE_ERROR,
			G_("member list only for locations, not keys"),0);

	if (argc > optind) {
		oop_source_sys * const sys = oop_sys_new(); 
		oop_source * const oop = oop_sys_source(sys);
		while (argc != optind) {
			struct gale_text name = gale_text_from(
				gale_global->enc_cmdline,
				argv[optind++],-1);
			if (do_location)
				gale_find_location(oop,name,on_location,oop);
			else
				gale_key_search(oop,
					gale_key_handle(name),flags,
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

		ass = gale_key_assert(key,gale_time_forever(),1);
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

			gale_key_retract(ass,1);
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
