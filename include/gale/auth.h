#ifndef AUTH_H
#define AUTH_H

#include "message.h"

void gale_keys(void);

char *sign_data(const char *id,const char *data,const char *end);
char *verify_data(const char *sig,const char *data,const char *end);

#define ENCRYPTION_PADDING 8
#define DECRYPTION_PADDING 8

char *encrypt_data(const char *id,const char *data,const char *dend,
                   char *out,char **oend);
char *decrypt_data(char *header,const char *data,const char *end,
                   char *out,char **oend);

void sign_message(const char *id,struct gale_message *);
void encrypt_message(const char *id,struct gale_message *);

#endif
