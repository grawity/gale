#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "gale/all.h"

void sign_message(const char *id,struct gale_message *msg) {
	char *hdr = sign_data(id,msg->data,msg->data + msg->data_size);
	int len = strlen(hdr) + 13;
	char *tmp = gale_malloc(len + msg->data_size);
	sprintf(tmp,"Signature: %s\r\n",hdr);
	memcpy(tmp + len,msg->data,msg->data_size);
	gale_free(hdr); gale_free(msg->data);
	msg->data = tmp;
	msg->data_size += len;
}

void encrypt_message(const char *id,struct gale_message *msg) {
	char *cend,*crypt = gale_malloc(msg->data_size + ENCRYPTION_PADDING);
	char *hdr = encrypt_data(id,msg->data,msg->data + msg->data_size,
	                         crypt,&cend);
	int len = strlen(hdr) + 14;
	char *tmp = gale_malloc(len + cend - crypt);
	sprintf(tmp,"Encryption: %s\r\n",hdr);
	memcpy(tmp + len,crypt,cend - crypt);
	gale_free(msg->data); gale_free(hdr);
	msg->data = tmp;
	msg->data_size = len + cend - crypt;
}
