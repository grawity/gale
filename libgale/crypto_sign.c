#include "key_i.h"
#include "crypto_i.h"
#include "gale/crypto.h"
#include "gale/misc.h"

#include <assert.h>

/** Digitally sign some data.
 *  \param key_count Number of keys in the \a target array.
 *  \param target Array of keys.  The keys must include private key data.  
 *         Keys which contain key.source data fragments will be stored along 
 *         with the signature.
 *  \param data Group to sign.  Will be replaced by a signed group.
 *  \return Nonzero iff the operation succeeded.
 *  \sa gale_crypto_sender(), gale_crypto_verify(), gale_crypto_sign_raw() */
int gale_crypto_sign(int key_count,
	const struct gale_group *keys,
	struct gale_group *data) 
{
	struct gale_text *name;
	struct gale_data *source,original;
	const struct gale_data *sigs;
	int i;

	if (0 == key_count) return 1;

	gale_create_array(name,key_count);
	gale_create_array(source,key_count);
	for (i = 0; i < key_count; ++i) {
		struct gale_fragment frag;
		if (!gale_group_lookup(keys[i],G_("key.id"),frag_text,&frag)) {
			gale_alert(GALE_WARNING,G_("key with no name"),0);
			return 0;
		}
		name[i] = frag.value.text;

		if (gale_group_lookup(keys[i],G_("key.source"),frag_data,&frag))
			source[i] = frag.value.data;
		else
			source[i] = null_data;
	}

	original.l = 0;
	original.p = gale_malloc(gale_u32_size() + gale_group_size(*data));
	gale_pack_u32(&original,0);
	gale_pack_group(&original,*data);
	sigs = gale_crypto_sign_raw(key_count,keys,original);
	if (NULL == sigs) return 0;

	if (1 == key_count
	&& (0 == source->l
	|| !gale_text_compare(name[0],key_i_name(source[0])))) {
		struct gale_fragment frag;
		int sig_len;

		if (0 == source->l) {
			source->p = gale_malloc(
				  gale_copy_size(sizeof(key_magic2))
				+ gale_text_size(*name));
			gale_pack_copy(source,key_magic2,sizeof(key_magic2));
			gale_pack_text(source,*name);
		}

		sig_len = sizeof(sig_magic)
			+ gale_u32_size()
			+ gale_copy_size(sigs->l)
			+ gale_copy_size(source->l);

		frag.name = G_("security/signature");
		frag.type = frag_data;
		frag.value.data.l = 0;
		frag.value.data.p = gale_malloc(
			  gale_u32_size()
			+ sig_len
			+ gale_copy_size(original.l));

		gale_pack_u32(&frag.value.data,sig_len);
		gale_pack_copy(&frag.value.data,sig_magic,sizeof(sig_magic));
		gale_pack_u32(&frag.value.data,sigs->l);
		gale_pack_copy(&frag.value.data,sigs->p,sigs->l);
		gale_pack_copy(&frag.value.data,source->p,source->l);
		assert(sig_len + gale_u32_size() == frag.value.data.l);
		gale_pack_copy(&frag.value.data,original.p,original.l);

		*data = gale_group_empty();
		gale_group_add(data,frag);
	} else {
		struct gale_fragment frag;
		frag.name = G_("auth.signature");
		frag.type = frag_group;
		frag.value.group = gale_group_empty();

		for (i = key_count - 1; i >= 0; --i) {
			struct gale_fragment sub,subsub;

			sub.name = name[i];
			sub.type = frag_group;
			sub.value.group = gale_group_empty();

			if (0 != source[i].l) {
				subsub.name = G_("key");
				subsub.type = frag_data;
				subsub.value.data = source[i];
				gale_group_add(&sub.value.group,subsub);
			}

			subsub.name = G_("data");
			subsub.type = frag_data;
			subsub.value.data = sigs[i];
			gale_group_add(&sub.value.group,subsub);

			gale_group_add(&frag.value.group,sub);
		}

		gale_group_add(data,frag);
	}

