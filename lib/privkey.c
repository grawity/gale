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

static int unpack_fragment(
	struct gale_data *d,struct gale_group *g,
	struct gale_text name,u32 len) 
{
	struct gale_fragment frag;
	frag.type = frag_data;
	frag.name = name;
	frag.value.data.p = gale_malloc(len);
	frag.value.data.l = len;
	if (!gale_unpack_rle(d,frag.value.data.p,len)) return 0;
	gale_group_add(g,frag);
	return 1;
}

int _ga_priv_rsa(struct gale_group group,R_RSA_PRIVATE_KEY *rsa) {
	struct gale_fragment bits,mod,exp,privexp,prime,primeexp,coeff;

	memset(rsa,0,sizeof(*rsa));
	if (!gale_group_lookup(group,G_("rsa.bits"),frag_number,&bits)
	||  !gale_group_lookup(group,G_("rsa.modulus"),frag_data,&mod)
	||  MAX_RSA_MODULUS_LEN != mod.value.data.l
	||  !gale_group_lookup(group,G_("rsa.exponent"),frag_data,&exp)
	||  MAX_RSA_MODULUS_LEN != exp.value.data.l
	||  !gale_group_lookup(group,G_("rsa.private.exponent"),frag_data,&privexp)
	||  MAX_RSA_MODULUS_LEN != privexp.value.data.l
	||  !gale_group_lookup(group,G_("rsa.private.prime"),frag_data,&prime)
	||  2*MAX_RSA_PRIME_LEN != prime.value.data.l
	||  !gale_group_lookup(group,G_("rsa.private.prime.exponent"),frag_data,&primeexp)
	||  2*MAX_RSA_PRIME_LEN != primeexp.value.data.l
	||  !gale_group_lookup(group,G_("rsa.private.coefficient"),frag_data,&coeff)
	||  MAX_RSA_PRIME_LEN != coeff.value.data.l)
		return 0;

	rsa->bits = bits.value.number;
	memcpy(rsa->modulus,mod.value.data.p,mod.value.data.l);
	memcpy(rsa->publicExponent,exp.value.data.p,exp.value.data.l);
	memcpy(rsa->exponent,privexp.value.data.p,privexp.value.data.l);
	memcpy(rsa->prime,prime.value.data.p,prime.value.data.l);
	memcpy(rsa->primeExponent,primeexp.value.data.p,primeexp.value.data.l);
	memcpy(rsa->coefficient,coeff.value.data.p,coeff.value.data.l);
	return 1;
}

void _ga_import_priv(struct auth_id **id,struct gale_data key,struct inode *in)
{
	const size_t mlen = MAX_RSA_MODULUS_LEN;
	const size_t plen = MAX_RSA_PRIME_LEN;

	struct auth_id *try = NULL;
	struct gale_group data = gale_group_empty();
	struct gale_fragment frag;

	*id = NULL;

	if (gale_unpack_compare(&key,magic,sizeof(magic))) {
		const char *sz;
		if (!gale_unpack_str(&key,&sz)) {
			gale_alert(GALE_WARNING,"invalid private key format",0);
			return;
		}
		init_auth_id(&try,gale_text_from_latin1(sz,-1));
	} else if (gale_unpack_compare(&key,magic2,sizeof(magic2))) {
		struct gale_text text;
		if (!gale_unpack_text(&key,&text)) {
			gale_alert(GALE_WARNING,"invalid private key format",0);
			return;
		}
		init_auth_id(&try,text);
	} else {
		gale_alert(GALE_WARNING,"unknown private key format",0);
		return;
	}

	frag.name = G_("rsa.bits");
	frag.type = frag_number;
	if (!gale_unpack_u32(&key,&frag.value.number)
	||  !unpack_fragment(&key,&data,G_("rsa.modulus"),mlen)
	||  !unpack_fragment(&key,&data,G_("rsa.exponent"),mlen)
	||  !unpack_fragment(&key,&data,G_("rsa.private.exponent"),mlen)
	||  !unpack_fragment(&key,&data,G_("rsa.private.prime"),2 * plen)
	||  !unpack_fragment(&key,&data,G_("rsa.private.prime.exponent"),2*plen)
	||  !unpack_fragment(&key,&data,G_("rsa.private.coefficient"),plen)
	||  0 != key.l) {
		_ga_warn_id(G_("\"%\": malformed private key"),try);
		return;
	}

	gale_group_add(&data,frag);
	if (frag.value.number < MIN_RSA_MODULUS_BITS 
	||  frag.value.number > MAX_RSA_MODULUS_BITS) {
		_ga_warn_id(G_("\"%\": bad private key size"),try);
		return;
	}

	try->priv_data = data;
	try->priv_inode = in ? *in : _ga_init_inode();
	*id = try;
}

void _ga_export_priv(struct auth_id *id,struct gale_data *key) {
	R_RSA_PRIVATE_KEY priv;

	if (!_ga_priv_rsa(id->priv_data,&priv)) {
		key->p = NULL;
		key->l = 0;
		return;
	}

	key->l = gale_copy_size(sizeof(magic2)) 
	       + gale_text_size(id->name) + gale_u32_size()
	       + gale_rle_size(MAX_RSA_MODULUS_LEN) * 3
	       + gale_rle_size(MAX_RSA_PRIME_LEN) * 5;
	key->p = gale_malloc(key->l);
	key->l = 0;

	gale_pack_copy(key,magic2,sizeof(magic2));
	gale_pack_text(key,id->name);
	gale_pack_u32(key,priv.bits);
	gale_pack_rle(key,priv.modulus,MAX_RSA_MODULUS_LEN);
	gale_pack_rle(key,priv.publicExponent,MAX_RSA_MODULUS_LEN);
	gale_pack_rle(key,priv.exponent,MAX_RSA_MODULUS_LEN);
	gale_pack_rle(key,priv.prime,MAX_RSA_PRIME_LEN * 2);
	gale_pack_rle(key,priv.primeExponent,MAX_RSA_PRIME_LEN * 2);
	gale_pack_rle(key,priv.coefficient,MAX_RSA_PRIME_LEN);
}
