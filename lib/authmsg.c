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
	struct gale_data sig,data;

	data.p = gale_malloc(gale_group_size(in->data) + gale_u32_size());
	data.l = 0;
	gale_pack_u32(&data,0);
	gale_pack_group(&data,in->data);

	if (tweak)
		_auth_sign(id,data,&sig);
	else
		auth_sign(id,data,&sig);

	if (sig.p) {
		struct gale_fragment frag;
		out = new_message();

		frag.name = G_("security/signature");
		frag.type = frag_data;
		frag.value.data.p = gale_malloc_atomic(
			gale_u32_size() * 2 + sig.l + data.l);
		frag.value.data.l = 0;

		gale_pack_u32(&frag.value.data,sig.l);
		gale_pack_copy(&frag.value.data,sig.p,sig.l);
		gale_pack_copy(&frag.value.data,data.p,data.l);

		gale_group_add(&out->data,frag);
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
	struct gale_fragment frag;
	struct gale_data data,cipher;
	int i;

	for (i = 0; i < num; ++i)
		if (!auth_id_public(id[i])) {
			gale_alert(GALE_WARNING,"cannot encrypt without key",0);
			return NULL;
		}

	data.p = gale_malloc(gale_group_size(in->data) + gale_u32_size());
	data.l = 0;
	gale_pack_u32(&data,0);
	gale_pack_group(&data,in->data);
	auth_encrypt(num,id,data,&cipher);

	if (!cipher.p) return NULL;

	out = new_message();
	out->cat = in->cat;
	frag.type = frag_data;
	frag.name = G_("security/encryption");
	frag.value.data = cipher;
	gale_group_add(&out->data,frag);

	return out;
}

struct auth_id *verify_message(struct gale_message *in,
                               struct gale_message **out) 
{
	struct gale_data sig,data;
	struct gale_group group;
	struct gale_fragment frag;
	struct auth_id *id = NULL;
	u32 len,zero;

	*out = in;

	group = gale_group_find(in->data,G_("security/signature"));
	if (gale_group_null(group)) return id;
	frag = gale_group_first(group);
	if (frag.type != frag_data) return id;

	if (!gale_unpack_u32(&frag.value.data,&len) || len > frag.value.data.l) {
		gale_alert(GALE_WARNING,"invalid signature fragment",0);
		return id;
	}

	sig.p = frag.value.data.p;
	sig.l = len;

	frag.value.data.p += len;
	frag.value.data.l -= len;

	data = frag.value.data;
	if (!gale_unpack_u32(&data,&zero) || zero != 0
	||  !gale_unpack_group(&data,&group)) {
		gale_alert(GALE_WARNING,"unknown signature format",0);
		return id;
	}

	auth_verify(&id,frag.value.data,sig);
	*out = new_message();
	(*out)->cat = in->cat;
	(*out)->data = group;

	return id;
}

struct auth_id *decrypt_message(struct gale_message *in,
                                struct gale_message **out) 
{
	struct auth_id *id = NULL;
	struct gale_data plain;
	struct gale_group group;
	struct gale_fragment frag;

	*out = in;

	group = gale_group_find(in->data,G_("security/encryption"));
	if (gale_group_null(group)) return id;
	frag = gale_group_first(group);
	if (frag.type != frag_data) return id;

	*out = NULL;

	auth_decrypt(&id,frag.value.data,&plain);
	if (id) {
		u32 zero;

		if (!gale_unpack_u32(&plain,&zero) || zero != 0
		||  !gale_unpack_group(&plain,&group)) {
			gale_alert(GALE_WARNING,"unknown encryption format",0);
			return NULL;
		}

		*out = new_message();
		(*out)->cat = in->cat;
		(*out)->data = group;
	}

	return id;
}
