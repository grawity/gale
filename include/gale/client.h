/** \file
 *  Convenient high-level interface functions. */

#ifndef GALE_CLIENT_H
#define GALE_CLIENT_H

#include "gale/core.h"
#include "gale/misc.h"
#include "oop.h"

/** \name Message Formatting */
/*@{*/

struct gale_location;

/** Function type to report a complete location lookup.
 *  \param name Name of the location.
 *  \param loc Location handle if successful, NULL if unsuccessful.
 *  \param user User-defined parameter. 
 *  \return Liboop continuation code (usually OOP_CONTINUE).
 *  \sa gale_find_location() */
typedef void *gale_call_location(struct gale_text name,
	struct gale_location *loc,void *user);

void gale_find_location(
	oop_source *,struct gale_text,
	gale_call_location *,void *user);
void gale_find_exact_location(
	oop_source *,struct gale_text,
	gale_call_location *,void *user);
void gale_find_default_location(
	oop_source *,gale_call_location *,void *user);

struct gale_text gale_location_name(struct gale_location *loc);
struct gale_key *gale_location_key(struct gale_location *loc);
const struct gale_map *gale_location_members(struct gale_location *loc);

int gale_location_receive_ok(struct gale_location *loc);
int gale_location_send_ok(struct gale_location *loc);

/** Logical Gale message structure. */
struct gale_message {
        /** List of authenticated senders (NULL-terminated).  */
	struct gale_location **from;
        /** List of message recipients (NULL-terminated).  */
	struct gale_location **to;
        /** Message data substructure.  */
	struct gale_group data;
};

/** Function type to report a packed message.
 *  \param pack Fully packed message, or NULL if unsuccessful.
 *  \param user User-defined parameter.
 *  \return Liboop continuation code (usually OOP_CONTINUE).
 *  \sa gale_pack_message() */
typedef void *gale_call_packet(struct gale_packet *msg,void *user);

void gale_pack_message(oop_source *oop,
	struct gale_message *msg,
	gale_call_packet *call,void *user);

/** Function type to report an unpacked message.
 *  \param msg The message, NULL if unsuccessful.
 *  \param user User-defined parameter.
 *  \return Liboop continuation code (usually OOP_CONTINUE).
 *  \sa gale_unpack_message() */
typedef void *gale_call_message(struct gale_message *msg,void *user);

void gale_unpack_message(oop_source *oop,
	struct gale_packet *pack,
	gale_call_message *func,void *user);

struct gale_text gale_pack_subscriptions(
	struct gale_location **list,
	int *positive);
/*@}*/

/** \name Error Processing */
/*@{*/

struct gale_error_queue;

struct gale_error_queue *gale_make_queue(oop_source *);
void gale_on_queue(struct gale_error_queue *,gale_call_message *,void *);
gale_call_error gale_queue_error;

/*@}*/

/** \name Connection Management */
/*@{*/

/** Connect to a Gale server.
 *  Keeps a link connected.  Uses link_on_error() to automatically reconnect 
 *  if the connection is closed.
 *  If you use gale_make_server(), you should \e not use link_on_error().  
 *  (Instead, use gale_on_disconnect().)
 *  \param oop Liboop event source to use.
 *  \param link The ::gale_link to keep connected.
 *  \param server The server to connect to.  Normally ::null_text for the
 *                default server.
 *  \param avoid_port Normally zero.  Nonzero to avoid connecting to the
 *                    local host or numerically smaller IP addresses at
 *                    the specified port.  (Used by the server to avoid loops.)
 *  \return A server handle you can use to disconnect the link later.
 *  \sa gale_reopen(), gale_close(), gale_on_connect(), gale_on_disconnect() */
struct gale_server *gale_make_server(
	oop_source *oop,struct gale_link *link,
	struct gale_text server,int avoid_port);

/** Disconnect from a Gale server.
 *  Gracefully closes a Gale server connection.
 *  \param serv The server handle returned by gale_make_server().
 *  \sa gale_make_server() */
void gale_close(struct gale_server *serv);

/** Function type for user-defined notification handler for disconnection.
 *  \param serv The server handle.
 *  \param user User-defined parameter.
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
 *  When gale_make_server() is called, the connection process is initiated 
 *  in the background.  When it completes, the function (if any) registered 
 *  with gale_on_connect() will be called.  Henceforth, the function is 
 *  also called for every successful reconnection (after disconnection).
 *  \param serv The server handle returned by gale_make_server().
 *  \param func The function to call when connection succeeds.
 *  \param user A user-defined parameter.
 *  \sa gale_make_server(), gale_call_connect() */
void gale_on_connect(struct gale_server *serv,gale_call_connect *func,void *user);

/** Set a handler to be called when a connection is broken.
 *  A connection established by gale_make_server() can be broken by the 
 *  remote end, by a faulty network, or by gale_close().  In all but the 
 *  last case, the connection manager will immediately begin a reconnection 
 *  attempt.  In any case, the function (if any) registered with 
 *  gale_on_disconnect() will be called to let you know.
 *  \param serv The server handle returned by gale_make_server().
 *  \param func The function to call when a connection is broken.
 *  \param user A user-defined parameter.
 *  \sa gale_make_server(), gale_close(), gale_call_disconnect() */
void gale_on_disconnect(struct gale_server *,gale_call_disconnect *func,void *user);

/*@}*/

/* -- standard fragment utilities -------------------------------------------*/

void gale_add_id(struct gale_group *group,struct gale_text terminal);

#endif
