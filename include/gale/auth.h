/* auth.h -- old style authentication */

#ifndef GALE_AUTH_H
#define GALE_AUTH_H

#include "message.h"
#include "id.h"

/* generate our key pair, if we don't have one */
void gale_keys(void);

/* Sign a message with the given ID.  Returns the signed message, NULL if
   unsuccessful. */
struct gale_message *sign_message(struct gale_id *id,struct gale_message *);
/* Encrypt a message to the given IDs.  
   Returns the encrypted message, NULL if unsuccessful. */
struct gale_message *encrypt_message(int num,struct gale_id **id,
                                     struct gale_message *);

/* Verify a message's digital signature.  Returns NULL if unsueccessful,
   otherwise the (new) gale_id (see id.h) for the sender (free it yourself). */
struct gale_id *verify_message(struct gale_message *);

/* Decrypt a message.  Returns NULL if unsuccessful (or not encrypted!).
   Stores a pointer to the decrypted message, to the original message
   (with another reference count) if not encrypted, or to NULL if encrypted
   but unable to decrypt. */
struct gale_id *decrypt_message(struct gale_message *,struct gale_message **);

#endif
