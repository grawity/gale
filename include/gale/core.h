/* core.h -- core low-level libgale functions */

#ifndef GALE_CORE_H
#define GALE_CORE_H

#include "gale/types.h"

/* -- gale version and initialization -------------------------------------- */

/* Initialize gale stuff.  First parameter is the program name ("gsub");
   the next two are argc and argv. */
void gale_init(const char *,int argc,char * const *argv);

/* The makefiles define this based on the "version" file. */
#ifndef VERSION
#error You must define VERSION.
#endif

/* A banner, suitable for usage messages. */
#define GALE_BANNER \
	(PACKAGE " version " VERSION ", copyright 1997,1998 Dan Egnor")

/* -- management of message (puff) objects --------------------------------- */

/* The message object is reference counted; this is done mostly for the 
   server, but might prove useful elsewhere. */

struct gale_message {
	struct gale_text cat;  /* Category expression text. */
	struct gale_data data; /* Message data. */
	int ref;               /* Reference count. */
};

/* Create a new message with a single reference count and fields empty. */
struct gale_message *new_message(void);

/* Manage reference counts on a message.  release_message() will delete the
   message (and free the category and data) if the reference count reaches 0 */
void addref_message(struct gale_message *);
void release_message(struct gale_message *);

/* -- gale server connection management ------------------------------------ */

/* A link object represents an active connection to a Gale server.  It does
   not include the actual file descriptor -- see client.h for a simplified
   interface that includes the actual connection.  These links are basically
   protocol states and message queues. */

struct gale_link;

/* Create a new, empty link. */
struct gale_link *new_link(void);
/* The same, but use the old protocol. */
struct gale_link *new_old_link(void);
/* Destroy a link, and release any contained messages. */
void free_link(struct gale_link *);
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

/* Get the next incoming message.  NULL if there aren't any. */
struct gale_message *link_get(struct gale_link *);
/* If the other end of the link sent a "will", get it.  (Otherwise NULL.) */
struct gale_message *link_willed(struct gale_link *);
/* If the other end sent a subscription, get it.  (Otherwise NULL.) */
struct gale_text link_subscribed(struct gale_link *);

#endif
