#include "file.h"
#include "pack.h"
#include "key.h"
#include "id.h"

#include <stdio.h>
#include <netinet/in.h>
#include <assert.h>
#include <string.h>
#include <sys/time.h>

/* old public key format:

   magic: 0x6813 0x0000 (v2: 0x6813 0x0002)
   id: NUL-terminated (v2: counted Unicode)
   -- possibly truncated here: EXPORT_STUB --
   comment: NUL-terminated (v2: counted Unicode)
   bits: dword
   modulus, exponent: RLE char[MAX_RSA_MODULUS_LEN]
   -- possibly truncated here: EXPORT_SIGN --
   (v2: signed: time)
   (v2: expired: time)
   signature for everything above

   new public key format:

   magic: 0x4741 0x4C45 0x0001
   id: counted Unicode
   -- possibly truncated here: EXPORT_STUB --
   data: fragment group (no zero!)
*/

static const byte magic[] = { 0x68, 0x13, 0x00, 0x00 };
static const byte magic2[] = { 0x68, 0x13, 0x00, 0x02 };
static const byte magic3[] = { 0x47, 0x41, 0x4C, 0x45, 0x00, 0x01 };

struct gale_text _ga_signer(struct gale_text text) {
	while (text.l > 0 && !strchr(".@:/",*text.p)) {
		--text.l;
		++text.p;
	}
	if (text.l) {
		--text.l;
		++text.p;
		return text;
	}
	return G_("ROOT");
}

int _ga_pub_equal(struct gale_group a,struct gale_group b) {
	struct gale_fragment mod[2],exp[2],bits[2];
	if (!gale_group_lookup(a,G_("rsa.modulus"),frag_data,&mod[0])
	||  !gale_group_lookup(a,G_("rsa.exponent"),frag_data,&exp[0])
	||  !gale_group_lookup(a,G_("rsa.bits"),frag_number,&bits[0])
	||  !gale_group_lookup(b,G_("rsa.modulus"),frag_data,&mod[1])
	||  !gale_group_lookup(b,G_("rsa.exponent"),frag_data,&exp[1])
	||  !gale_group_lookup(b,G_("rsa.bits"),frag_number,&bits[1])) return 0;

	return (0 == gale_data_compare(mod[0].value.data,mod[1].value.data)
	    &&  0 == gale_data_compare(exp[0].value.data,exp[1].value.data)
	    &&  bits[0].value.number == bits[1].value.number);
}

int _ga_pub_older(struct gale_group a,struct gale_group b) {
	struct gale_fragment s[2];
	if (!gale_group_lookup(a,G_("key.signed"),frag_time,&s[0]))
		return (gale_group_lookup(b,G_("key.signed"),frag_time,&s[1]));
	if (!gale_group_lookup(b,G_("key.signed"),frag_time,&s[1]))
		return 0;
	return gale_time_compare(s[0].value.time,s[1].value.time) < 0;
}

int _ga_pub_rsa(struct gale_group group,R_RSA_PUBLIC_KEY *rsa) {
	struct gale_fragment mod,exp,bits;

	memset(rsa,0,sizeof(*rsa));
	if (!gale_group_lookup(group,G_("rsa.modulus"),frag_data,&mod)
	||  MAX_RSA_MODULUS_LEN != mod.value.data.l
	||  !gale_group_lookup(group,G_("rsa.exponent"),frag_data,&exp)
	||  MAX_RSA_MODULUS_LEN != exp.value.data.l
	||  !gale_group_lookup(group,G_("rsa.bits"),frag_number,&bits)
	||  bits.value.number < MIN_RSA_MODULUS_BITS
	||  bits.value.number > MAX_RSA_MODULUS_BITS)
		return 0;

	rsa->bits = bits.value.number;
	memcpy(rsa->modulus,mod.value.data.p,mod.value.data.l);
	memcpy(rsa->exponent,exp.value.data.p,exp.value.data.l);
	return 1;
}

