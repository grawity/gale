#include "common.h"
#include "file.h"
#include "key.h"
#include "id.h"

#include "gale/all.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

int iflag = 0;

const byte m_gale[] = { 0x68, 0x13 };
const byte m_pub[] = { 0x00, 0x00 };
const byte m_pub2[] = { 0x00, 0x02 };
const byte m_priv[] = { 0x00, 0x01 };
const byte m_priv2[] = { 0x00, 0x03 };
const byte m_sign[] = { 0x01, 0x00 };

void pub_info(struct auth_id *id) {
	printf("<%s> (%s), %d bits%s\n",
	       gale_text_to_local(id->name),
	       gale_text_to_local(id->comment),
	       id->public->bits,
	       id->trusted ? " [trusted]" : "");
}

void pub_date(struct gale_time time) {
	struct timeval tv;
	time_t sec;
	char buf[30];
	gale_time_to(&tv,time);
	sec = tv.tv_sec;
	strftime(buf,sizeof(buf),"%Y-%m-%d %H:%M",localtime(&sec));
	printf("%s",buf);
}

void pub_key(struct auth_id *id) {
	int indent = 0;

	if (iflag) {
		printf("%s\n",gale_text_to_local(id->name));
		return;
	}

	if (_ga_trust_pub(id))
		printf("Trusted");
	else
		printf("UNTRUSTED");
	printf(" public key: "); pub_info(id);
	while (id->sig.id) {
		int i;
		indent += 2;
		for (i = 0; i < indent; ++i) printf(" ");
		printf("Signed");
		if (gale_time_compare(gale_time_zero(),id->sign_time) < 0) {
			printf(" (");
			pub_date(id->sign_time);
			if (gale_time_compare(id->expire_time,gale_time_forever()) < 0) {
				printf(" - ");
				pub_date(id->expire_time);
			}
			printf(")");
		}
		id = id->sig.id;
		printf(": "); pub_info(id);
	}
}

void priv_key(struct auth_id *id) {
	if (iflag) {
		printf("%s\n",gale_text_to_local(id->name));
		return;
	}
	printf("Private key: <%s>, %d bits\n",
	       gale_text_to_local(id->name),
	       id->private->bits);
}

void usage(void) {
        fprintf(stderr,
                "%s\n"
                "usage: gkinfo [-hix] (id | < keyfile)\n"
		"flags: -h          Display this message\n"
		"       -i          Output key ID only\n"
		"       -x          Disable remote key retrieval\n"
                ,GALE_BANNER);
	exit(1);
}

int main(int argc,char *argv[]) {
	struct gale_data key = null_data;
	struct auth_id *id;
	int arg;

	gale_init("gkinfo",argc,argv);

	while ((arg = getopt(argc,argv,"ixdD")) != EOF) switch (arg) {
	case 'i': iflag = 1; break;
	case 'x': disable_gale_akd(); break;
	case 'd': ++gale_global->debug_level; break;
	case 'D': gale_global->debug_level += 5; break;
	case 'h':
	case '?': usage();
	}

	if (optind + 1 == argc) {
		int found = 0;
		init_auth_id(&id,gale_text_from_local(argv[optind],-1));
		if (iflag) {
			found = auth_id_private(id) || auth_id_public(id);
			printf("%s\n",gale_text_to_local(auth_id_name(id)));
		} else {
			if (auth_id_private(id)) {
				found = 1;
				priv_key(id);
			}
			if (auth_id_public(id)) {
				found = 1;
				pub_key(id);
			}
		}
		if (!found) gale_alert(GALE_ERROR,"could not find key",0);
	} else {
		if (optind != argc) usage();
		if (isatty(0)) usage();
		if (!_ga_load(0,&key)) 
			gale_alert(GALE_ERROR,"could not read file",errno);

		if (key.l < 4 || memcmp(key.p,m_gale,sizeof(m_gale)))
			gale_alert(GALE_ERROR,"unrecognized file format",0);

		if (!memcmp(key.p + sizeof(m_gale),m_pub,sizeof(m_pub))
		||  !memcmp(key.p + sizeof(m_gale),m_pub2,sizeof(m_pub2))) {
			struct inode inode = _ga_init_inode();
			_ga_import_pub(&id,key,&inode,IMPORT_NORMAL);
			if (!id) gale_alert(GALE_ERROR,"public key invalid",0);
			pub_key(id);
		} else 
		if (!memcmp(key.p + sizeof(m_gale),m_priv,sizeof(m_pub))
		||  !memcmp(key.p + sizeof(m_gale),m_priv2,sizeof(m_priv2))) {
			_ga_import_priv(&id,key,NULL);
			if (!id) gale_alert(GALE_ERROR,"private key invalid",0);
			priv_key(id);
		} else
			gale_alert(GALE_ERROR,"unknown file data",0);
	}

	return 0;
}
