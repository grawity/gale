#ifndef AUTH_H
#define AUTH_H

void gale_id(const char **);
void gale_domain(const char **);
void gale_user(const char **);

void gale_keys(void);

char *sign_message(const char *data,const char *end);
char *verify_signature(const char *sig,const char *data,const char *end);

#define ENCRYPTION_PADDING 8
#define DECRYPTION_PADDING 8

char *encrypt_message(char *id,const char *data,const char *dend,
                      char *out,char **oend);
char *decrypt_message(char *header,const char *data,const char *end,
                      char *out,char **oend);

#endif
