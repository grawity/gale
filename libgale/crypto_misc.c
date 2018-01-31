#include "crypto_i.h"
#include "gale/crypto.h"
#include <openssl/evp.h>
#include <openssl/rand.h>

/** Generate cryptographically random data.
 *  \param len The amount of data to generate.
 *  \return A block of high-entropy random data of length \a len. */
struct gale_data gale_crypto_random(int len) {
	struct gale_data output;
	output.p = gale_malloc(len);
	output.l = len;
	crypto_i_seed();
	if (RAND_bytes(output.p,output.l) <= 0) crypto_i_error();
	return output;
}

/** Compute a cryptographically secure hash of some data.
 *  \param len A block of data to hash.
 *  \return A block of data containing a secure hash of the data. */
struct gale_data gale_crypto_hash(struct gale_data orig) {
	EVP_MD_CTX * context = EVP_MD_CTX_new();
	struct gale_data output;

	output.p = gale_malloc(EVP_MAX_MD_SIZE);

	EVP_DigestInit(context,EVP_sha1());
	EVP_DigestUpdate(context,orig.p,orig.l);
	EVP_DigestFinal(context,output.p,&output.l);
	return output;
}
