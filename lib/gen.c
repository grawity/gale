#include "common.h"
#include "random.h"
#include "file.h"
#include "key.h"
#include "id.h"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>

static void sign_key(struct gale_data in,struct gale_data *out) {
	int infd = -1,outfd = -1;
	char * args[] = { "gksign", NULL };
	pid_t pid = gale_exec("gksign",args,&infd,&outfd,NULL);

	out->p = NULL;
	out->l = 0;

	if (pid < 0) return;

	if (_ga_save(infd,in)) {
		close(infd); infd = -1;
		_ga_load(outfd,out);
	}

	if (infd != -1) close(infd);
	if (outfd != -1) close(outfd);

	gale_wait(pid);
}

static void stash(char * const * argv) {
	struct gale_data data;
	struct gale_text fn = gale_text_from_local(argv[1],-1);
	struct inode inode;
	if (_ga_load(0,&data) 
	&& _ga_save_file(gale_global->dot_private,fn,0600,data,&inode)) {
		gale_alert(GALE_NOTICE,
		gale_text_to_local(gale_text_concat(5, 
			G_("saving private key in \""),
			_ga_dot_private,G_("/"),inode.name,
			G_("\""))),0);
	}
}

void auth_id_gen(struct auth_id *id,struct gale_text comment) {
	int err;
	R_RSA_PROTO_KEY proto;
	struct gale_data key;
	const char *bits;

	id->comment = comment;
	gale_create(id->public);
	gale_create(id->private);

	proto.bits = 0;
	bits = getenv("GALE_AUTH_BITS");
	if (bits) proto.bits = atoi(bits);
	if (proto.bits == 0) proto.bits = 768;

	if (proto.bits > MAX_RSA_MODULUS_BITS) {
		gale_alert(GALE_WARNING,"key size too big; truncating",0);
		proto.bits = MAX_RSA_MODULUS_BITS;
	} else if (proto.bits < MIN_RSA_MODULUS_BITS) {
		gale_alert(GALE_WARNING,"key size too small; expanding",0);
		proto.bits = MIN_RSA_MODULUS_BITS;
	}

	proto.useFermat4 = 1;
	gale_alert(GALE_NOTICE,"generating keys, please wait...",0);
	err = R_GeneratePEMKeys(id->public,id->private,&proto,_ga_rrand());
	assert(!err);

	_ga_export_priv(id,&key);
	if (!key.p) 
		_ga_warn_id(G_("could not export private key \"%\""),id);
	else {
		int fd;
		pid_t pid;
		char *argv[] = { "gkstash", NULL, NULL };
		argv[1] = gale_text_to_local(id->name);
		pid = gale_exec("gkstash",argv,&fd,NULL,stash);
		if (fd >= 0) {
			_ga_save(fd,key);
			close(fd);
		}
		gale_wait(pid);
		gale_free(key.p);
		gale_free(argv[1]);
	}

	_ga_sign_pub(id,gale_time_forever());
	_ga_export_pub(id,&key,EXPORT_TRUSTED);
	if (!key.p) {
		_ga_warn_id(G_("could not export public key \"%\""),id);
		return;
	}

	if (!id->sig.id) {
		struct gale_data sgn;
		sign_key(key,&sgn);
		if (sgn.p) {
			struct auth_id *nid;
			_ga_import_pub(&nid,sgn,NULL,IMPORT_NORMAL);
			if (nid == id) {
				struct gale_data tmp = key;
				key = sgn;
				sgn = tmp;
			}
		}
		if (sgn.p) gale_free(sgn.p);
	}

	if (_ga_trust_pub(id)) {
		struct inode inode;
		_ga_save_file(gale_global->sys_local,id->name,0644,key,NULL);
		if (_ga_save_file(gale_global->dot_local,id->name,0644,key,&inode)) {
			gale_alert(GALE_NOTICE,
			gale_text_to_local(gale_text_concat(5, 
				G_("saving signed public key in \""),
				gale_global->dot_local,G_("/"),inode.name,
				G_("\""))),0);
		}
		gale_free(key.p);
	}

	gale_alert(GALE_NOTICE,"done generating keys",0);
}
