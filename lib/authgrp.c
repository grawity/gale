#include "common.h"
#include "pack.h"
#include "key.h"
#include "id.h"

#include <assert.h>
#include <string.h>
#include <netinet/in.h>

/* returns nonzero if ok */
int auth_sign(struct gale_group *grp,struct auth_id *with,int sigflag) {
	struct signature sig;
	struct gale_data data,sigdata;
	struct gale_fragment frag;

	data.l = 0;
	data.p = gale_malloc(gale_group_size(*grp) + gale_u32_size());
	gale_pack_u32(&data,0);
	gale_pack_group(&data,*grp);

	_ga_init_sig(&sig);
	_ga_sign(&sig,with,data);
	if (NULL == sig.id) return 0;
	_ga_export_sig(&sig,&sigdata,sigflag ? EXPORT_NORMAL : EXPORT_STUB);

	frag.name = G_("security/signature");
	frag.type = frag_data;
	frag.value.data.l = 0;
	frag.value.data.p = gale_malloc_atomic(
		  gale_u32_size()
		+ sigdata.l + data.l); 

	gale_pack_u32(&frag.value.data,sigdata.l);
	gale_pack_copy(&frag.value.data,sigdata.p,sigdata.l);
	gale_pack_copy(&frag.value.data,data.p,data.l);

	*grp = gale_group_empty();
	gale_group_add(grp,frag);
	return 1;
}

struct auth_id *auth_verify(struct gale_group *grp) {
        struct gale_data data;
        struct gale_group group;
        struct gale_fragment frag;
	struct signature sig;
	struct auth_id *id;
        u32 len,zero;

        group = gale_group_find(*grp,G_("security/signature"));
        if (gale_group_null(group)) return NULL;
        frag = gale_group_first(group);
        if (frag.type != frag_data) return NULL;

	if (!gale_unpack_u32(&frag.value.data,&len) 
	||  len > frag.value.data.l) {
		gale_alert(GALE_WARNING,G_("invalid signature format"),0);
		return NULL;
	}

	data.p = frag.value.data.p + len;
	data.l = frag.value.data.l - len;
	frag.value.data.l = len;
	_ga_import_sig(&sig,frag.value.data);

	if (!sig.id) {
		gale_alert(GALE_WARNING,G_("invalid signature"),0);
		id = NULL;
	} else if (!auth_id_public(sig.id)) {
		_ga_warn_id(G_("cannot find key \"%\" to validate"),sig.id);
		id = NULL;
        } else if (_ga_verify(&sig,data)) {
		/* success! */
                id = sig.id;
        } else {
		/* try to get a new version ... */
		struct gale_group save = sig.id->pub_data;
                if (_ga_find_pub(sig.id)
		&& !_ga_pub_equal(sig.id->pub_data,save)
                &&  _ga_verify(&sig,data))
                        id = sig.id;
                else
			id = NULL;
        }

        if (!gale_unpack_u32(&data,&zero) || zero != 0
        ||  !gale_unpack_group(&data,&group)) {
                gale_alert(GALE_WARNING,G_("invalid signature payload"),0);
                id = NULL;
	} else
		*grp = group;

        return id;
}

int auth_encrypt(struct gale_group *grp,int num,struct auth_id **id) {
	struct gale_fragment frag;
	struct gale_data data,cipher;
	int i;

	for (i = 0; i < num; ++i)
		if (!auth_id_public(id[i])) {
			_ga_warn_id(G_("\"%\": no key, no encrypt"),id[i]);
			return 0;
		}

	data.p = gale_malloc(gale_group_size(*grp) + gale_u32_size());
	data.l = 0;
	gale_pack_u32(&data,0); /* some damn reason */
	gale_pack_group(&data,*grp);

	_ga_encrypt(num,id,data,&cipher);
	if (!cipher.p) return 0;

	*grp = gale_group_empty();
	frag.type = frag_data;
	frag.name = G_("security/encryption");
	frag.value.data = cipher;
	gale_group_add(grp,frag);
	return 1;
}

struct auth_id *auth_decrypt(struct gale_group *grp) {
	struct auth_id *id = NULL;
	struct gale_data plain;
	struct gale_group group;
	struct gale_fragment frag;

	group = gale_group_find(*grp,G_("security/encryption"));
	if (gale_group_null(group)) return NULL;
	frag = gale_group_first(group);
	if (frag.type != frag_data) return NULL;

	_ga_decrypt(&id,frag.value.data,&plain);
	if (id) {
		u32 zero;

		if (!gale_unpack_u32(&plain,&zero) || zero != 0
		||  !gale_unpack_group(&plain,&group)) {
			gale_alert(GALE_WARNING,G_("unknown encryption"),0);
			return NULL;
		}

		*grp = group;
	}

	return id;
}
