/* client.h -- high-level interfaces to gale (helper functions) */

#ifndef GALE_CLIENT_H
#define GALE_CLIENT_H

#include "gale/core.h"
#include "oop.h"

/* -- server connection management ------------------------------------------*/

/* Using the given event source, keep a gale_link connected to the server.
   Subscribe to the given subscription list.
   Uses the on_error event handler. */
struct gale_server *gale_open(
	oop_source *,struct gale_link *,
	struct gale_text subscr,struct gale_text server);

/* Stop connecting to the server. */
void gale_close(struct gale_server *);

/* Notifications. */
void gale_on_connect(struct gale_server *,
     void *(*)(struct gale_server *,void *),
     void *);

void gale_on_disconnect(struct gale_server *,
     void *(*)(struct gale_server *,void *),
     void *);

/* -- standard fragment utilities -------------------------------------------*/

void gale_add_id(struct gale_group *group,struct gale_text terminal);

/* -- gale user id management ---------------------------------------------- */

struct auth_id; /* defined in gauth.h */

/* Control AKD, if you need to suppress it.  Starts out enabled. */
void disable_gale_akd(); /* Increases the "suppress count" */
void enable_gale_akd();  /* Decreases the "suppress count" */

/* Look up an ID by the local naming conventions. */
struct auth_id *lookup_id(struct gale_text);

/* Find our own ID, generate keys if necessary. */
struct auth_id *gale_user();

/* Return @ domain / pfx / user / sfx in a newly allocated string. */
/*owned*/ struct gale_text 
id_category(struct auth_id *,struct gale_text pfx,struct gale_text sfx);

/* Return @ dom / pfx / in a newly allocated string.  NULL dom = default */
/*owned*/ struct gale_text 
dom_category(struct gale_text dom,struct gale_text pfx);

/* -- message-level authentication and encryption -------------------------- */

struct gale_message; /* defined in core.h */

/* Sign a message with the given ID.  
   Returns the signed message, NULL if unsuccessful. */
struct gale_message *sign_message(struct auth_id *id,struct gale_message *);

/* Don't use this. */
struct gale_message *_sign_message(struct auth_id *id,struct gale_message *);

/* Encrypt a message to the given IDs.  
   Returns the encrypted message, NULL if unsuccessful. */
struct gale_message *encrypt_message(int num,struct auth_id **id,
                                     struct gale_message *);

/* Verify a message's digital signature.  
   Returns sender's id (see gauth.h; free yourself), NULL if unsuccessful. */
struct auth_id *verify_message(struct gale_message *,struct gale_message **);

/* Decrypt a message.  
   Returns the recipient, NULL if unsuccessful or not encrypted.
   Stores a pointer to the decrypted message, to the original message
   (with another reference count) if not encrypted, or to NULL if encrypted
   but unable to decrypt. */
struct auth_id *decrypt_message(struct gale_message *,struct gale_message **);

#endif
