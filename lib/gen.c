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
			gale_global->dot_private,G_("/"),inode.name,
			G_("\""))),0);
	}
}

static void clear(struct auth_id *id,struct gale_text comment) {
	struct gale_fragment frag;

	id->pub_time_slow = gale_time_zero();
	id->pub_time_fast = gale_time_zero();
	id->pub_data = gale_group_empty();
	id->pub_orig = null_data;
	id->pub_signer = NULL;
	id->pub_inode = _ga_init_inode();
	id->pub_trusted = 0;

	id->priv_time_slow = gale_time_zero();
	id->priv_time_fast = gale_time_zero();
	id->priv_data = gale_group_empty();
	id->priv_inode = _ga_init_inode();

	frag.type = frag_text;
	frag.name = G_("key.owner");
	frag.value.text = comment;
	gale_group_add(&id->pub_data,frag);
	gale_group_add(&id->priv_data,frag);
}

static void write_priv(struct auth_id *id) {
	struct gale_data key;
	_ga_export_priv(id,&key);
	if (0 == key.l) 
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
}

static void write_pub(struct auth_id *id) {
	struct gale_data key;
	_ga_sign_pub(id,gale_time_forever());
	_ga_export_pub(id,&key,EXPORT_TRUSTED);
	if (0 == key.l) {
		_ga_warn_id(G_("could not export public key \"%\""),id);
		return;
	}

	if (NULL == id->pub_signer) {
		struct gale_data sgn;
		sign_key(key,&sgn);
		if (0 != sgn.l) {
			struct auth_id *nid;
			_ga_import_pub(&nid,sgn,NULL,IMPORT_NORMAL);
			if (nid == id) {
				struct gale_data tmp = key;
				key = sgn;
				sgn = tmp;
			}
		}
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
}

void auth_id_redirect(
	struct auth_id *id,struct gale_text comment,
	struct auth_id *dest)
{
	struct gale_fragment frag;

	clear(id,comment);
	id->priv_data = gale_group_empty();

	frag.type = frag_text;
	frag.name = G_("key.redirect");
	frag.value.text = auth_id_name(dest);
	gale_group_add(&id->pub_data,frag);

	write_pub(id);
}

void auth_id_gen(struct auth_id *id,struct gale_text comment) {
	int err;
	R_RSA_PROTO_KEY proto;
	R_RSA_PUBLIC_KEY *rsapub;
	R_RSA_PRIVATE_KEY *rsapriv;
	struct gale_fragment frag;
	const char *bits;

	clear(id,comment);

	proto.bits = 0;
	bits = getenv("GALE_AUTH_BITS");
	if (NULL != bits) proto.bits = atoi(bits);
	if (proto.bits == 0) proto.bits = 768; /* default */

	if (proto.bits > MAX_RSA_MODULUS_BITS) {
		gale_alert(GALE_WARNING,"key size too big; truncating",0);
		proto.bits = MAX_RSA_MODULUS_BITS;
	} else if (proto.bits < MIN_RSA_MODULUS_BITS) {
		gale_alert(GALE_WARNING,"key size too small; expanding",0);
		proto.bits = MIN_RSA_MODULUS_BITS;
	}

	gale_create(rsapub);
	gale_create(rsapriv);
	proto.useFermat4 = 1;
	gale_alert(GALE_NOTICE,"generating keys, please wait...",0);
	err = R_GeneratePEMKeys(rsapub,rsapriv,&proto,_ga_rrand());
	assert(!err);

	/* Fill in fields from structure. */

	frag.type = frag_number;
	frag.name = G_("rsa.bits");
	frag.value.number = rsapub->bits;
	gale_group_add(&id->pub_data,frag);
	frag.value.number = rsapriv->bits;
	gale_group_add(&id->priv_data,frag);

	frag.type = frag_data;
	frag.name = G_("rsa.modulus");
	frag.value.data.l = MAX_RSA_MODULUS_LEN;
	frag.value.data.p = rsapub->modulus;
	gale_group_add(&id->pub_data,frag);
	frag.value.data.p = rsapriv->modulus;
	gale_group_add(&id->priv_data,frag);

	frag.name = G_("rsa.exponent");
	frag.value.data.p = rsapub->exponent;
	gale_group_add(&id->pub_data,frag);
	frag.value.data.p = rsapriv->publicExponent;
	gale_group_add(&id->priv_data,frag);

	frag.name = G_("rsa.private.exponent");
	frag.value.data.p = rsapriv->exponent;
	gale_group_add(&id->priv_data,frag);

	frag.name = G_("rsa.private.prime");
	frag.value.data.l = 2 * MAX_RSA_PRIME_LEN;
	frag.value.data.p = rsapriv->prime[0];
	gale_group_add(&id->priv_data,frag);

	frag.name = G_("rsa.private.prime.exponent");
	frag.value.data.p = rsapriv->primeExponent[0];
	gale_group_add(&id->priv_data,frag);

	frag.name = G_("rsa.private.coefficient");
	frag.value.data.l = MAX_RSA_PRIME_LEN;
	frag.value.data.p = rsapriv->coefficient;
	gale_group_add(&id->priv_data,frag);

	write_priv(id);
	write_pub(id);
	gale_alert(GALE_NOTICE,"done generating keys",0);
}
