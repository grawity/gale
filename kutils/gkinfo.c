#include "common.h"
#include "file.h"
#include "key.h"
#include "id.h"

#include "gale/all.h"

/* For MD5: */
#include "global.h"
#include "rsaref.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

int do_name_only = 0;
int do_verbose = 0;
int is_found = 0;

const byte m_gale[] = { 0x68, 0x13 };
const byte m_pub[] = { 0x00, 0x00 };
const byte m_pub2[] = { 0x00, 0x02 };
const byte m_priv[] = { 0x00, 0x01 };
const byte m_priv2[] = { 0x00, 0x03 };
const byte m_sign[] = { 0x01, 0x00 };

const byte m_gale3[] = { 'G', 'A', 'L', 'E' };
const byte m_pub3[] = { 0x00, 0x01 };

const char *do_indent(int indent) {
	char *str;
	gale_create_array(str,1 + indent);
	str[indent] = '\0';
	while (indent--) str[indent] = ' ';
	return str;
}

void do_info(struct gale_group grp,const struct inode *i,int indent) {
	if (0 != i->name.l)
		printf("%sStored in \"%s\"\n",do_indent(indent),
		       gale_text_to_local(i->name));

	printf("%s",gale_text_to_local(gale_print_group(grp,2)));
}

void pub_info(struct auth_id *id,int indent) {
	struct gale_fragment frag;
	printf("<%s>",gale_text_to_local(id->name));
	if (gale_group_lookup(id->pub_data,G_("key.owner"),frag_text,&frag))
		printf(" (%s)",gale_text_to_local(frag.value.text));
	if (id->pub_trusted)
		printf(" [trusted]");
	printf("\n");

	if (do_verbose) do_info(id->pub_data,&id->pub_inode,indent += 2);

	if (gale_group_lookup(id->pub_data,G_("key.redirect"),frag_text,&frag))
		printf("%sRedirector to <%s>\n",
		       do_indent(indent),gale_text_to_local(frag.value.text));

	if (do_verbose) {
		putchar('\n');
		return;
	}

	if (NULL == id->pub_signer) return;
	printf("%sSigned: ",do_indent(indent += 2));
	pub_info(id->pub_signer,indent);
}

void pub_key(struct auth_id *id) {
	is_found = 1;
	if (do_name_only) return;

	if (_ga_trust_pub(id))
		printf("Trusted");
	else
		printf("UNTRUSTED");
	printf(" public key: "); pub_info(id,0);
}

void priv_key(struct auth_id *id) {
	is_found = 1;
	if (do_name_only) return;

	printf("Private key: <%s>\n",gale_text_to_local(id->name));
	if (do_verbose) {
		do_info(id->priv_data,&id->priv_inode,2);
		putchar('\n');
	}
}

void usage(void) {
        fprintf(stderr,
                "%s\n"
                "usage: gkinfo [-hvix] (id | < keyfile)\n"
		"flags: -h          Display this message\n"
		"       -i          Output key ID only\n"
		"       -v          Verbose output\n"
		"       -x          Disable remote key retrieval\n"
                ,GALE_BANNER);
	exit(1);
}

int main(int argc,char *argv[]) {
	struct gale_data key = null_data;
	struct auth_id *id;
	int arg;

	gale_init("gkinfo",argc,argv);

	while ((arg = getopt(argc,argv,"ixvdD")) != EOF) switch (arg) {
	case 'i': do_name_only = 1; break;
	case 'x': disable_gale_akd(); break;
	case 'v': do_verbose = 1; break;
	case 'd': ++gale_global->debug_level; break;
	case 'D': gale_global->debug_level += 5; break;
	case 'h':
	case '?': usage();
	}

	if (optind + 1 == argc) {
		init_auth_id(&id,gale_text_from_local(argv[optind],-1));
		if (auth_id_private(id)) priv_key(id);
		if (auth_id_public(id)) pub_key(id);
	} else {
		struct gale_data test;

		if (optind != argc) usage();
		if (isatty(0)) usage();
		if (!_ga_load(0,&key)) 
			gale_alert(GALE_ERROR,"could not read file",errno);

		test = key;
		if (gale_unpack_compare(&test,m_gale,sizeof(m_gale))) {
			if (gale_unpack_compare(&test,m_pub,sizeof(m_pub))
			||  gale_unpack_compare(&test,m_pub2,sizeof(m_pub2))) {
				struct inode inode = _ga_init_inode();
				_ga_import_pub(&id,key,&inode,IMPORT_NORMAL);
				if (NULL == id) 
					gale_alert(GALE_ERROR,
					           "public key invalid",0);
				pub_key(id);
			} else 
			if (gale_unpack_compare(&test,m_priv,sizeof(m_priv))
			||  gale_unpack_compare(&test,m_priv2,sizeof(m_priv2))){
				_ga_import_priv(&id,key,NULL);
				if (NULL == id) 
					gale_alert(GALE_ERROR,
					           "private key invalid",0);
				priv_key(id);
			} else
				gale_alert(GALE_ERROR,"invalid key",0);
		} else if (gale_unpack_compare(&test,m_gale3,sizeof(m_gale3))) {
			if (gale_unpack_compare(&test,m_pub3,sizeof(m_pub3))) {
				struct inode inode = _ga_init_inode();
				_ga_import_pub(&id,key,&inode,IMPORT_NORMAL);
				if (NULL == id) 
					gale_alert(GALE_ERROR,
					           "new public key invalid",0);
				pub_key(id);
			} else
				gale_alert(GALE_ERROR,"invalid key",0);
		} else
			gale_alert(GALE_ERROR,"unknown file format",0);
	}

	if (!is_found)
		gale_alert(GALE_ERROR,"could not find key",0);
	else if (do_name_only)
		printf("%s\n",gale_text_to_local(auth_id_name(id)));

	return 0;
}
