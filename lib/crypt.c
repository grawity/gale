#include "common.h"
#include "random.h"
#include "key.h"
#include "id.h"

#include <string.h>
#include <netinet/in.h>

#define MAX_KEYS 128

/* encryption format:

   magic (v1 or v2)
   iv: 8 bytes
   keycount: u32
   ( key name: NUL-terminated (v2: counted Unicode)
     key len: u32
     key data ) * keycount
   encrypted data

*/

static const byte magic[] = { 0x68, 0x13, 0x02, 0x00 };
static const byte magic2[] = { 0x68, 0x13, 0x02, 0x01 };

void _ga_encrypt(int num,struct auth_id **ids,
                 struct gale_data plain,struct gale_data *cipher)
{
	R_ENVELOPE_CTX ctx;
	byte **ekey;
	unsigned int *ekeylen;
	R_RSA_PUBLIC_KEY **key;
	R_RANDOM_STRUCT *r = _ga_rrand();
	byte iv[8];
	struct gale_data tmp;
	unsigned int u;
	u32 n;
	int i;

	gale_create_array(ekey,num);
	gale_create_array(ekeylen,num);
	gale_create_array(key,num);

	tmp.p = NULL;
	tmp.l = 0;
	*cipher = tmp;

	for (i = 0; i < num; ++i) ekey[i] = NULL;

	for (i = 0; i < num; ++i) {
		gale_create(key[i]);
		if (!auth_id_public(ids[i])
		||  !_ga_pub_rsa(ids[i]->pub_data,key[i])) {
			_ga_warn_id(G_("\"%\": no public key, cannot encrypt"),
			            ids[i]);
			return;
		}
		ekey[i] = gale_malloc(MAX_ENCRYPTED_KEY_LEN);
	}

	if (R_SealInit(&ctx,ekey,ekeylen,iv,num,key,EA_DES_EDE3_CBC,r)) {
		gale_alert(GALE_WARNING,"failure in encryption operation",0);
		return;
	}

	n = gale_copy_size(sizeof(magic2))
	  + gale_copy_size(sizeof(iv)) + gale_u32_size() + plain.l + 8;
	for (i = 0; i < num; ++i) {
		n += gale_text_size(ids[i]->name)
		  +  ekeylen[i] + gale_u32_size();
	}
	tmp.p = gale_malloc(n);
	tmp.l = 0;

	gale_pack_copy(&tmp,magic2,sizeof(magic2));
	gale_pack_copy(&tmp,iv,sizeof(iv));
	gale_pack_u32(&tmp,num);

	for (i = 0; i < num; ++i) {
		gale_pack_text(&tmp,ids[i]->name);
		gale_pack_u32(&tmp,ekeylen[i]);
		gale_pack_copy(&tmp,ekey[i],ekeylen[i]);
	}

	R_SealUpdate(&ctx,tmp.p + tmp.l,&u,plain.p,plain.l); tmp.l += u;
	R_SealFinal(&ctx,tmp.p + tmp.l,&u); tmp.l += u;

	*cipher = tmp;
	tmp.p = NULL;
}

void _ga_decrypt(struct auth_id **id,
                 struct gale_data cipher,struct gale_data *plain)
{
	R_ENVELOPE_CTX ctx;
	R_RSA_PRIVATE_KEY priv;
	unsigned int u,ekeylen = 0;
	byte iv[8],ekey[MAX_ENCRYPTED_KEY_LEN];
	u32 num,i,version;
	struct auth_id *tmp = NULL;

	*id = NULL;
	plain->p = NULL;
	plain->l = 0;

	if (gale_unpack_compare(&cipher,magic,sizeof(magic)))
		version = 1;
	else if (gale_unpack_compare(&cipher,magic2,sizeof(magic2)))
		version = 2;
	else {
		gale_alert(GALE_WARNING,"unrecognized encryption format",0);
		return;
	}

	if (!gale_unpack_copy(&cipher,iv,sizeof(iv))
	||  !gale_unpack_u32(&cipher,&num)) {
		gale_alert(GALE_WARNING,"invalid encryption format",0);
		return;
	}

	for (i = 0; i < num; ++i) {
		struct gale_text name;
		int flag = 0;
		u32 n;

		if (version > 1) {
			if (!gale_unpack_text(&cipher,&name)) {
				gale_alert(GALE_WARNING,"malformed crypto",0);
				return;
			}
		} else {
			const char *sz;
			if (!gale_unpack_str(&cipher,&sz))
				gale_alert(GALE_WARNING,"malformed crypto",0);
			name = gale_text_from_latin1(sz,-1);
		}

		if (!tmp) {
			init_auth_id(&tmp,name);
			if (auth_id_private(tmp)) 
				flag = 1;
			else
				tmp = NULL;
		}

		if (!gale_unpack_u32(&cipher,&n)) {
			gale_alert(GALE_WARNING,"truncated encryption",0);
			return;
		}

		if (n > MAX_ENCRYPTED_KEY_LEN || cipher.l < n) {
			gale_alert(GALE_WARNING,"invalid encryption data",0);
			return;
		}

		if (flag) {
			ekeylen = n;
			gale_unpack_copy(&cipher,ekey,n);
		} else {
			cipher.l -= n;
			cipher.p += n;
		}
	}

	if (!tmp) return;

	_ga_priv_rsa(tmp->priv_data,&priv);
	if (R_OpenInit(&ctx,EA_DES_EDE3_CBC,ekey,ekeylen,iv,&priv)) {
		_ga_warn_id(G_("failure decrypting message to \"%\""),tmp);
		return;
	}

	plain->p = gale_malloc(cipher.l + 8);
	plain->l = 0;

	R_OpenUpdate(&ctx,plain->p,&u,cipher.p,cipher.l); plain->l += u;
	R_OpenFinal(&ctx,plain->p + plain->l,&u); plain->l += u;

	*id = tmp;
}
