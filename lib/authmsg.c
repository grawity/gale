#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "gale/all.h"
#include "auth.h"

struct gale_message *sign_message(const char *id,struct gale_message *in) {
	char *hdr = sign_data(id,in->data,in->data + in->data_size);
	int len = strlen(hdr) + 13;
	struct gale_message *out = new_message();
	out->category = gale_strdup(in->category);
	out->data = gale_malloc(out->data_size = len + in->data_size);
	sprintf(out->data,"Signature: %s\r\n",hdr);
	memcpy(out->data + len,in->data,in->data_size);
	gale_free(hdr);
	return out;
}

struct gale_message *encrypt_message(const char *id,struct gale_message *in) {
	char *cend,*crypt = gale_malloc(in->data_size + ENCRYPTION_PADDING);
	char *hdr = encrypt_data(id,in->data,in->data + in->data_size,crypt,&cend);
	int len = strlen(hdr) + 14;
	struct gale_message *out = new_message();
	out->data = gale_malloc(out->data_size = len + cend - crypt);
	sprintf(out->data,"Encryption: %s\r\n",hdr);
	memcpy(out->data + len,crypt,cend - crypt);
	gale_free(hdr);
	return out;
}
