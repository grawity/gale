#include "init.h"
#include "file.h"
#include "pack.h"
#include "key.h"
#include "id.h"

#include <stdio.h>
#include <netinet/in.h>
#include <assert.h>
#include <string.h>
#include <sys/time.h>

/* public key format:

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
*/

static const byte magic[] = { 0x68, 0x13, 0x00, 0x00 };
static const byte magic2[] = { 0x68, 0x13, 0x00, 0x02 };

static struct gale_text must_sign(struct gale_text text) {
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

void _ga_import_pub(struct auth_id **id,struct gale_data key,
                    struct inode *source,int trust) {
	struct auth_id *imp = NULL;
	struct gale_data save = key;
	struct signature sig;
	struct gale_text comment = null_text;
	R_RSA_PUBLIC_KEY k;
	struct gale_time sign,expire;
	u32 u,version;
	int valid = 0;

	/* --- find out what we're doing --- */

	*id = NULL;
	_ga_init_sig(&sig);

	if (gale_unpack_compare(&key,magic,sizeof(magic)))
		version = 1;
	else if (gale_unpack_compare(&key,magic2,sizeof(magic2)))
		version = 2;
	else {
		gale_alert(GALE_WARNING,"unknown public key format",0);
		goto error;
	}

	if (version > 1) {
		struct gale_text name;

		if (!gale_unpack_text(&key,&name)) {
			gale_alert(GALE_WARNING,"truncated public key",0);
			goto error;
		}

		init_auth_id(&imp,name);
	} else {
		const char *sz;
		struct gale_text text;

		if (!gale_unpack_str(&key,&sz)) {
			gale_alert(GALE_WARNING,"truncated public key",0);
			goto error;
		}

		text = gale_text_from_latin1(sz,-1);
		init_auth_id(&imp,text);
	}

	/* --- now import the actual key bits and stuff --- */

	gale_diprintf(10,2,"(auth) \"%s\": importing public key\n",
	              gale_text_to_local(imp->name));

	if (key.l == 0) {
		gale_dprintf(11,"(auth) \"%s\": stub key found\n",
			     gale_text_to_local(imp->name));
		goto success;
	}

	if (version > 1) {
		if (!gale_unpack_text(&key,&comment)) {
			_ga_warn_id(G_("\"%\": malformed public key"),imp);
			goto error;
		}
	} else {
		const char *sz;
		if (!gale_unpack_str(&key,&sz)) {
			_ga_warn_id(G_("\"%\": malformed public key"),imp);
			goto error;
		}
		comment = gale_text_from_latin1(sz,-1);
	}

	gale_dprintf(11,"(auth) \"%s\": found comment \"%s\"\n",
		     gale_text_to_local(imp->name),gale_text_to_local(comment));

	memset(&k,0,sizeof(k));

	if (!gale_unpack_u32(&key,&u)
	||  !gale_unpack_rle(&key,k.modulus,MAX_RSA_MODULUS_LEN)
	||  !gale_unpack_rle(&key,k.exponent,MAX_RSA_MODULUS_LEN)) {
		_ga_warn_id(G_("\"%\": malformed public key"),imp);
		goto error;
	}

	k.bits = u;
	if (k.bits < MIN_RSA_MODULUS_BITS || k.bits > MAX_RSA_MODULUS_BITS) {
		_ga_warn_id(G_("\"%\": invalid public key bit size"),imp);
		goto error;
	}

	if (version > 1 && key.l > 0) {
		if (!gale_unpack_time(&key,&sign)
		||  !gale_unpack_time(&key,&expire)) {
			_ga_warn_id(G_("\"%\": malformed v2 public key"),imp);
			goto error;
		}
	} else {
		sign = gale_time_zero();
		expire = gale_time_forever();
	}

	/* --- We've retrieved actual key data; now decide its fate. --- */

	if (imp->trusted && !trust) goto ignore;

