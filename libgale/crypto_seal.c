#include "key_i.h"
#include "crypto_i.h"
#include "gale/crypto.h"

#include <assert.h>
#include <openssl/evp.h>

static const byte magic[] = { 0x68, 0x13, 0x02, 0x00 };
static const byte magic2[] = { 0x68, 0x13, 0x02, 0x01 };

#define IV_LEN 8

/** Encrypt some data.
 *  \param key_count Number of keys in the \a target array.
 *  \param target Array of keys.  Anyone who owns any of these keys will be 
 *         able to decrypt the data.  These keys must include public key data.
 *  \param data Group to encrypt.  Will be replaced by an encrypted group.
 *  \return Nonzero iff the operation succeeded.
 *  \sa gale_crypto_target(), gale_crypto_open() */
int gale_crypto_seal(
	int key_count,const struct gale_group *target,
	struct gale_group *data)
{
	struct gale_fragment frag;
	struct gale_data plain,cipher;
	EVP_CIPHER_CTX *context = EVP_CIPHER_CTX_new();

	int i,*session_key_length;
	unsigned char **session_key,iv[EVP_MAX_IV_LENGTH];
	struct gale_text *raw_name;
	EVP_PKEY **public_key;
	RSA *rsa;

	int good_count = 0,is_successful = 0;

	plain.p = gale_malloc(gale_group_size(*data) + gale_u32_size());
	plain.l = 0;
	gale_pack_u32(&plain,0); /* version identifier? */
	gale_pack_group(&plain,*data);
	*data = gale_group_empty();

	gale_create_array(raw_name,key_count);
	gale_create_array(public_key,key_count);
	for (i = 0; i < key_count; ++i) public_key[i] = NULL;
	for (i = 0; i < key_count; ++i) {
		public_key[good_count] = EVP_PKEY_new();
		EVP_PKEY_assign_RSA(public_key[good_count],RSA_new());
		rsa = EVP_PKEY_get0_RSA(public_key[good_count]);
		raw_name[good_count] = key_i_swizzle(crypto_i_rsa(
			target[i],rsa));
		if (0 != raw_name[good_count].l
		&&  crypto_i_public_valid(rsa))
			++good_count;
		else
			EVP_PKEY_free(public_key[good_count]);
	}

	gale_create_array(session_key_length,good_count);
	gale_create_array(session_key,good_count);
	for (i = 0; i < good_count; ++i) 
		gale_create_array(session_key[i],EVP_PKEY_size(public_key[i]));

	crypto_i_seed();
	if (!EVP_SealInit(context,EVP_des_ede3_cbc(),
		session_key,session_key_length,iv,public_key,good_count)) {
		crypto_i_error();
		goto cleanup;
	}

	cipher.l = gale_copy_size(sizeof(magic2))
	         + gale_copy_size(EVP_CIPHER_CTX_iv_length(context))
	         + gale_u32_size()
	         + plain.l + EVP_CIPHER_CTX_block_size(context) - 1;
	for (i = 0; i < good_count; ++i)
		cipher.l += gale_text_size(raw_name[i])
		         +  gale_u32_size()
		         +  gale_copy_size(session_key_length[i]);

	cipher.p = gale_malloc(cipher.l);
	cipher.l = 0;

	assert(IV_LEN == EVP_CIPHER_CTX_iv_length(context));
	gale_pack_copy(&cipher,magic2,sizeof(magic2));
	gale_pack_copy(&cipher,iv,IV_LEN);
	gale_pack_u32(&cipher,good_count);
	for (i = 0; i < good_count; ++i) {
		gale_pack_text(&cipher,raw_name[i]);
		gale_pack_u32(&cipher,session_key_length[i]);
		gale_pack_copy(&cipher,session_key[i],session_key_length[i]);
	}

	EVP_SealUpdate(context,cipher.p + cipher.l,&i,plain.p,plain.l);
	cipher.l += i;

	EVP_SealFinal(context,cipher.p + cipher.l,&i);
	cipher.l += i;

	frag.type = frag_data;
	frag.name = G_("security/encryption");
	frag.value.data = cipher;
	gale_group_add(data,frag);

	is_successful = 1;
cleanup:
	for (i = 0; i < good_count; ++i)
		if (NULL != public_key[i]) EVP_PKEY_free(public_key[i]);
	return is_successful;
}

/** List the keys that can decrypt an encrypted group.
 *  \param encrypted Encrypted group to examine.
 *  \return NULL iff the group is not encrypted.  Otherwise, an array of
 *          key names, terminated by null_text.
 *  \sa gale_crypto_seal(), gale_crypto_open() */
