/* auth.h -- message-level authentication */

#ifndef GALE_AUTH_H
#define GALE_AUTH_H

#include "message.h"
#include "id.h"

/* Sign a message with the given ID.  
   Returns the signed message, NULL if unsuccessful. */
struct gale_message *sign_message(struct auth_id *id,struct gale_message *);
/* Encrypt a message to the given IDs.  
   Returns the encrypted message, NULL if unsuccessful. */
struct gale_message *encrypt_message(int num,struct auth_id **id,
                                     struct gale_message *);

/* Verify a message's digital signature.  
   Returns sender's id (see gauth.h; free yourself), NULL if unsuccessful. */
struct auth_id *verify_message(struct gale_message *);

/* Decrypt a message.  
   Returns the recipient, NULL if unsuccessful or not encrypted.
   Stores a pointer to the decrypted message, to the original message
   (with another reference count) if not encrypted, or to NULL if encrypted
   but unable to decrypt. */
struct auth_id *decrypt_message(struct gale_message *,struct gale_message **);

/* deprecated; wrapper to auth_id_gen */
void gale_keys(void);

#endif
