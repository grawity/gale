/* auth.h -- old style authentication */

#ifndef GALE_AUTH_H
#define GALE_AUTH_H

#include "message.h"
#include "id.h"

/* generate our key pair, if we don't have one */
void gale_keys(void);

/* check a digital signature.  "sig" is the NUL-terminated signature text,
   "data" and "end" mark the data to check.  Returns NULL if unsuccessful,
   otherwise the (new) gale_id (see id.h) for the sender (free it yourself). */
struct gale_id *verify_data(const char *sig,const char *data,const char *end);

/* The amount you should add when decrypting or encrypting to make sure you
   have enough buffer space for the result. */
#define ENCRYPTION_PADDING 8
#define DECRYPTION_PADDING 8

/* decrypt encrypted data.  Returns a new gale_id for the recipient (some id
   for which you have the private key).  "data" and "end" mark the input,
   "header" the encryption header, "out" the buffer to output to; at the end,
   "oend" will point to the end of decrypted data.  If unsuccessful, returns
   NULL. */
struct gale_id *decrypt_data(char *header,const char *data,const char *end,
                             char *out,char **oend);

/* Sign a message with the given ID.  Returns the signed message, NULL if
   unsuccessful. */
struct gale_message *sign_message(struct gale_id *id,struct gale_message *);
/* Encrypt a message to the given ID.  Returns the encrypted message, NULL
   if unsuccessful. */
struct gale_message *encrypt_message(struct gale_id *id,struct gale_message *);

#endif
