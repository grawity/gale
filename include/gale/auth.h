#ifndef GALE_AUTH_H
#define GALE_AUTH_H

#include "message.h"

void gale_keys(void);

char *verify_data(const char *sig,const char *data,const char *end);

#define ENCRYPTION_PADDING 8
#define DECRYPTION_PADDING 8

char *decrypt_data(char *header,const char *data,const char *end,
                   char *out,char **oend);

struct gale_message *sign_message(const char *id,struct gale_message *);
struct gale_message *encrypt_message(const char *id,struct gale_message *);

#endif
