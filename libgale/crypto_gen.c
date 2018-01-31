#include "crypto_i.h"
#include "gale/crypto.h"

#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <assert.h>
#include <stdarg.h>

static void add_bignum(
	struct gale_group *group,struct gale_text name,
	int size,int count,BIGNUM *b,...) 
{
	struct gale_fragment frag;
	va_list ap;

	frag.name = name;
	frag.type = frag_data;
	frag.value.data.p = gale_malloc(size * count);
	frag.value.data.l = 0;

	va_start(ap,b);
	while (count--) {
		const int len = BN_num_bytes(b);
		assert(len <= size);
		memset(frag.value.data.p + frag.value.data.l,0,size);
		BN_bn2bin(b,frag.value.data.p + frag.value.data.l + size - len);
		frag.value.data.l += size;
		b = va_arg(ap,BIGNUM *);
	}
	va_end(ap);

	gale_group_add(group,frag);
}

/** Generate a new key.
 *  \param id The name to embed in the key.
 *  \return The newly generated key, containing public and private data.
 *  \sa gale_crypto_public() */
struct gale_group gale_crypto_generate(struct gale_text id) {
	RSA *rsa = NULL;
	int bits = gale_text_to_number(gale_var(G_("GALE_AUTH_BITS")));
	struct gale_group output = gale_group_empty();
	struct gale_fragment frag;
	BIGNUM *n, *e, *d, *p, *q, *dmp1, *dmq1, *iqmp;

	if (0 == bits) bits = 768; /* default value */
	if (bits < 512) {
		gale_alert(GALE_WARNING,G_("expanding key size to 512"),0);
		bits = 512;
	}

	crypto_i_seed();
	gale_alert(GALE_NOTICE,G_("generating key, please wait..."),0);
	rsa = RSA_generate_key(bits,RSA_F4,NULL,NULL);
	assert(NULL != rsa);

	frag.type = frag_text;
	frag.name = G_("key.id");
	frag.value.text = id;
	gale_group_add(&output,frag);

	frag.type = frag_number;
	frag.name = G_("rsa.bits");
	frag.value.number = bits;
	gale_group_add(&output,frag);

	RSA_get0_key(rsa, &n, &e, &d);
	add_bignum(&output,G_("rsa.modulus"),GALE_RSA_MODULUS_LEN,1,n);
	add_bignum(&output,G_("rsa.exponent"),GALE_RSA_MODULUS_LEN,1,e);
	add_bignum(&output,G_("rsa.private.exponent"),
		GALE_RSA_MODULUS_LEN,1,d);

	RSA_get0_factors(rsa, &p, &q);
	add_bignum(&output,G_("rsa.private.prime"),
		GALE_RSA_PRIME_LEN,2,p,q);

	RSA_get0_crt_params(rsa, &dmp1, &dmq1, &iqmp);
	add_bignum(&output,G_("rsa.private.prime.exponent"),
		GALE_RSA_PRIME_LEN,2,dmp1,dmq1);
	add_bignum(&output,G_("rsa.private.coefficient"),
		GALE_RSA_PRIME_LEN,1,iqmp);

	if (NULL != rsa) RSA_free(rsa);
	return output;
}

/** Extract the public components of a key.
 *  \param key A key which may contain private data.
 *  \return The same key with all private data expunged.
 *  \sa gale_crypto_generate() */
struct gale_group gale_crypto_public(struct gale_group key) {
	struct gale_group filtered = key;
	while (!gale_group_null(key)) {
		struct gale_fragment frag = gale_group_first(key);
		key = gale_group_rest(key);

		if (gale_text_compare(G_("rsa.private"),frag.name) <= 0
		&&  gale_text_compare(G_("rsa.private.~"),frag.name) > 0) {
			gale_group_remove(&filtered,frag.name,frag.type);
			key = filtered;
		}
	}

	return filtered;
}