void _ga_import_pub(struct auth_id **id,struct gale_data key,
                    struct inode *source,int trust) {
	struct auth_id *try = NULL;
	const struct gale_data save = key; /* to verify signature */
	struct gale_group data = gale_group_empty();
	struct gale_fragment frag;
	struct signature sig;
	u32 version;
	int valid = 0;

	*id = NULL;
	_ga_init_sig(&sig);

	if (gale_unpack_compare(&key,magic,sizeof(magic)))
		version = 1;
	else if (gale_unpack_compare(&key,magic2,sizeof(magic2)))
		version = 2;
	else if (gale_unpack_compare(&key,magic3,sizeof(magic3)))
		version = 3;
	else 
		goto invalid;

	if (version > 1) {
		struct gale_text name;
		if (!gale_unpack_text(&key,&name)) goto invalid;
		init_auth_id(&try,name);
	} else {
		const char *sz;
		if (!gale_unpack_str(&key,&sz)) goto invalid;
		init_auth_id(&try,gale_text_from(NULL,sz,-1));
	}

	/* stub key */
	if (key.l == 0) goto success;

	if (version > 2) {
		if (!gale_unpack_group(&key,&data)) goto invalid;
		sig.id = auth_verify(&data);
	} else {
		frag.type = frag_text;
		frag.name = G_("key.owner");
		if (version > 1) {
			if (!gale_unpack_text(&key,&frag.value.text)) 
				goto invalid;
		} else {
			const char *sz;
			if (!gale_unpack_str(&key,&sz)) goto invalid;
			frag.value.text = gale_text_from(NULL,sz,-1);
		}
		gale_group_add(&data,frag);

		frag.type = frag_number;
		frag.name = G_("rsa.bits");
		if (!gale_unpack_u32(&key,(u32 *) &frag.value.number)
		||  frag.value.number < MIN_RSA_MODULUS_BITS
		||  frag.value.number > MAX_RSA_MODULUS_BITS) goto invalid;
		gale_group_add(&data,frag);

		frag.type = frag_data;
		frag.name = G_("rsa.modulus");
		frag.value.data.l = MAX_RSA_MODULUS_LEN;
		frag.value.data.p = gale_malloc(frag.value.data.l);
		if (!gale_unpack_rle(&key,frag.value.data.p,frag.value.data.l))
			goto invalid;
		gale_group_add(&data,frag);

		frag.name = G_("rsa.exponent");
		frag.value.data.p = gale_malloc(frag.value.data.l);
		if (!gale_unpack_rle(&key,frag.value.data.p,frag.value.data.l))
			goto invalid;
		gale_group_add(&data,frag);

		if (version > 1 && key.l > 0) {
			frag.type = frag_time;
			frag.name = G_("key.signed");
			if (!gale_unpack_time(&key,&frag.value.time)) 
				goto invalid;
			gale_group_add(&data,frag);

			frag.name = G_("key.expires");
			if (!gale_unpack_time(&key,&frag.value.time)) 
				goto invalid;
			gale_group_add(&data,frag);
		}
	}

	if (try->pub_trusted && !trust) goto ignore;

	/* is the existing key valid? */
	if (NULL == source) {
		/* check any network import against the local filesystem */
		auth_hook *hook_save = gale_global->find_public;
		gale_global->find_public = NULL; /* no AKD */
		valid = auth_id_public(try);
		gale_global->find_public = hook_save;
	} else
		valid = _ga_trust_pub(try);

	if (valid && !trust && !_ga_pub_older(try->pub_data,data)) goto ignore;

	if (2 >= version && 0 != key.l) {
		struct gale_data blob = save;
		blob.l = key.p - blob.p;
		_ga_import_sig(&sig,key);
		if (!sig.id || !auth_id_public(sig.id)) {
			_ga_warn_id(G_("\"%\": cannot find signing key"),try);
			_ga_init_sig(&sig);
		} else if (!_ga_verify(&sig,blob))
			_ga_init_sig(&sig);
	}

	if (NULL != sig.id
	&&  gale_text_compare(sig.id->name,_ga_signer(try->name))) {
		_ga_warn_id(G_("key \"%\" cannot certify \"%\""),sig.id,try);
		_ga_init_sig(&sig);
	}

	if (valid && !trust && !_ga_trust_pub(sig.id)) goto ignore;

	/* OK... now install the key. */
	if (!gale_group_null(try->pub_data) 
	&&  !_ga_pub_equal(data,try->pub_data)) {
		gale_alert(GALE_NOTICE,G_("replacing an old public key"),0);
#if !SAFE_KEY
		_ga_erase_inode(try->pub_inode);
#endif
	}

	try->pub_data = data;
	try->pub_orig = save;
	try->pub_signer = sig.id;
	try->pub_trusted = trust;

	if (NULL != source)
		try->pub_inode = *source;
	else
		_ga_save_file(gale_global->sys_cache,try->name,0644,
		              save,&try->pub_inode);

	goto success;

invalid:
	assert(NULL == *id);
	if (NULL == try) 
		gale_alert(GALE_WARNING,G_("invalid public key"),0);
	else
		_ga_warn_id(G_("\"%\": malformed public key"),try);
	return;

ignore:
	if (_ga_pub_equal(try->pub_data,data)) goto success;
	_ga_warn_id(G_("\"%\": ignoring obsolete public key"),try);
	return;

success:
	if (0 == try->pub_orig.l) try->pub_orig = save;
	*id = try;
	return;
}

static struct gale_data export(struct gale_text name,struct gale_group data) {
	struct gale_data key;
	if (gale_group_null(data)) return null_data;

	key.l = gale_copy_size(sizeof(magic3))
	      + gale_text_size(name)
	      + gale_group_size(data);
	key.p = gale_malloc(key.l);
	key.l = 0;

	gale_pack_copy(&key,magic3,sizeof(magic3));
	gale_pack_text(&key,name);
	gale_pack_group(&key,data);
	return key;
}