	return 1;
}

/** List the keys that may have signed a group.
 *  \param signed_group The signed group.
 *  \return An array of key names, terminated by null_text.
 *  \sa gale_crypto_sign(), gale_crypto_verify()
 *  \warning This list of keys is not authoritative; the signatures have not
 *           yet been validated.  Use gale_crypto_verify() for that. */
const struct gale_text *gale_crypto_sender(struct gale_group signed_group) {
	struct gale_fragment frag;
	struct gale_text *output;

	if (gale_group_lookup(signed_group,
		G_("security/signature"),frag_data,&frag))
	{
		u32 len;
		struct gale_data sig = frag.value.data;

		if (!gale_unpack_u32(&sig,&len)
		||  len > sig.l) return &null_text;

		sig.l = len;
		if (!gale_unpack_compare(&sig,sig_magic,sizeof(sig_magic))
		||  !gale_unpack_skip(&sig)) 
			return &null_text;

		gale_create_array(output,2);
		output[0] = key_i_name(sig);
		output[1] = null_text;
		return output;
	}

	frag = gale_group_first(signed_group);
	if (frag_group == frag.type
	&& !gale_text_compare(frag.name,G_("auth.signature")))
	{
		struct gale_group group = frag.value.group;
		int count = 0;

		while (!gale_group_null(group)) {
			group = gale_group_rest(group);
			++count;
		}

		gale_create_array(output,1 + count);
		count = 0;
		group = frag.value.group;
		while (!gale_group_null(group)) {
			struct gale_fragment sub = gale_group_first(group);
			group = gale_group_rest(group);
			if (frag_group == sub.type)
				output[count++] = sub.name;
		}

		output[count++] = null_text;
		return output;
	}

	return &null_text;
}

/** Extract keys which have been bundled with a signed group.
 *  \param signed_group The signed group.
 *  \return An array of cached keys, terminated by null_data.  It is up to 
 *          the caller to process and validate these keys.
 *  \sa gale_crypto_sign(), gale_crypto_verify() */
const struct gale_data *gale_crypto_bundled(struct gale_group signed_group) {
	struct gale_fragment frag;
	struct gale_data *output;

	frag = gale_group_first(signed_group);
	if (frag_group == frag.type
	&& !gale_text_compare(frag.name,G_("auth.signature")))
	{
		struct gale_group group = frag.value.group;
		int count = 0;

		while (!gale_group_null(group)) {
			group = gale_group_rest(group);
			++count;
		}

		gale_create_array(output,1 + count);
		count = 0;
		group = frag.value.group;
		while (!gale_group_null(group)) {
			struct gale_fragment sub = gale_group_first(group);
			struct gale_fragment subsub;
			group = gale_group_rest(group);
			if (frag_group == sub.type
			&&  gale_group_lookup(sub.value.group,G_("key"),
				frag_data,&subsub))
				output[count++] = subsub.value.data;
		}

		output[count++] = null_data;
		return output;
	}

	if (gale_group_lookup(signed_group,
		G_("security/signature"),
		frag_data,&frag))
	{
		u32 len;
		struct gale_data sig = frag.value.data;

		if (!gale_unpack_u32(&sig,&len)
		||  len > sig.l) return &null_data;

		sig.l = len;
		if (!gale_unpack_compare(&sig,sig_magic,sizeof(sig_magic))
		||  !gale_unpack_skip(&sig)) 
			return &null_data;

		gale_create_array(output,2);
		output[0] = sig;
		output[1] = null_data;
		return output;
	}

	return &null_data;
}

/** Extract the original content from a signed group.
 *  \param signed_group The signed group.
 *  \return The data contained within the signed group.
 *  \sa gale_crypto_sign(), gale_crypto_verify()
 *  \warning Successful extraction of data does not indicate authenticity.
 *           You must use gale_crypto_verify() to check the signature. */
struct gale_group gale_crypto_original(struct gale_group signed_group)
{
	struct gale_fragment frag;