const struct gale_text *gale_crypto_target(struct gale_group encrypted) {
	struct gale_fragment frag;
	struct gale_data data;
	struct gale_text *output;

	unsigned char iv[IV_LEN];
	u32 i,key_count;

	if (gale_group_null(encrypted)) return NULL;
	frag = gale_group_first(encrypted);
	if (gale_text_compare(G_("security/encryption"),frag.name)
	||  frag_data != frag.type) return NULL;

	data = frag.value.data;
	if (!gale_unpack_compare(&data,magic2,sizeof(magic2))
	||  !gale_unpack_copy(&data,iv,sizeof(iv))
	||  !gale_unpack_u32(&data,&key_count)) {
		gale_alert(GALE_WARNING,G_("unknown encryption format"),0);
		gale_create(output);
		*output = null_text;
		return output;
	}

	gale_create_array(output,1 + key_count);
	for (i = 0; i < key_count; ++i) {
		struct gale_text name;
		if (!gale_unpack_text(&data,&name)
		||  !gale_unpack_skip(&data)) {
			gale_alert(GALE_WARNING,G_("invalid encryption"),0);
			gale_create(output);
			*output = null_text;
			return NULL;
		}

		output[i] = key_i_swizzle(name);
	}

	output[i] = null_text;
	return output;
}

/** Decrypt some data.
 *  \param key Key to use for decryption.  Use gale_crypto_target() to find the
 *         keys you can use, pick one you own, and supply private key data.
 *  \param data Encrypted group.  Will be replaced by decrypted group.
 *  \return Nonzero iff the operation succeeded.
 *  \sa gale_crypto_seal(), gale_crypto_target() */
int gale_crypto_open(struct gale_group key,struct gale_group *cipher) {
	struct gale_fragment frag;
	struct gale_data data;
	unsigned char iv[IV_LEN];
	u32 i,key_count;
	EVP_PKEY *private_key = NULL;
	RSA *rsa;
	struct gale_text raw_name;
	struct gale_data session_key,plain;
	EVP_CIPHER_CTX *context = EVP_CIPHER_CTX_new();
	int length,is_successful = 0;

	if (gale_group_null(*cipher)) goto cleanup;
	frag = gale_group_first(*cipher);
	if (gale_text_compare(G_("security/encryption"),frag.name)
	||  frag_data != frag.type) {
		gale_alert(GALE_WARNING,G_("can't decrypt unencrypted data"),0);
		goto cleanup;
	}

	data = frag.value.data;
	if (!gale_unpack_compare(&data,magic2,sizeof(magic2))
	||  !gale_unpack_copy(&data,iv,sizeof(iv))
	||  !gale_unpack_u32(&data,&key_count)) goto cleanup;

	private_key = EVP_PKEY_new();
	EVP_PKEY_assign_RSA(private_key,RSA_new());
	rsa = EVP_PKEY_get0_RSA(private_key);
	raw_name = key_i_swizzle(crypto_i_rsa(key,rsa));
	if (!crypto_i_private_valid(rsa)) {
		gale_alert(GALE_WARNING,G_("invalid private key"),0);
		goto cleanup;
	}

	session_key = null_data;
	for (i = 0; i < key_count; ++i) {
		struct gale_text name;
		if (!gale_unpack_text(&data,&name)) goto cleanup;
		if (gale_text_compare(raw_name,name)) {
			if (!gale_unpack_skip(&data)) goto cleanup;
			continue;
		}

		if (!gale_unpack_u32(&data,&session_key.l)) goto cleanup;
		session_key.p = gale_malloc(session_key.l);
		if (!gale_unpack_copy(&data,session_key.p,session_key.l)) 
			goto cleanup;
	}

	if (0 == session_key.l) {
		gale_alert(GALE_WARNING,G_("key doesn't fit encrypted data"),0);
		goto cleanup;
	}

	if (!EVP_OpenInit(context,EVP_des_ede3_cbc(),
		session_key.p,session_key.l,iv,private_key)) {
		crypto_i_error();
		goto cleanup;
	}

	plain.p = gale_malloc(data.l);
	plain.l = 0;

	EVP_OpenUpdate(context,plain.p + plain.l,&length,data.p,data.l);
	plain.l += length;
	EVP_OpenFinal(context,plain.p + plain.l,&length);
	plain.l += length;

	if (!gale_unpack_u32(&plain,&i) || 0 != i
	||  !gale_unpack_group(&plain,cipher)) {
		gale_alert(GALE_WARNING,G_("invalid encrypted data"),0);
		goto cleanup;
	}

	is_successful = 1;
cleanup:
	if (NULL != private_key) EVP_PKEY_free(private_key);
	return is_successful;
}
