/* client.h -- high-level interfaces to gale (helper functions) */

#ifndef GALE_CLIENT_H
#define GALE_CLIENT_H

#include "gale/core.h"

/* -- simplified interface to server connections ----------------------------*/

/* Client structure.  Don't write any of these fields yourself. */

struct gale_client {
	int socket;                   /* The file descriptor (for select). */
	struct gale_link *link;       /* The link object (see link.h) */
	struct gale_text server;      /* Name of server */
	struct gale_text subscr;      /* Sub list */
};

/* Open a connection to the server (defined by GALE_SERVER).  "spec" is the
   subscription list to use; NULL if you don't want to subscribe. */
struct gale_client *gale_open(struct gale_text spec);

/* Close a connection opened by gale_open. */
void gale_close(struct gale_client *);

/* If you get an error, use this to reattempt connection to the server.  It
   will retry with progressive delay until it succeeds. */
void gale_retry(struct gale_client *);

/* Return nonzero if an error has occurred (and you should call gale_retry), 0
   otherwise.  If this returns nonzero, you can't do either of the following 
   two operations on the connection. */
int gale_error(struct gale_client *);

/* Transmit any queued messages on the link.  Returns 0 if successful. */
int gale_send(struct gale_client *);

/* Wait for the next message on the link.  Returns 0 if successful.  (Extract
   the actual message from the gale_link -- see link.h.) */
int gale_next(struct gale_client *);

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
