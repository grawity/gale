#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "gale/all.h"

static struct gale_message *sign(struct auth_id *id,struct gale_message *in,
                                 int tweak) 
{
	struct gale_message *out = NULL;
	struct gale_data sig;

	if (tweak)
		_auth_sign(id,in->data,&sig);
	else
		auth_sign(id,in->data,&sig);

	if (sig.p) {
		struct gale_fragment frag,*array[2];
		out = new_message();

		frag.name = G_("security/signature");
		frag.type = frag_data;
		frag.value.data.p = gale_malloc_atomic(
			gale_u32_size() + sig.l + in->data.l);
		frag.value.data.l = 0;

		gale_pack_u32(&frag.value.data,sig.l);
		gale_pack_copy(&frag.value.data,sig.p,sig.l);
		gale_pack_copy(&frag.value.data,in->data.p,in->data.l);

		array[0] = &frag;
		array[1] = NULL;
		out->data = pack_message(array);
		out->cat = in->cat;
	}

	return out;
}

struct gale_message *sign_message(struct auth_id *id,struct gale_message *in) {
	return sign(id,in,0);
}

struct gale_message *_sign_message(struct auth_id *id,struct gale_message *in) {
	return sign(id,in,1);
}

struct gale_message *encrypt_message(int num,struct auth_id **id,
                                     struct gale_message *in) 
{
	struct gale_message *out = NULL;
	struct gale_fragment frag,*array[2];
	struct gale_data cipher;
	int i;

	for (i = 0; i < num; ++i)
		if (!auth_id_public(id[i])) {
			gale_alert(GALE_WARNING,"cannot encrypt without key",0);
			return NULL;
		}

	auth_encrypt(num,id,in->data,&cipher);

	if (!cipher.p) return NULL;

	out = new_message();
	out->cat = in->cat;
	frag.type = frag_data;
	frag.name = G_("security/encryption");
	frag.value.data = cipher;
	array[0] = &frag;
	array[1] = NULL;
	out->data = pack_message(array);

	return out;
}

struct auth_id *verify_message(struct gale_message *in,
                               struct gale_message **out) 
{
	struct gale_data data,sig;
	struct auth_id *id = NULL;
	struct gale_fragment **frags = unpack_message(in->data);
	u32 len;

	*out = in;

	if (!frags[0] || frags[0]->type != frag_data
	||  gale_text_compare(frags[0]->name,G_("security/signature")))
		return id;

	data = frags[0]->value.data;
	if (!gale_unpack_u32(&data,&len) || len >= data.l) {
		gale_alert(GALE_WARNING,"invalid signature fragment",0);
		return id;
	}

	sig.p = data.p;
	sig.l = len;

	data.p += len;
	data.l -= len;

	auth_verify(&id,data,sig);
	*out = new_message();
	(*out)->cat = in->cat;
	(*out)->data = data;

	return id;
}

struct auth_id *decrypt_message(struct gale_message *in,
                                struct gale_message **out) 
{
	struct auth_id *id = NULL;
	struct gale_data plain;

	struct gale_fragment **frags = unpack_message(in->data);
	if (!frags[0] || frags[0]->type != frag_data
	||  gale_text_compare(frags[0]->name,G_("security/encryption"))) {
		*out = in;
		return id;
	}

	*out = NULL;
	auth_decrypt(&id,frags[0]->value.data,&plain);
	if (id) {
		*out = new_message();
		(*out)->cat = in->cat;
		(*out)->data = plain;
	}

	return id;
}