	if (gale_group_null(signed_group)) return signed_group;
	frag = gale_group_first(signed_group);

	if (frag_group == frag.type
	&& !gale_text_compare(frag.name,G_("auth.signature")))
		return gale_group_rest(signed_group);

	if (gale_group_lookup(signed_group,
		G_("security/signature"),
		frag_data,&frag))
	{
		struct gale_data data = frag.value.data;
		struct gale_group output;
		u32 zero;
		if (gale_unpack_skip(&data)
		&&  gale_unpack_u32(&data,&zero) && 0 == zero
		&&  gale_unpack_group(&data,&output)) return output;
	}

	return signed_group;
}

/** Verify that a group has been signed.
 *  \param key_count Number of keys in the \a keys array.
 *  \param keys Array of keys to test.  The keys must include public key data.
 *  \param signed_group The signed group.
 *  \sa gale_crypto_sign(), gale_crypto_sender(), 
 *      gale_crypto_bundled(), gale_crypto_verify_raw()
 *  \return Nonzero iff the operation succeeded and the data was in fact
 *          signed by all the keys in the list. */
int gale_crypto_verify(int key_count,
	const struct gale_group *keys,
	struct gale_group signed_group) 
{
	int i;
	struct gale_fragment frag;
	struct gale_data *sigs,original_data = null_data;
	struct gale_text *names;

	gale_create_array(names,key_count);
	gale_create_array(sigs,key_count);
	for (i = 0; i < key_count; ++i) {
		struct gale_fragment frag;
		if (!gale_group_lookup(keys[i],G_("key.id"),frag_text,&frag)) {
			gale_alert(GALE_WARNING,G_("key with no name"),0);
			return 0;
		}
		names[i] = frag.value.text;
		sigs[i] = null_data;
	}

	frag = gale_group_first(signed_group);
	if (frag_group == frag.type
	&& !gale_text_compare(frag.name,G_("auth.signature"))) {
		struct gale_group orig = gale_group_rest(signed_group);
		original_data.p = gale_malloc(
			  gale_u32_size()
			+ gale_group_size(orig));
		original_data.l = 0;
		gale_pack_u32(&original_data,0);
		gale_pack_group(&original_data,orig);

		while (!gale_group_null(frag.value.group)) {
			struct gale_fragment sub,subsub;
			sub = gale_group_first(frag.value.group);
			frag.value.group = 
				gale_group_rest(frag.value.group);
			for (i = 0; i < key_count; ++i)
				if (frag_group == sub.type
				&& !gale_text_compare(sub.name,names[i])
				&& gale_group_lookup(sub.value.group,
					G_("data"),frag_data,&subsub))
					sigs[i] = subsub.value.data;
		}
	}
	else if (gale_group_lookup(signed_group,
		G_("security/signature"),
		frag_data,&frag)) 
	{
		struct gale_data key,sig;
		struct gale_text name;
		u32 sig_len;

		original_data = frag.value.data;
		if (!gale_unpack_u32(&original_data,&sig_len) 
		||  sig_len > original_data.l) return 0;

		sig.p = original_data.p;
		sig.l = sig_len;
		original_data.p += sig_len;
		original_data.l -= sig_len;

		if (!gale_unpack_compare(&sig,sig_magic,sizeof(sig_magic))
		||  !gale_unpack_u32(&sig,&sig_len) 
		||  sig_len > sig.l) return 0;

		key.p = sig.p + sig_len;
		key.l = sig.l - sig_len;
		sig.l = sig_len;

		name = key_i_name(key);
		for (i = 0; i < key_count; ++i)
			if (!gale_text_compare(name,names[i]))
				sigs[i] = sig;
	}

	for (i = 0; i < key_count; ++i)
		if (0 == sigs[i].l) {
			gale_alert(GALE_WARNING,G_("missing signature"),0);
			return 0;
		}

	return gale_crypto_verify_raw(key_count,keys,sigs,original_data);
}
