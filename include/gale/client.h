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
	struct gale_text subscr,struct gale_text server,int avoid_local);

/* Stop connecting to the server. */
void gale_close(struct gale_server *);

/* Notifications. */
typedef void *gale_call_disconnect(struct gale_server *,void *);
typedef void *gale_call_connect(struct gale_server *,
	struct gale_text host,
	struct sockaddr_in addr,void *);

void gale_on_connect(struct gale_server *,gale_call_connect *,void *);
void gale_on_disconnect(struct gale_server *,gale_call_disconnect *,void *);

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

/* Return @ domain / pfx / user / sfx. */
struct gale_text 
id_category(struct auth_id *,struct gale_text pfx,struct gale_text sfx);

/* Return @ dom / pfx /.  NULL dom = default */
struct gale_text 
dom_category(struct gale_text dom,struct gale_text pfx);

#endif
