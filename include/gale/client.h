/** \file
 *  Convenient high-level interface functions. */

#ifndef GALE_CLIENT_H
#define GALE_CLIENT_H

#include "gale/core.h"
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

/** Look up a Gale location address.
 *  Start looking up a Gale location address in the background and return
 *  immediately.  When the lookup is complete (whether it succeeded or 
 *  failed), the supplied callback is invoked.
 *  \param oop Liboop event source to use.
 *  \param name Name of the location to look up (e.g. "pub.food").
 *  \param func Function to call when location lookup completes.
 *  \param user User-defined parameter to pass the function. 
 *  \sa gale_location_name(), gale_find_exact_location() */
void gale_find_location(oop_source *oop,
	struct gale_text name,
	gale_call_location *func,void *user);

/** Lookup a Gale location address without alias expansion.
 *  This function is like gale_find_location(), but accepts only canonical
 *  location names, and skips all alias expansion steps.
 *  \param oop Liboop event source to use.
 *  \param name Name of the location to look up (e.g. "pub.food@ofb.net").
 *  \param func Function to call when location lookup completes.
 *  \param user User-defined parameter to pass the function. 
 *  \sa gale_find_location() */
void gale_find_exact_location(oop_source *oop,
	struct gale_text name,
	gale_call_location *func,void *user);

/** Look up the default user location.
 *  Start looking up the local user's default "personal" location.  When the
 *  lookup is complete (whether it succeeded or failed), the supplied callback
 *  is invoked.
 *  \param oop Liboop event source to use.
 *  \param func Function to call when location lookup completes.
 *  \param user User-defined parameter to pass the function.
 *  \sa gale_find_location() */
void gale_find_default_location(oop_source *oop,
	gale_call_location *func,void *user);

/** Find a location's name.
 *  This is approximately the opposite of gale_find_location().
 *  \param loc Location to examine.
 *  \return The name of the location (e.g. "pub.food@ofb.net"). */
struct gale_text gale_location_name(struct gale_location *loc);

/** Find the "root" of a location.
 *  This function will return the "parent" responsible for setting a 
 *  location's properties.  (For example, the root of "pub.food.bitter@ofb.net"
 *  may be "pub@ofb.net".) 
 *  \param loc Location to find root for.
 *  \return Root location. */
struct gale_location *gale_location_root(struct gale_location *loc);

/** Get all the public metadata associated with a location.
 *  \param loc Location to examine.
 *  \return Public location metadata in ::gale_group format. */
struct gale_group gale_location_public_data(struct gale_location *loc);

/** Get all the private metadata associated with a location.
 *  \param loc Location to examine.
 *  \return Private location metadata in ::gale_group format. */
struct gale_group gale_location_private_data(struct gale_location *loc);

/** Determine if we can receive messages sent to a location.
 *  Effectively, this is true if we hold the private key for a location
 *  (or the location is public). 
 *  \param loc Location to examine.
 *  \return Nonzero if we can subscribe to this location. */
int gale_location_receive_ok(struct gale_location *loc);

/** Determine if we can send messages to a location.
 *  \param loc Location to examine.
 *  \return Nonzero if we can send messages to this location. */
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

/** Pack a Gale message into a raw "packet".
 *  Packing may require location lookups, so this function starts
 *  the process in the background, using liboop to invoke a callback
 *  when the process is complete.
 *  \param oop Liboop event source to use.
 *  \param msg Message to pack.
 *  \param func Function to call with packed message.
 *  \param user User-defined parameter to pass the function.
 *  \sa gale_unpack_message(), link_put() */
void gale_pack_message(oop_source *oop,
	struct gale_message *msg,
	gale_call_packet *call,void *user);

/** Function type to report an unpacked message.
 *  \param msg The message, NULL if unsuccessful.
 *  \param user User-defined parameter.
 *  \return Liboop continuation code (usually OOP_CONTINUE).
 *  \sa gale_unpack_message() */
typedef void *gale_call_message(struct gale_message *msg,void *user);

/** Unpack a Gale message from a raw "packet".
 *  Unpacking may require location lookups, so this function starts
 *  the process in the background, using liboop to invoke a callback
 *  when the process is complete.
 *  \param oop Liboop event source to use.
 *  \param pack "Packet" to unpack (usually as received).
 *  \param func Function to call with unpacked message.
 *  \param user User-defined parameter to pass the function. 
 *  \sa gale_pack_message(), link_on_message() */
void gale_unpack_message(oop_source *oop,
	struct gale_packet *pack,
	gale_call_message *func,void *user);

/*@}*/

/** \name Compatibility stuff (ignore) */
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
 *  If you use gale_open(), you should \e not use link_subscribe() or
 *  link_on_error().  (Instead, use gale_reopen() or gale_on_disconnect().)
 *  \param oop Liboop event source to use.
 *  \param link The ::gale_link to keep connected.
 *  \param subscr The subscription to use.
 *  \param server The server to connect to.  Normally ::null_text for the
 *                default server.
 *  \param avoid_local Normally zero.  Nonzero to avoid connecting to the
 *                     local host or numerically smaller IP addresses.
 *                     (Used by the server to avoid loops.)
 *  \return A server handle you can use to disconnect the link later.
 *  \sa gale_reopen(), gale_close(), gale_on_connect(), gale_on_disconnect() */
struct gale_server *gale_open(
	oop_source *oop,struct gale_link *link,
	struct gale_text subscr,struct gale_text server,int avoid_local);

/** Change subscriptions.
 *  Reset the subscriptions of a Gale server connection without having to
 *  take down the connection entirely.
 *  \param serv The server handle returned by gale_open().
 *  \param subscr The new subscriptions. */
void gale_reopen(struct gale_server *serv,struct gale_text subscr);

/** Disconnect from a Gale server.
 *  Gracefully closes a Gale server connection.
 *  \param serv The server handle returned by gale_open().
 *  \sa gale_open() */
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
