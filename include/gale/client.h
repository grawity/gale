/** \file
 *  Convenient high-level interface functions. */

#ifndef GALE_CLIENT_H
#define GALE_CLIENT_H

#include "gale/core.h"
#include "oop.h"

/** \name Compatibility stuff (ignore). */
/*@{*/

struct old_gale_message {
	struct gale_text cat;
	struct gale_group data;
};

struct old_gale_message *gale_make_message(void);
struct old_gale_message *gale_receive(struct gale_packet *pack);
struct gale_packet *gale_transmit(struct old_gale_message *pack);

/*@}*/

/** \name Connection Management */
/*@{*/

/** Connect to a Gale server.
 *  Keeps a link connected and subscribed.  Uses link_on_error() to 
 *  automatically reconnect and resubscribe if the connection is closed.
 *  \param oop Liboop event source to use.
 *  \param link The ::gale_link to keep connected.
 *  \param subscr The subscription to use.
 *  \param server The server to connect to.  Normally ::null_text for the
 *                default server.
 *  \param avoid_local Normally zero.  Nonzero to avoid connecting to the
 *                     local host or numerically smaller IP addresses.
 *                     (Used by the server to avoid loops.)
 *  \return A server handle you can use to disconnect the link later.
 *  \sa gale_close(), gale_on_connect(), gale_on_disconnect() */
struct gale_server *gale_open(
	oop_source *oop,struct gale_link *link,
	struct gale_text subscr,struct gale_text server,int avoid_local);

/** Disconnect from a Gale server.
 *  Gracefully closes a Gale server connection.
 *  \param serv The server handle returned by gale_open().
 *  \sa gale_open() */
void gale_close(struct gale_server *serv);

/** Function type for user-defined notification handler for disconnection.
 *  \param serv The server handle.
 *  \param user The user-defined parameter to pass the function. 
 *  \return Liboop continuation code (usually OOP_CONTINUE).
 *  \sa gale_on_disconnect() */
typedef void *gale_call_disconnect(struct gale_server *serv,void *user);

/** Function type for user-defined notification handler for connection.
 *  \param serv The server handle.
 *  \param host The hostname of the chosen server.
 *  \param addr The IP address of the chosen server.
 *  \param user The user-defined parameter to pass the function. 
 *  \return Liboop continuation code (usually OOP_CONTINUE).
 *  \sa gale_on_connect() */
typedef void *gale_call_connect(struct gale_server *serv,
	struct gale_text host,struct sockaddr_in addr,void *);

/** Set a handler to be called when a connection is established.
 *  When gale_open() is called, the connection process is initiated in the
 *  background.  When it completes, the function (if any) registered with
 *  gale_on_connect() will be called to let you know.
 *  \param serv The server handle returned by gale_open().
 *  \param func The function to call when connection succeeds.
 *  \param user A user-defined parameter.
 *  \sa gale_open(), gale_call_connect() */
void gale_on_connect(struct gale_server *serv,gale_call_connect *func,void *user);

/** Set a handler to be called when a connection is broken.
 *  A connection established by gale_open() can be broken by the remote end,
 *  by a faulty network, or by gale_close().  In all but the last case, the
 *  connection manager will immediately begin a reconnection attempt.  In
 *  any case, the function (if any) registered with gale_on_disconnect()
 *  will be called to let you know.
 *  \param serv The server handle returned by gale_open().
 *  \param func The function to call when a connection is broken.
 *  \param user A user-defined parameter.
 *  \sa gale_open(), gale_close(), gale_call_disconnect() */
void gale_on_disconnect(struct gale_server *,gale_call_disconnect *func,void *user);

/*@}*/

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
