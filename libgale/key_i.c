#include "key_i.h"
#include "crypto_i.h" /* For GALE_RSA_... constants */
#include "gale/crypto.h"
#include "gale/misc.h"

#include <assert.h>

struct gale_text key_i_swizzle(struct gale_text name) {
	int at;
	struct gale_text local,component,output;

	for (at = 0; at < name.l && '@' != name.p[at]; ++at) ;
	if (name.l == at) return name;
	local = gale_text_left(name,at);
	if (at > 0) name = gale_text_right(name,-at);

	component = output = null_text;
	while (gale_text_token(local,'.',&component))
		output = (0 == output.l) ? component
		       : gale_text_concat(3,component,G_("."),output);
	return gale_text_concat(2,output,name);
}

int key_i_private(struct gale_data key) {
	return gale_unpack_compare(&key,priv_magic1,sizeof(priv_magic1))
	    || gale_unpack_compare(&key,priv_magic2,sizeof(priv_magic2))
	    || gale_unpack_compare(&key,priv_magic3,sizeof(priv_magic3));
}

static struct gale_text get_name(struct gale_data *key) {
	if (gale_unpack_compare(key,key_magic1,sizeof(key_magic1))) {
                const char *sz;
                if (gale_unpack_str(key,&sz))
			return key_i_swizzle(gale_text_from(NULL,sz,-1));
	} else
	if (gale_unpack_compare(key,key_magic2,sizeof(key_magic2))
	||  gale_unpack_compare(key,key_magic3,sizeof(key_magic3))) {
                struct gale_text name;
                if (gale_unpack_text(key,&name))
	                return key_i_swizzle(name);
	} else 
	if (gale_unpack_compare(key,priv_magic1,sizeof(priv_magic1))) {
		const char *sz;
		if (gale_unpack_str(key,&sz))
			return key_i_swizzle(gale_text_from(NULL,sz,-1));
	} else
	if (gale_unpack_compare(key,priv_magic2,sizeof(priv_magic2))
	||  gale_unpack_compare(key,priv_magic3,sizeof(priv_magic3))) {
		struct gale_text name;
		if (gale_unpack_text(key,&name))
	                return key_i_swizzle(name);
	}

	return null_text;
}

struct gale_text key_i_name(struct gale_data key) {
	return get_name(&key);
}

int key_i_stub(struct gale_data key) {
	get_name(&key);
	return 0 == key.l;
}

const struct gale_data *key_i_bundled(struct gale_data key) {
	if (gale_unpack_compare(&key,key_magic1,sizeof(key_magic1))) {
		byte data[GALE_RSA_MODULUS_LEN];
		const char *sz;
		u32 bits;
		if (gale_unpack_str(&key,&sz)
		&&  gale_unpack_str(&key,&sz)
		&&  gale_unpack_u32(&key,&bits)
		&&  gale_unpack_rle(&key,data,sizeof(data))
		&&  gale_unpack_rle(&key,data,sizeof(data))
		&&  gale_unpack_compare(&key,sig_magic,sizeof(sig_magic))
		&&  gale_unpack_skip(&key)) {
			struct gale_data *output;
			gale_create_array(output,2);
			output[0] = key;
			output[1] = null_data;
			return output;
		}
	} else
	if (gale_unpack_compare(&key,key_magic2,sizeof(key_magic2))) {
		byte data[GALE_RSA_MODULUS_LEN];
		struct gale_text text;
		struct gale_time time;
		u32 bits;
		if (gale_unpack_text(&key,&text)
		&&  gale_unpack_text(&key,&text)
		&&  gale_unpack_u32(&key,&bits)
		&&  gale_unpack_rle(&key,data,sizeof(data))
		&&  gale_unpack_rle(&key,data,sizeof(data))
		&&  gale_unpack_time(&key,&time)
		&&  gale_unpack_time(&key,&time)
		&&  gale_unpack_compare(&key,sig_magic,sizeof(sig_magic))
		&&  gale_unpack_skip(&key)) {
			struct gale_data *output;
			gale_create_array(output,2);
			output[0] = key;
			output[1] = null_data;
			return output;
		}
	} else
	if (gale_unpack_compare(&key,key_magic3,sizeof(key_magic3))) {
		struct gale_text text;
		struct gale_group group;
		if (gale_unpack_text(&key,&text)
		&&  gale_unpack_group(&key,&group))
			return gale_crypto_bundled(group);
	}

	return &null_data;
}

static struct gale_group public(
	struct gale_text name,struct gale_text comment,
	u32 bits,struct gale_data modulus,struct gale_data exponent,
	struct gale_time sign,struct gale_time expires)
{
	struct gale_fragment frag;
	struct gale_group output = gale_group_empty();

