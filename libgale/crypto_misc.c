#include "crypto_i.h"
#include "gale/crypto.h"
#include <openssl/evp.h>
#include <openssl/rand.h>

struct gale_data gale_crypto_random(int len) {
	struct gale_data output;
	output.p = gale_malloc(len);
	output.l = len;
	crypto_i_seed();
	if (RAND_bytes(output.p,output.l) <= 0) crypto_i_error();
	return output;
}

struct gale_data gale_crypto_hash(struct gale_data orig) {
	EVP_MD_CTX context;
	struct gale_data output;

	output.p = gale_malloc(EVP_MAX_MD_SIZE);

	EVP_DigestInit(&context,EVP_sha1());
	EVP_DigestUpdate(&context,orig.p,orig.l);
	EVP_DigestFinal(&context,output.p,&output.l);
	return output;
}
