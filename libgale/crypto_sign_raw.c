#include "crypto_i.h"
#include "gale/crypto.h"

#include <assert.h>
#include <openssl/evp.h>

/** Low-level signature operation.
 *  \param key_count Number of keys in the \a source array.
 *  \param source Array of keys.  The keys must include private key data.
 *  \param data Data to sign.
 *  \return Array of signatures, one for each key,
 *          or NULL if the operation failed. 
 *  \sa gale_crypto_verify_raw(), gale_crypto_sign() */
const struct gale_data *gale_crypto_sign_raw(int key_count,
        const struct gale_group *source,
        struct gale_data data)
{
	int i;
	struct gale_data *output;
	RSA *rsa;
	EVP_MD_CTX *context = EVP_MD_CTX_new();
	EVP_SignInit(context,EVP_md5());
	EVP_SignUpdate(context,data.p,data.l);

	gale_create_array(output,key_count);
	for (i = 0; NULL != output && i < key_count; ++i) {
		EVP_PKEY *key = EVP_PKEY_new();
		EVP_PKEY_assign_RSA(key,RSA_new());
		rsa = EVP_PKEY_get0_RSA(key);
		crypto_i_rsa(source[i],rsa);
		if (!crypto_i_private_valid(rsa)) {
			gale_alert(GALE_WARNING,G_("invalid private key"),0);
			output = NULL;
			goto cleanup;
		}

		output[i].p = gale_malloc(EVP_PKEY_size(key));
		if (!EVP_SignFinal(context,output[i].p,&output[i].l,key)) {
			crypto_i_error();
			output = NULL;
			goto cleanup;
		}

	cleanup:
		EVP_PKEY_free(key);
	}

	return output;
}

/** Low-level signature verification.
 *  \param key_count Number of keys in the \a keys array 
 *         and number fo signatures in the \a sigs array.
 *  \param keys Array of keys.  The keys must include public key data.
 *  \param sigs Array of signatures, as returned from gale_crypto_sign_raw().
 *  \param data Data to verify against signatures.
 *  \return Nonzero iff the all signatures are valid. */
int gale_crypto_verify_raw(int key_count,
        const struct gale_group *keys,
        const struct gale_data *sigs,
        struct gale_data data)
{
	int i,is_valid = 1;
	EVP_MD_CTX *context = EVP_MD_CTX_new();
	RSA *rsa;

	EVP_VerifyInit(context,EVP_md5());
	EVP_VerifyUpdate(context,data.p,data.l);
	for (i = 0; is_valid && i < key_count; ++i) {
		EVP_PKEY *key = EVP_PKEY_new();
		EVP_PKEY_assign_RSA(key,RSA_new());
		rsa = EVP_PKEY_get0_RSA(key);
		crypto_i_rsa(keys[i],rsa);
		if (!crypto_i_public_valid(rsa)) {
			gale_alert(GALE_WARNING,G_("invalid public key"),0);
			is_valid = 0;
			goto cleanup;
		}

		if (!EVP_VerifyFinal(context,sigs[i].p,sigs[i].l,key)) {
			crypto_i_error();
			is_valid = 0;
			goto cleanup;
		}

	cleanup:
		EVP_PKEY_free(key);
	}

	return is_valid;
}