	if (!source) {
		auth_hook *hook_save = hook_find_public;
		gale_dprintf(11,"(auth) \"%s\": looking around for it\n",
			     gale_text_to_local(imp->name));
		hook_find_public = NULL;
		valid = auth_id_public(imp);
		hook_find_public = hook_save;
	} else
		valid = _ga_trust_pub(imp);

	if (valid && gale_time_compare(sign,imp->sign_time) <= 0) goto ignore;

	/* Import and validate the signature, if there is one. */

	if (key.l != 0) {
		struct gale_data data;

		data = save;
		data.l = key.p - data.p;

		gale_dprintf(11,"(auth) \"%s\": validating signature\n",
			     gale_text_to_local(imp->name));

		_ga_import_sig(&sig,key);

		if (sig.id && !sig.id->public) auth_id_public(sig.id);
		if (!sig.id || !sig.id->public) {
			_ga_warn_id(G_("\"%\": cannot find signing key"),imp);
			_ga_init_sig(&sig);
		} else if (!_ga_verify(&sig,data))
			_ga_init_sig(&sig);
		else if (gale_text_compare(sig.id->name,must_sign(imp->name))) {
			_ga_warn_id(G_("key \"%\" cannot certify \"%\""),
			            sig.id,imp);
			_ga_init_sig(&sig);
		}
	}

	/* Valid signatures win over invalid ones. */

	if (valid && !_ga_trust_pub(sig.id)) goto ignore;

	/* --- OK, we've accepted the key.  Install it. --- */

	gale_dprintf(11,"(auth) \"%s\": valid key found; installing\n",
		     gale_text_to_local(imp->name));

	if (imp->public && memcmp(imp->public,&k,sizeof(k)))
		gale_alert(GALE_NOTICE,
		           "replacing an old public key with this one",0);

	imp->version = version;
	imp->sign_time = sign;
	imp->expire_time = expire;
	imp->trusted = trust;

	imp->sig = sig;
	_ga_init_sig(&sig);

	imp->comment = comment; comment = null_text;
	if (!imp->public) gale_create(imp->public);
	*imp->public = k;

	_ga_erase_inode(imp->source);

	if (source) 
		imp->source = *source;
	else {
		struct gale_data key;

		gale_dprintf(11,"(auth) \"%s\": storing key in cache\n",
			     gale_text_to_local(imp->name));

		_ga_export_pub(imp,&key,EXPORT_NORMAL);
		if (0 != key.l)
		_ga_save_file(_ga_etc_cache,imp->name,0644,key,&imp->source);
	}

