#include "pack.h"
#include "key.h"
#include "id.h"

#include <assert.h>
#include <string.h>

/* private key: 

   magic: 0x6813 0x0001 (v2: 0x6813 0x0003)
   id: NUL-terminated (v2: counted Unicode)
   stuff... (see below)
*/

static const byte magic[] = { 0x68, 0x13, 0x00, 0x01 };
static const byte magic2[] = { 0x68, 0x13, 0x00, 0x03 };

void _ga_import_priv(struct auth_id **id,struct gale_data key) {
	u32 u32;
	int version;
	struct gale_text text;
	struct auth_id *imp = NULL;
	R_RSA_PRIVATE_KEY *priv;

	*id = NULL;

	if (gale_unpack_compare(&key,magic,sizeof(magic))) {
		const char *sz;
		version = 1;
		if (!gale_unpack_str(&key,&sz)) {
			gale_alert(GALE_WARNING,"invalid private key format",0);
			return;
		}
		text = gale_text_from_latin1(sz,-1);
	} else if (gale_unpack_compare(&key,magic2,sizeof(magic2))) {
		version = 2;
		if (!gale_unpack_text(&key,&text)) {
			gale_alert(GALE_WARNING,"invalid private key format",0);
			return;
		}
	} else {
		gale_alert(GALE_WARNING,"unknown private key format",0);
		return;
	}

	init_auth_id(&imp,text);
	if (0 == imp->version) imp->version = version;

	imp->private = gale_create(priv);
	memset(priv,0,sizeof(R_RSA_PRIVATE_KEY));

	if (!gale_unpack_u32(&key,&u32)
	||  !gale_unpack_rle(&key,priv->modulus,MAX_RSA_MODULUS_LEN)
	||  !gale_unpack_rle(&key,priv->publicExponent,MAX_RSA_MODULUS_LEN)
	||  !gale_unpack_rle(&key,priv->exponent,MAX_RSA_MODULUS_LEN)
	||  !gale_unpack_rle(&key,priv->prime,MAX_RSA_PRIME_LEN * 2)
	||  !gale_unpack_rle(&key,priv->primeExponent,MAX_RSA_PRIME_LEN * 2)
	||  !gale_unpack_rle(&key,priv->coefficient,MAX_RSA_PRIME_LEN)) {
		_ga_warn_id(G_("\"%\": bad private key length"),imp);
		return;
	}

	priv->bits = u32;
	if (priv->bits < MIN_RSA_MODULUS_BITS 
	||  priv->bits > MAX_RSA_MODULUS_BITS) {
		_ga_warn_id(G_("\"%\": bad private key size"),imp);
		return;
	}

	assert(key.l == 0);
	*id = imp;
}

void _ga_export_priv(struct auth_id *id,struct gale_data *key) {
	int len;
	char *sz = NULL;

	if (!id->private) {
		key->p = NULL;
		key->l = 0;
		return;
	}

	if (0 == id->version) id->version = 2;

	if (id->version > 1) {
		len = gale_copy_size(sizeof(magic2)) 
		    + gale_text_size(id->name);
	} else {
		sz = gale_text_to_latin1(id->name);
		len = gale_copy_size(sizeof(magic)) + gale_str_size(sz);
	}

	len += gale_u32_size()
	    +  gale_rle_size(MAX_RSA_MODULUS_LEN) * 3
	    +  gale_rle_size(MAX_RSA_PRIME_LEN) * 5;
	key->p = gale_malloc(len);
	key->l = 0;

	if (id->version > 1) {
		gale_pack_copy(key,magic2,sizeof(magic2));
		gale_pack_text(key,id->name);
	} else {
		gale_pack_copy(key,magic,sizeof(magic));
		gale_pack_str(key,sz);
	}

	gale_pack_u32(key,id->private->bits);
	gale_pack_rle(key,id->private->modulus,MAX_RSA_MODULUS_LEN);
	gale_pack_rle(key,id->private->publicExponent,MAX_RSA_MODULUS_LEN);
	gale_pack_rle(key,id->private->exponent,MAX_RSA_MODULUS_LEN);
	gale_pack_rle(key,id->private->prime,MAX_RSA_PRIME_LEN * 2);
	gale_pack_rle(key,id->private->primeExponent,MAX_RSA_PRIME_LEN * 2);
	gale_pack_rle(key,id->private->coefficient,MAX_RSA_PRIME_LEN);

	if (sz) gale_free(sz);
}
