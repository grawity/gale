#include "crypto_i.h"

#include <openssl/rand.h>
#include <openssl/err.h>

#include <sys/vfs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

void crypto_i_seed(void) {
	static int is_init = 0;
	struct {
		int fd;
		struct stat st;
		struct timeval tv[2];
		pid_t pid,pgrp;
		unsigned char stuff[16];
	} r;

	if (is_init) return;
	gettimeofday(&r.tv[0],NULL);

	r.pid = getpid();
	r.pgrp = getpgrp();
	stat("/",&r.st);
	r.fd = open("/dev/random",O_RDONLY);
	if (-1 != r.fd) {
		read(r.fd,r.stuff,sizeof(r.stuff));
		close(r.fd);
	}

	gettimeofday(&r.tv[1],NULL);
	RAND_seed(&r,sizeof(r));
	is_init = 1;
}

void crypto_i_error(void) {
	unsigned long err;
	ERR_load_crypto_strings();

	while (0 != (err = ERR_get_error()))
		gale_alert(GALE_WARNING,gale_text_concat(3,
			gale_text_from(NULL,ERR_lib_error_string(err),-1),
			G_(": "),
			gale_text_from(NULL,ERR_reason_error_string(err),-1)),
			0);
}

struct gale_text crypto_i_rsa(struct gale_group key,RSA *rsa) {
	struct gale_text name = null_text;

	while (!gale_group_null(key)) {
		struct gale_fragment frag = gale_group_first(key);
		struct gale_data data = frag.value.data;
		key = gale_group_rest(key);

		if (frag_text == frag.type
		&& !gale_text_compare(G_("key.id"),frag.name))
			name = frag.value.text;

		if (frag_data != frag.type) 
			continue;
		else if (!gale_text_compare(G_("rsa.modulus"),frag.name))
			rsa->n = BN_bin2bn(data.p,data.l,rsa->n);
		else if (!gale_text_compare(G_("rsa.exponent"),frag.name))
			rsa->e = BN_bin2bn(data.p,data.l,rsa->e);
		else if (!gale_text_compare(G_("rsa.private.exponent"),frag.name))
			rsa->d = BN_bin2bn(data.p,data.l,rsa->d);
		else if (2*GALE_RSA_PRIME_LEN == data.l
		     && !gale_text_compare(G_("rsa.private.prime"),frag.name)) {
			rsa->p = BN_bin2bn(data.p,GALE_RSA_PRIME_LEN,rsa->p);
			rsa->q = BN_bin2bn(
				GALE_RSA_PRIME_LEN + data.p,
				GALE_RSA_PRIME_LEN,
				rsa->q);
		}
		else if (2*GALE_RSA_PRIME_LEN == data.l
		     && !gale_text_compare(frag.name,
			G_("rsa.private.prime.exponent")))
		{
			rsa->dmp1 = BN_bin2bn(data.p,
				GALE_RSA_PRIME_LEN,
				rsa->dmp1);
			rsa->dmq1 = BN_bin2bn(
				GALE_RSA_PRIME_LEN + data.p,
				GALE_RSA_PRIME_LEN,
				rsa->dmq1);
		}
		else if (!gale_text_compare(frag.name,
			G_("rsa.private.coefficient")))
			rsa->iqmp = BN_bin2bn(data.p,data.l,rsa->iqmp);
	}

	return name;
}

int crypto_i_public_valid(RSA *rsa) {
	return NULL != rsa->n && NULL != rsa->e;
}

int crypto_i_private_valid(RSA *rsa) {
	return crypto_i_public_valid(rsa)
	    && NULL != rsa->d
	    && NULL != rsa->p && NULL != rsa->q
	    && NULL != rsa->dmp1 && NULL != rsa->dmq1
	    && NULL != rsa->iqmp;
}