	goto success;

ignore:
	if (!memcmp(imp->public,&k,sizeof(k))) goto success;
	_ga_warn_id(G_("\"%\": ignoring obsolete public key"),imp);
	goto error;

success:
	*id = imp;

error:
	if (imp != NULL)
		gale_diprintf(10,-2,"(auth) \"%s\": done with import\n",
		             gale_text_to_local(imp->name));
}

void _ga_export_pub(struct auth_id *id,struct gale_data *key,int flag) {
	size_t len;
	struct gale_data sig;

	if (id->version == 0) id->version = 2;

	/* Make a stub key, if that's all we can do... */
	if (flag == EXPORT_STUB || (!id->sig.id && flag == EXPORT_NORMAL)) {
		if (id->version > 1) {
			size_t len = gale_copy_size(sizeof(magic2))
			           + gale_text_size(id->name);
			key->p = gale_malloc(len);
			key->l = 0;

			gale_pack_copy(key,magic2,sizeof(magic2));
			gale_pack_text(key,id->name);
		} else {
			char *name = gale_text_to_latin1(id->name);
			size_t len = gale_copy_size(sizeof(magic)) 
			           + gale_str_size(name);
			key->p = gale_malloc(len);
			key->l = 0;

			gale_pack_copy(key,magic,sizeof(magic));
			gale_pack_str(key,name);
			gale_free(name);
		}

		return;
	}

	if (!id->public) {
		key->p = NULL;
		key->l = 0;
		return;
	}

	sig.p = NULL;
	sig.l = 0;
	if (id->sig.id) _ga_export_sig(&id->sig,&sig,EXPORT_NORMAL);

	len = gale_copy_size(sizeof(magic))
	    + gale_u32_size() + 2 * gale_rle_size(MAX_RSA_MODULUS_LEN)
	    + gale_copy_size(sig.l);

	if (id->version > 1)
		len += gale_text_size(id->name) + gale_text_size(id->comment)
		    +  2 * gale_time_size();
	else
		len += id->name.l + 1 + id->comment.l + 1;

	key->p = gale_malloc(len);
	key->l = 0;

	if (id->version > 1) {
		gale_pack_copy(key,magic2,sizeof(magic2));
		gale_pack_text(key,id->name);
		gale_pack_text(key,id->comment);
	} else {
		char *name = gale_text_to_latin1(id->name);
		char *comment = gale_text_to_latin1(id->comment);
		gale_pack_copy(key,magic,sizeof(magic));
		gale_pack_str(key,name);
		gale_pack_str(key,comment);
		gale_free(name);
		gale_free(comment);
	}

	gale_pack_u32(key,id->public->bits);
	gale_pack_rle(key,id->public->modulus,MAX_RSA_MODULUS_LEN);
	gale_pack_rle(key,id->public->exponent,MAX_RSA_MODULUS_LEN);

	if (id->version > 1 && (sig.p || flag == EXPORT_SIGN)) {
		gale_pack_time(key,id->sign_time);
		gale_pack_time(key,id->expire_time);
	}

	if (sig.p && flag != EXPORT_SIGN) {
		gale_pack_copy(key,sig.p,sig.l);
		gale_free(sig.p);
	}
}

void _ga_sign_pub(struct auth_id *id,struct gale_time expire) {
	struct gale_data blob;
	struct auth_id *signer;

	if (!gale_text_compare(id->name,G_("ROOT"))) return;

	init_auth_id(&signer,must_sign(id->name));
	if (!signer 
	||  !auth_id_private(signer)
	||  !auth_id_public(signer))
		return;

	id->sign_time = gale_time_now();
	id->expire_time = expire;

	_ga_init_sig(&id->sig);
	_ga_export_pub(id,&blob,EXPORT_SIGN);
	if (blob.p) {
		_ga_sign(&id->sig,signer,blob);
		gale_free(blob.p);
	}
}

static int bad_key(struct auth_id *id) {
	R_RSA_PUBLIC_KEY *key = id->public;
	if (key->bits != 768
	||  key->modulus[32] != 0xCA
	||  key->modulus[45] != 0x2B
	||  key->modulus[55] != 0x76
	||  key->modulus[67] != 0xC7
	||  key->modulus[81] != 0xE9
	||  key->modulus[82] != 0x01) return 0;
	return 1;
}

int _ga_trust_pub(struct auth_id *id) {
	struct gale_time now;

	if (!id) return 0;

	gale_diprintf(10,2,"(auth) \"%s\": verifying trust of key\n",
	             gale_text_to_local(id->name));

	if (!id->public) {
		gale_diprintf(10,-2,"(auth) \"%s\": no public key to trust!\n",
		             gale_text_to_local(id->name));
		return 0;
	}

	now = gale_time_now();

	while (id && id->public) {
		gale_dprintf(12,"(auth) checking component \"%s\"\n",
			     gale_text_to_local(id->name));
		if (bad_key(id)) {
			_ga_warn_id(G_("\"%\": not trusting old, insecure key"),id);
			id = NULL;
		} else if (id->trusted) {
			gale_dprintf(11,"(auth) found trusted parent\n");
			break;
		} else if (gale_time_compare(id->expire_time,now) < 0) {
			_ga_warn_id(G_("\"%\": certificate expired"),id);
			id = NULL;
		} else
			id = id->sig.id;
	}

	gale_diprintf(10,-2,"(auth) \"%s\" done checking key (%s)\n",
	             id ? gale_text_to_local(id->name) : "(none)",
	             (id && id->trusted) ? "trusted" : "not trusted");
	return id && id->trusted;
}
