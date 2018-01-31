#include "crypto_i.h"

#include <openssl/rand.h>
#include <openssl/err.h>

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
	r.fd = open("/dev/urandom",O_RDONLY);
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
	BIGNUM *n, *e, *d, *p, *q, *dmp1, *dmq1, *iqmp;

	RSA_get0_key(rsa, &n, &e, &d);
	RSA_get0_factors(rsa, &p, &q);
	RSA_get0_crt_params(rsa, &dmp1, &dmq1, &iqmp);

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
			n = BN_bin2bn(data.p,data.l,n);
		else if (!gale_text_compare(G_("rsa.exponent"),frag.name))
			e = BN_bin2bn(data.p,data.l,e);
		else if (!gale_text_compare(G_("rsa.private.exponent"),frag.name))
			d = BN_bin2bn(data.p,data.l,d);
		else if (2*GALE_RSA_PRIME_LEN == data.l
		     && !gale_text_compare(G_("rsa.private.prime"),frag.name)) {
			p = BN_bin2bn(data.p,GALE_RSA_PRIME_LEN,p);
			q = BN_bin2bn(
				GALE_RSA_PRIME_LEN + data.p,
				GALE_RSA_PRIME_LEN,
				q);
		}
		else if (2*GALE_RSA_PRIME_LEN == data.l
		     && !gale_text_compare(frag.name,
			G_("rsa.private.prime.exponent")))
		{
			dmp1 = BN_bin2bn(data.p,
				GALE_RSA_PRIME_LEN,
				dmp1);
			dmq1 = BN_bin2bn(
				GALE_RSA_PRIME_LEN + data.p,
				GALE_RSA_PRIME_LEN,
				dmq1);
		}
		else if (!gale_text_compare(frag.name,
			G_("rsa.private.coefficient")))
			iqmp = BN_bin2bn(data.p,data.l,iqmp);
	}

	RSA_set0_key(rsa, n, e, d);
	RSA_set0_factors(rsa, p, q);
	RSA_set0_crt_params(rsa, dmp1, dmq1, iqmp);

	return name;
}

int crypto_i_public_valid(RSA *rsa) {
	BIGNUM *n, *e;
	RSA_get0_key(rsa, &n, &e, NULL);
	return NULL != n && NULL != e;
}

int crypto_i_private_valid(RSA *rsa) {
	BIGNUM *d, *p, *q, *dmp1, *dmq1, *iqmp;

	RSA_get0_key(rsa, NULL, NULL, &d);
	RSA_get0_factors(rsa, &p, &q);
	RSA_get0_crt_params(rsa, &dmp1, &dmq1, &iqmp);

	return crypto_i_public_valid(rsa)
	    && NULL != d
	    && NULL != p && NULL != q
	    && NULL != dmp1 && NULL != dmq1
	    && NULL != iqmp;
}
