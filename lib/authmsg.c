#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "gale/all.h"
#include "auth.h"

static int old_auth(void) {
	if (getenv("GALE_NEW_AUTH")) return 0;
	if (getenv("GALE_OLD_AUTH")) return 0;
	return time(NULL) <= 876380400;
}

void gale_keys(void) {
	if (old_auth()) old_gale_keys();
	if (!auth_id_public(user_id) || !auth_id_private(user_id)) 
		auth_id_gen(user_id,getenv("GALE_FROM"));
}

struct gale_message *sign_message(struct gale_id *id,struct gale_message *in) {
	struct gale_message *out = NULL;

    if (old_auth()) {

	char *hdr = sign_data(id,in->data,in->data + in->data_size);
	int len = strlen(hdr) + 13;
	out = new_message();
	out->category = gale_strdup(in->category);
	out->data = gale_malloc(out->data_size = len + in->data_size);
	sprintf(out->data,"Signature: %s\r\n",hdr);
	memcpy(out->data + len,in->data,in->data_size);
	gale_free(hdr);

    } else {

	struct gale_data data,sig;
	data.p = in->data;
	data.l = in->data_size;
	auth_sign(id,data,&sig);
	if (sig.p) {
		int len = armor_len(sig.l);
		out = new_message();
		out->category = gale_strdup(in->category);
		out->data_size = 16 + len + in->data_size;
		out->data = gale_malloc(out->data_size);
		strcpy(out->data,"Signature: g2/");
		armor(sig.p,sig.l,out->data + 14);
		strcpy(out->data + 14 + len,"\r\n");
		memcpy(out->data + 16 + len,in->data,in->data_size);
		gale_free(sig.p);
	}

	if (!out) addref_message(out = in);

    }

	return out;
}

struct gale_message *encrypt_message(int num,struct gale_id **id,
                                     struct gale_message *in) 
{
	struct gale_message *out = NULL;

    if (old_auth()) {

	char *cend,*crypt = gale_malloc(in->data_size + ENCRYPTION_PADDING);
	char *hdr;
	int len;

	if (num != 1) {
		gale_alert(GALE_WARNING,"cannot send to multiple ids",0);
		return NULL;
	}

	hdr = encrypt_data(*id,in->data,in->data + in->data_size,crypt,&cend);
	len = strlen(hdr) + 14;
	out = new_message();
	out->category = gale_strdup(in->category);
	out->data = gale_malloc(out->data_size = len + cend - crypt);
	sprintf(out->data,"Encryption: %s\r\n",hdr);
	memcpy(out->data + len,crypt,cend - crypt);
	gale_free(hdr);

    } else {

	struct gale_data plain,cipher;
	int i;

	for (i = 0; i < num; ++i)
		if (!find_id(id[i])) {
			gale_alert(GALE_WARNING,"cannot encrypt without key",0);
			return NULL;
		}

	plain.p = in->data;
	plain.l = in->data_size;
	auth_encrypt(num,id,plain,&cipher);

	if (!cipher.p) return NULL;

	out = new_message();
	out->category = gale_strdup(in->category);
	out->data = gale_malloc(out->data_size = cipher.l + 16);
	strcpy(out->data,"Encryption: g2\r\n");
	memcpy(out->data + 16,cipher.p,cipher.l);
	gale_free(cipher.p);

    }

	return out;
}

struct gale_id *verify_message(struct gale_message *in) {
	const char *ptr = in->data,*end;
	const char *dptr,*dend = in->data + in->data_size;
	struct gale_id *id = NULL;

	for (end = ptr; end < dend && *end != '\r'; ++end) ;
	dptr = end + 1;
	if (dptr < dend && *dptr == '\n') ++dptr;

	if (end == dend) {
		gale_alert(GALE_WARNING,"invalid header format",0);
	} else if (!strncasecmp(ptr,"Signature: RSA/MD5",18)) {
		char *sig = gale_strndup(ptr + 11,end - ptr - 11);
		id = verify_data(sig,dptr,dend);
		gale_free(sig);
	} else if (!strncasecmp(in->data,"Signature: g2/",14)) {
		struct gale_data data,sig;

		ptr += 14;
		sig.l = dearmor_len(end - ptr);
		sig.p = gale_malloc(sig.l);
		dearmor(ptr,end - ptr,sig.p);

		data.l = dend - dptr;
		data.p = (byte *) dptr;

		auth_verify(&id,data,sig);
		gale_free(sig.p);
	}

	return id;
}

struct gale_id *decrypt_message(struct gale_message *in,
                                struct gale_message **out) 
{
	const char *ptr = in->data,*end;
	const char *dptr,*dend = in->data + in->data_size;
	struct gale_id *id = NULL;
	*out = in;

	for (end = ptr; end < dend && *end != '\r'; ++end) ;
	dptr = end + 1;
	if (dptr < dend && *dptr == '\n') ++dptr;

	if (!strncasecmp(ptr,"Encryption: RSA/3DES",20)) {
		char *plain,*pend,*hdr = gale_strndup(ptr + 12,end - ptr - 12);

		*out = NULL;
		plain = gale_malloc(dend - end + DECRYPTION_PADDING);
		id = decrypt_data(hdr,dptr,dend,plain,&pend);
		gale_free(hdr);

		if (id) {
			*out = new_message();
			(*out)->category = gale_strdup(in->category);
			(*out)->data = plain;
			(*out)->data_size = pend - plain;
		}
	} else if (!strncasecmp(ptr,"Encryption: g2",14)) {
		struct gale_data cipher,plain;

		*out = NULL;
		cipher.p = (byte *) dptr;
		cipher.l = dend - dptr;
		auth_decrypt(&id,cipher,&plain);

		if (id) {
			*out = new_message();
			(*out)->category = gale_strdup(in->category);
			(*out)->data = plain.p;
			(*out)->data_size = plain.l;
		}
	}

	if (*out) addref_message(*out);
	return id;
}
