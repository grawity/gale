/* core.h -- core low-level libgale functions */

#ifndef GALE_CORE_H
#define GALE_CORE_H

#include "gale/types.h"

/* -- gale version and initialization -------------------------------------- */

/* Initialize gale stuff.  First parameter is the program name ("gsub");
   the next two are argc and argv. */
void gale_init(const char *,int argc,char * const *argv);

struct gale_environ;

/* Get an environment variable. */
struct gale_text gale_var(struct gale_text);

/* Set an environment variable. */
void gale_set(struct gale_text var,struct gale_text value);

/* Save and restore the environment as a whole. */
struct gale_environ *gale_save_environ(void);
void gale_restore_environ(struct gale_environ *);

/* The makefiles define this based on the "version" file. */
#ifndef VERSION
#error You must define VERSION.
#endif

/* A banner, suitable for usage messages. */
#define GALE_BANNER \
	(PACKAGE " version " VERSION ", copyright 1997,1998 Dan Egnor")

/* -- management of message (puff) objects --------------------------------- */

struct gale_message {
	struct gale_text cat;   /* Category expression text. */
	struct gale_group data; /* Message data. */
};

/* Create a new message with empty fields. */
struct gale_message *new_message(void);

/* -- gale server connection management ------------------------------------ */

/* A link object represents an active connection to a Gale server.  It does
   not include the actual file descriptor -- see client.h for a simplified
   interface that includes the actual connection.  These links are basically
   protocol states and message queues. */

struct gale_link;

/* Create a new, empty link. */
struct gale_link *new_link(void);
/* Reset the link, in case you've lost a server connection and are 
   reconnecting.  Basically, puts the protocol back to the ground state. */
void reset_link(struct gale_link *);

/* Again, see client.h for higher-level routines that call the next four. */

/* True (1) if the link is ready to receive a message.  If an incoming message 
   is already pending, this will be false. */
int link_receive_q(struct gale_link *);
/* Receive some data from a file descriptor.  Returns zero if successful;
   nonzero if there's a problem. */
int link_receive(struct gale_link *,int fd);
/* True (1) if the link is ready to transmit a message.  If there are no
   more outgoing messages or operations in the queue, this will be false. */
int link_transmit_q(struct gale_link *);
/* Transmit some data to a file descriptor.  Returns zero if successful;
   nonzero if there's a problem. */
int link_transmit(struct gale_link *,int fd);

/* Set the link's subscription, which will be transmitted to the server at 
   the next opportunity. */
void link_subscribe(struct gale_link *,struct gale_text spec);
/* Add a message to the link's outgoing queue. */
void link_put(struct gale_link *,struct gale_message *);
/* Replace the current "will" message with this one.  It will be sent to the
   server at the next opportunity; the server will transmit the message when
   the connection fails. */
void link_will(struct gale_link *,struct gale_message *);
/* Return the total number and size of outgoing messages in the queue. */
int link_queue_num(struct gale_link *);
size_t link_queue_mem(struct gale_link *);
/* Drop the earliest outgoing message. */
void link_queue_drop(struct gale_link *);

/* Get the protocol version established on the link, -1 if not known yet. */
int link_version(struct gale_link *);
/* Get the next incoming message.  NULL if there aren't any. */
struct gale_message *link_get(struct gale_link *);
/* If the other end of the link sent a "will", get it.  (Otherwise NULL.) */
struct gale_message *link_willed(struct gale_link *);
/* If the other end sent a subscription, get it.  (Otherwise NULL.) */
struct gale_text link_subscribed(struct gale_link *);

/* -- cache management ----------------------------------------------------- */

/* Many of these functions have "raw" versions which operate on gale_data
   instead of the normal versions which operate on gale_group structures.
   Where possible, eschew the "raw" version and operate on structured data. */

/* Compute the cache ID for some data. */
struct gale_data cache_id(struct gale_group);
struct gale_data cache_id_raw(struct gale_data);

/* Attempt to find an item in the cache.  Returns true iff successful. */
int cache_find(struct gale_data id,struct gale_group *data);
int cache_find_raw(struct gale_data id,struct gale_data *data);

/* Store an item in the cache (if possible), optionally returning a file. */
void cache_store(struct gale_data id,struct gale_group data);
void cache_store_raw(struct gale_data id,struct gale_data data);

#endif