void _ga_export_pub(struct auth_id *id,struct gale_data *key,int flag) {
	if (EXPORT_STUB == flag 
	|| (NULL == id->pub_signer && EXPORT_NORMAL == flag)) {
		/* Export a stub key.  Might as well keep it compatible. */
		size_t len = gale_copy_size(sizeof(magic2))
		           + gale_text_size(id->name);
		key->p = gale_malloc(len);
		key->l = 0;
		gale_pack_copy(key,magic2,sizeof(magic2));
		gale_pack_text(key,id->name);
		return;
	}

	if (EXPORT_NORMAL == flag
	|| (EXPORT_TRUSTED == flag && NULL != id->pub_signer)) {
		/* We should already have the key serialized. */
		assert(NULL != id->pub_signer && 0 != id->pub_orig.l);
		*key = id->pub_orig;
		return;
	}

	/* Export an unsigned key. */
	assert(EXPORT_TRUSTED == flag && NULL == id->pub_signer);
	*key = export(id->name,id->pub_data);
}

void _ga_sign_pub(struct auth_id *id,struct gale_time expire) {
	struct auth_id *signer;
	struct gale_fragment frag;
	struct gale_group data;

	if (!gale_text_compare(id->name,G_("ROOT"))) return;

	init_auth_id(&signer,_ga_signer(id->name));
	if (NULL == signer 
	||  !auth_id_private(signer)
	||  !auth_id_public(signer))
		return;

	frag.type = frag_time;
	frag.name = G_("key.signed");
	frag.value.time = gale_time_now();
	gale_group_replace(&id->pub_data,frag);

	frag.name = G_("key.expires");
	frag.value.time = expire;
	gale_group_replace(&id->pub_data,frag);

	data = id->pub_data;
	if (!auth_sign(&data,signer,1)) return;
	id->pub_orig = export(id->name,data);
	id->pub_signer = signer;
}

static int bad_key(struct auth_id *id) {
	R_RSA_PUBLIC_KEY key;
	return (_ga_pub_rsa(id->pub_data,&key)
	&&	768 == key.bits
	&&	0xCA == key.modulus[32]
	&&	0x2B == key.modulus[45]
	&&	0x76 == key.modulus[55]
	&&	0xC7 == key.modulus[67]
	&&	0xE9 == key.modulus[81]
	&&	0x01 == key.modulus[82]);
}

static int expired(struct auth_id *id,struct gale_time now) {
	struct gale_fragment frag;
	return gale_group_lookup(id->pub_data,G_("key.expires"),frag_time,&frag)
	    && gale_time_compare(frag.value.time,now) < 0;
}

int _ga_trust_pub(struct auth_id *id) {
	struct gale_time now;

	if (NULL == id) return 0;
	gale_diprintf(10,2,"(auth) \"%s\": verifying trust of key\n",
	             gale_text_to(gale_global->enc_console,id->name));

	if (gale_group_null(id->pub_data)) {
		gale_diprintf(10,-2,"(auth) \"%s\": no public key to trust!\n",
		             gale_text_to(gale_global->enc_console,id->name));
		return 0;
	}

	if (id->pub_trusted) {
		gale_diprintf(10,-2,"(auth) \"%s\": implicitly trusted\n",
		             gale_text_to(gale_global->enc_console,id->name));
		return 1;
	}

	now = gale_time_now();
#if 0
	while (NULL != id && !gale_group_null(id->pub_data)) {
		gale_dprintf(12,"(auth) checking component \"%s\"\n",
			     gale_text_to(gale_global->enc_console,id->name));
		if (bad_key(id)) {
			_ga_warn_id(G_("\"%\": bad, old, insecure key"),id);
			id = NULL;
		} else if (id->pub_trusted) {
			gale_dprintf(11,"(auth) found trusted parent\n");
			break;
		} else if (expired(id,now)) {
			_ga_warn_id(G_("\"%\": key expired"),id);
			id = NULL;
		} else
			id = id->pub_signer;
	}

	gale_diprintf(10,-2,"(auth) done verifying trust\n");
	return NULL != id && id->pub_trusted;
#else
	if (bad_key(id)) {
		_ga_warn_id(G_("\"%\": bad, old, insecure key"),id);
		gale_diprintf(10,-2,"(auth) no trust\n");
		return 0;
	} else if (expired(id,now)) {
		_ga_warn_id(G_("\"%\": key expired"),id);
		gale_diprintf(10,-2,"(auth) no trust\n");
		return 0;
	} else if (NULL == id->pub_signer) {
		gale_diprintf(10,-2,"(auth) unsigned key, no trust\n");
		return 0;
	}

	if (auth_id_public(id->pub_signer)) {
		gale_diprintf(10,-2,"(auth) \"%s\": everything checks out\n", 
		              gale_text_to(gale_global->enc_console,id->name));
		return 1;
	} else {
		gale_diprintf(10,-2,"(auth) \"%s\": parent not trusted\n", 
		              gale_text_to(gale_global->enc_console,id->name));
		return 0;
	}
#endif
}
