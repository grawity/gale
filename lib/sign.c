#include "common.h"
#include "pack.h"
#include "key.h"
#include "id.h"

#include <assert.h>
#include <string.h>
#include <netinet/in.h>

/* signature format:

   magic
   length (u32)
   signature data
   key doing the signing
*/

static const byte magic[] = { 0x68, 0x13, 0x01, 0x00 };

void _ga_import_sig(struct signature *sig,struct gale_data data) {
	u32 len;
	struct signature ret;

	_ga_init_sig(sig); ret = *sig;

	if (!_ga_unpack_compare(&data,magic,sizeof(magic))
	||  !_ga_unpack_u32(&data,&len) || len > MAX_SIGNATURE_LEN) {
		gale_alert(GALE_WARNING,"invalid signature format",0);
		return;
	}

	ret.sig.p = gale_malloc(ret.sig.l = len);
	if (!_ga_unpack_copy(&data,ret.sig.p,ret.sig.l)) {
		gale_alert(GALE_WARNING,"invalid signature data",0);
		return;
	}

	_ga_import_pub(&ret.id,data,NULL,IMPORT_NORMAL);
	if (ret.id) *sig = ret;
}

void _ga_export_sig(struct signature *sig,struct gale_data *data,int flag) {
	struct gale_data key;
	int len;

	_ga_export_pub(sig->id,&key,flag);
	if (!key.p) {
		data->p = NULL;
		data->l = 0;
		return;
	}

	len = _ga_copy_size(sizeof(magic)) + _ga_u32_size 
	    + _ga_copy_size(sig->sig.l) + _ga_copy_size(key.l);
	data->p = gale_malloc(len);
	data->l = 0;

	_ga_pack_copy(data,magic,sizeof(magic));
	_ga_pack_u32(data,sig->sig.l);
	_ga_pack_copy(data,sig->sig.p,sig->sig.l);
	_ga_pack_copy(data,key.p,key.l);

	gale_free(key.p);
}

void _ga_init_sig(struct signature *sig) {
	sig->id = NULL;
	sig->sig.p = NULL;
	sig->sig.l = 0;
}

void _ga_sign(struct signature *sig,struct auth_id *id,struct gale_data data) {
	R_SIGNATURE_CTX ctx;

	_ga_init_sig(sig);
	if (!auth_id_private(id)) {
		_ga_warn_id(G_("cannot sign without private key \"%\""),id);
		return;
	}

	sig->sig.p = gale_malloc(MAX_SIGNATURE_LEN);
	sig->id = id;

	R_SignInit(&ctx,DA_MD5);
	R_SignUpdate(&ctx,data.p,data.l);
	if (R_SignFinal(&ctx,sig->sig.p,&sig->sig.l,sig->id->private)) {
		_ga_warn_id(G_("\"%\": failure in signature operation"),id);
		_ga_init_sig(sig);
	}
}

int _ga_verify(struct signature *sig,struct gale_data data) {
	R_SIGNATURE_CTX ctx;

	if (!sig->id) {
		gale_dprintf(10,"(auth) cannot verify empty signature\n");
		return 0;
	}

	gale_diprintf(10,2,"(auth) \"%s\": verifying some signed data\n",
	              gale_text_to_local(sig->id->name));

	gale_dprintf(11,"(auth) \"%s\": looking for key to verify sig with\n",
	             gale_text_to_local(sig->id->name));

	assert(sig->id->public);

	R_VerifyInit(&ctx,DA_MD5);
	R_VerifyUpdate(&ctx,data.p,data.l);
	if (R_VerifyFinal(&ctx,sig->sig.p,sig->sig.l,sig->id->public)) {
		_ga_warn_id(G_("\"%\": signature does not verify"),sig->id);
		return 0;
	}

	gale_diprintf(10,-2,"(auth) \"%s\": successful verification\n",
	              gale_text_to_local(sig->id->name));
	return 1;
}

/* Get rid of this as soon as we can! */

void _auth_sign(struct auth_id *id,
                struct gale_data data,struct gale_data *out)
{
	struct signature sig;

	out->p = NULL;
	out->l = 0;

	_ga_sign(&sig,id,data);
	if (!sig.id) return;
	_ga_export_sig(&sig,out,EXPORT_NORMAL);
}

void auth_sign(struct auth_id *id,
               struct gale_data data,struct gale_data *out)
{
	struct signature sig;

	out->p = NULL;
	out->l = 0;

	_ga_sign(&sig,id,data);
	if (!sig.id) return;
	_ga_export_sig(&sig,out,EXPORT_STUB);
}

void auth_verify(struct auth_id **id,
                 struct gale_data data,struct gale_data in)
{
	struct signature sig;
	_ga_import_sig(&sig,in);

	if (!sig.id || !auth_id_public(sig.id)) {
		_ga_warn_id(G_("cannot find key \"%\" to validate signature"),
		            sig.id);
		*id = NULL;
	} else if (_ga_verify(&sig,data)) 
		*id = sig.id;
	else if (sig.id && sig.id->public) {
		R_RSA_PUBLIC_KEY save = *(sig.id->public);
		if (_ga_find_pub(sig.id) 
		&&  memcmp(sig.id->public,&save,sizeof(save))
		&&  _ga_verify(&sig,data))
			*id = sig.id;
		else
			*id = NULL;
	}
}