	frag.name = G_("rsa.exponent");
	frag.type = frag_data;
	frag.value.data = exponent;
	gale_group_add(&output,frag);

	frag.name = G_("rsa.modulus");
	frag.type = frag_data;
	frag.value.data = modulus;
	gale_group_add(&output,frag);

	frag.name = G_("rsa.bits");
	frag.type = frag_number;
	frag.value.number = bits;
	gale_group_add(&output,frag);

	frag.name = G_("key.expires");
	frag.type = frag_time;
	frag.value.time = expires;
	gale_group_add(&output,frag);

	frag.name = G_("key.signed");
	frag.type = frag_time;
	frag.value.time = sign;
	gale_group_add(&output,frag);

	frag.name = G_("key.owner");
	frag.type = frag_text;
	frag.value.text = comment;
	gale_group_add(&output,frag);

	frag.name = G_("key.id");
	frag.type = frag_text;
	frag.value.text = key_i_swizzle(name);
	gale_group_add(&output,frag);

	return output;
}

static int get(
	struct gale_data *key,struct gale_group *output,
	struct gale_text name,unsigned len) 
{
	struct gale_fragment frag;
	frag.type = frag_data;
	frag.name = name;
	frag.value.data.p = gale_malloc(len);
	frag.value.data.l = len;
	if (!gale_unpack_rle(key,frag.value.data.p,frag.value.data.l)) return 0;
	gale_group_add(output,frag);
	return 1;
}

static struct gale_group private(struct gale_data key,struct gale_text name) {
	struct gale_group output = gale_group_empty();
	struct gale_fragment frag;
	const unsigned m = GALE_RSA_MODULUS_LEN;
	const unsigned p = GALE_RSA_PRIME_LEN;

	name = key_i_swizzle(name);

	frag.name = G_("rsa.bits");
	frag.type = frag_number;
	if (!gale_unpack_u32(&key,&frag.value.number)
        ||  !get(&key,&output,G_("rsa.modulus"),m)
        ||  !get(&key,&output,G_("rsa.exponent"),m)
        ||  !get(&key,&output,G_("rsa.private.exponent"),m)
        ||  !get(&key,&output,G_("rsa.private.prime"),2 * p)
        ||  !get(&key,&output,G_("rsa.private.prime.exponent"),2*p)
        ||  !get(&key,&output,G_("rsa.private.coefficient"),p)
        ||  0 != key.l) {
		gale_alert(GALE_WARNING,gale_text_concat(3,G_("\""),
			name,G_("\": ignoring malformed private key")),0);
                return gale_group_empty();
        }

	gale_group_add(&output,frag);

	frag.name = G_("key.id");
	frag.type = frag_text;
	frag.value.text = name;
	gale_group_add(&output,frag);

	return output;
}

struct gale_group key_i_group(struct gale_data key) {
	if (gale_unpack_compare(&key,key_magic1,sizeof(key_magic1))) {
		struct gale_data modulus,exponent;
		const char *name,*comment;
		u32 bits;

		modulus.p = gale_malloc((modulus.l = GALE_RSA_MODULUS_LEN));
		exponent.p = gale_malloc((exponent.l = GALE_RSA_MODULUS_LEN));
		if (gale_unpack_str(&key,&name)
		&&  gale_unpack_str(&key,&comment)
		&&  gale_unpack_u32(&key,&bits)
		&&  gale_unpack_rle(&key,modulus.p,modulus.l)
		&&  gale_unpack_rle(&key,exponent.p,exponent.l))
			return public(
				gale_text_from(NULL,name,-1),
				gale_text_from(NULL,comment,-1),
				bits,modulus,exponent,
				gale_time_zero(),gale_time_forever());
	} else
	if (gale_unpack_compare(&key,key_magic2,sizeof(key_magic2))) {
		struct gale_data modulus,exponent;
		struct gale_text name,comment;
		u32 bits;

		modulus.p = gale_malloc((modulus.l = GALE_RSA_MODULUS_LEN));
		exponent.p = gale_malloc((exponent.l = GALE_RSA_MODULUS_LEN));
		if (gale_unpack_text(&key,&name)
		&&  gale_unpack_text(&key,&comment)
		&&  gale_unpack_u32(&key,&bits)
		&&  gale_unpack_rle(&key,modulus.p,modulus.l)
		&&  gale_unpack_rle(&key,exponent.p,exponent.l))
		{
			struct gale_time sign,expires;
			if (!gale_unpack_time(&key,&sign)
			||  !gale_unpack_time(&key,&expires)) {
				sign = gale_time_zero();
				expires = gale_time_forever();
			}
			return public(
				name,comment,
				bits,modulus,exponent,
				sign,expires);
		}
	} else
	if (gale_unpack_compare(&key,key_magic3,sizeof(key_magic3))) {
		struct gale_text name;
		struct gale_group group;
		if (gale_unpack_text(&key,&name)
		&&  gale_unpack_group(&key,&group)) {
			struct gale_fragment frag;
			frag.name = G_("key.id");
			frag.type = frag_text;
			frag.value.text = key_i_swizzle(name);
			group = gale_crypto_original(group);
			gale_group_replace(&group,frag);
			return group;
		}
	} else
	if (gale_unpack_compare(&key,priv_magic1,sizeof(priv_magic1))) {
		const char *name;
		if (gale_unpack_str(&key,&name))
			return private(key,gale_text_from(NULL,name,-1));
	} else
	if (gale_unpack_compare(&key,priv_magic2,sizeof(priv_magic2))) {
		struct gale_text name;
		if (gale_unpack_text(&key,&name))
			return private(key,name);
	} else
	if (gale_unpack_compare(&key,priv_magic3,sizeof(priv_magic3))) {
		struct gale_text name;
		struct gale_group group;
		if (gale_unpack_text(&key,&name)
		&&  gale_unpack_group(&key,&group)) {
			struct gale_fragment frag;
			frag.name = G_("key.id");
			frag.type = frag_text;
			frag.value.text = key_i_swizzle(name);
			group = gale_crypto_original(group);
			gale_group_replace(&group,frag);
			return group;
		}
	}

