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

	char *hdr = sign_data(id,in->data.p,in->data.p + in->data.l);
	int len = strlen(hdr) + 13;
	out = new_message();
	out->cat = gale_text_dup(in->cat);
	out->data.p = gale_malloc(out->data.l = len + in->data.l);
	sprintf(out->data.p,"Signature: %s\r\n",hdr);
	memcpy(out->data.p + len,in->data.p,in->data.l);
	gale_free(hdr);

    } else {

	struct gale_data sig;
	auth_sign(id,in->data,&sig);
	if (sig.p) {
		int len = armor_len(sig.l);
		out = new_message();
		out->cat = gale_text_dup(in->cat);
		out->data.l = 16 + len + in->data.l;
		out->data.p = gale_malloc(out->data.l);
		strcpy(out->data.p,"Signature: g2/");
		armor(sig.p,sig.l,out->data.p + 14);
		strcpy(out->data.p + 14 + len,"\r\n");
		memcpy(out->data.p + 16 + len,in->data.p,in->data.l);
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

	char *cend,*crypt = gale_malloc(in->data.l + ENCRYPTION_PADDING);
	char *hdr;
	int len;

	if (num != 1) {
		gale_alert(GALE_WARNING,"cannot send to multiple ids",0);
		return NULL;
	}

	hdr = encrypt_data(*id,in->data.p,in->data.p + in->data.l,crypt,&cend);
	len = strlen(hdr) + 14;
	out = new_message();
	out->cat = gale_text_dup(in->cat);
	out->data.p = gale_malloc(out->data.l = len + cend - crypt);
	sprintf(out->data.p,"Encryption: %s\r\n",hdr);
	memcpy(out->data.p + len,crypt,cend - crypt);
	gale_free(hdr);

    } else {

	struct gale_data cipher;
	int i;

	for (i = 0; i < num; ++i)
		if (!find_id(id[i])) {
			gale_alert(GALE_WARNING,"cannot encrypt without key",0);
			return NULL;
		}

	auth_encrypt(num,id,in->data,&cipher);

	if (!cipher.p) return NULL;

	out = new_message();
	out->cat = gale_text_dup(in->cat);
	out->data.p = gale_malloc(out->data.l = cipher.l + 16);
	strcpy(out->data.p,"Encryption: g2\r\n");
	memcpy(out->data.p + 16,cipher.p,cipher.l);
	gale_free(cipher.p);

    }

	return out;
}

struct gale_id *verify_message(struct gale_message *in) {
	const char *ptr = in->data.p,*end;
	const char *dptr,*dend = in->data.p + in->data.l;
	struct gale_id *id = NULL;

	for (end = ptr; end < dend && *end != '\r'; ++end) ;
	dptr = end + 1;
	if (dptr < dend && *dptr == '\n') ++dptr;

	if (end == dend) {
		id = NULL;
	} else if (!strncasecmp(ptr,"Signature: RSA/MD5",18)) {
		char *sig = gale_strndup(ptr + 11,end - ptr - 11);
		id = verify_data(sig,dptr,dend);
		gale_free(sig);
	} else if (!strncasecmp(in->data.p,"Signature: g2/",14)) {
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
	const char *ptr = in->data.p,*end;
	const char *dptr,*dend = in->data.p + in->data.l;
	struct gale_id *id = NULL;
	*out = in;

	for (end = ptr; end < dend && *end != '\r'; ++end) ;
	dptr = end + 1;
	if (dptr < dend && *dptr == '\n') ++dptr;

	if (end == dend)
		*out = in;
	else if (!strncasecmp(ptr,"Encryption: RSA/3DES",20)) {
		char *plain,*pend,*hdr = gale_strndup(ptr + 12,end - ptr - 12);

		*out = NULL;
		plain = gale_malloc(dend - end + DECRYPTION_PADDING);
		id = decrypt_data(hdr,dptr,dend,plain,&pend);
		gale_free(hdr);

		if (id) {
			*out = new_message();
			(*out)->cat = gale_text_dup(in->cat);
			(*out)->data.p = plain;
			(*out)->data.l = pend - plain;
		}
	} else if (!strncasecmp(ptr,"Encryption: g2",14)) {
		struct gale_data cipher,plain;

		*out = NULL;
		cipher.p = (byte *) dptr;
		cipher.l = dend - dptr;
		auth_decrypt(&id,cipher,&plain);

		if (id) {
			*out = new_message();
			(*out)->cat = gale_text_dup(in->cat);
			(*out)->data.p = plain.p;
			(*out)->data.l = plain.l;
		}
	}

	if (*out) addref_message(*out);
	return id;
}