	return gale_group_empty();
}

static int verify(
	struct gale_data orig,
	struct gale_data sig,
	struct gale_group signer)
{
	u32 sig_len;
	assert(sig.p >= orig.p && sig.p + sig.l <= orig.p + orig.l);
	orig.l = sig.p - orig.p;

	if (gale_unpack_compare(&sig,sig_magic,sizeof(sig_magic))
	&&  gale_unpack_u32(&sig,&sig_len) && sig_len <= sig.l) {
		sig.l = sig_len;
		return gale_crypto_verify_raw(1,&signer,&sig,orig);
	}

	return 0;
}

int key_i_verify(struct gale_data original,struct gale_group signer) {
	struct gale_data key = original;
	if (gale_unpack_compare(&key,key_magic1,sizeof(key_magic1))) {
		byte data[GALE_RSA_MODULUS_LEN];
		const char *sz;
		u32 bits;
		if (gale_unpack_str(&key,&sz)
		&&  gale_unpack_str(&key,&sz)
		&&  gale_unpack_u32(&key,&bits)
		&&  gale_unpack_rle(&key,data,sizeof(data))
		&&  gale_unpack_rle(&key,data,sizeof(data)))
			return verify(original,key,signer);
	} else
	if (gale_unpack_compare(&key,key_magic2,sizeof(key_magic2))) {
		byte data[GALE_RSA_MODULUS_LEN];
		struct gale_text text;
		struct gale_time time;
		u32 bits;
		if (gale_unpack_text(&key,&text)
		&&  gale_unpack_text(&key,&text)
		&&  gale_unpack_u32(&key,&bits)
		&&  gale_unpack_rle(&key,data,sizeof(data))
		&&  gale_unpack_rle(&key,data,sizeof(data))
		&&  gale_unpack_time(&key,&time)
		&&  gale_unpack_time(&key,&time))
			return verify(original,key,signer);
	} else
	if (gale_unpack_compare(&key,key_magic3,sizeof(key_magic3))) {
		struct gale_text text;
		struct gale_group group;
		if (gale_unpack_text(&key,&text)
		&&  gale_unpack_group(&key,&group))
			return gale_crypto_verify(1,&signer,group);
	}

	return 0;
}

struct gale_data key_i_create(struct gale_group source) {
	struct gale_group original = gale_crypto_original(source);
	struct gale_text name = null_text;
	struct gale_data output;
	int is_private = 0;

	while (!gale_group_null(original)) {
		struct gale_fragment first = gale_group_first(original);
		original = gale_group_rest(original);

		if (frag_text == first.type
		&& !gale_text_compare(G_("key.id"),first.name))
			name = key_i_swizzle(first.value.text);
		else if (!gale_text_compare(
			G_("rsa.private"),
			gale_text_left(first.name,11)))
			is_private = 1;
	}

	output.l = gale_text_size(name)
	         + gale_group_size(source)
	         + gale_copy_size(is_private 
			? sizeof(priv_magic3) 
			: sizeof(key_magic3));

	output.p = gale_malloc(output.l);
	output.l = 0;
	if (is_private)
		gale_pack_copy(&output,priv_magic3,sizeof(priv_magic3));
	else
		gale_pack_copy(&output,key_magic3,sizeof(key_magic3));
	gale_pack_text(&output,name);
	gale_pack_group(&output,source);
	return output;
}
