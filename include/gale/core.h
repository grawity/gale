/* core.h -- core low-level libgale functions */

#ifndef GALE_CORE_H
#define GALE_CORE_H

/* -- basic data types and such -------------------------------------------- */

#include <sys/types.h>
#include "gale/compat.h"
#include "gale/config.h"

#if SIZEOF_INT == 4
typedef unsigned int u32;
#elif SIZEOF_LONG == 4
typedef unsigned long u32;
#elif SIZEOF_SHORT == 4
typedef unsigned short u32;
#else
#error Cannot find 32-bit data type!
#endif

#if SIZEOF_INT == 2
typedef unsigned int u16;
#elif SIZEOF_LONG == 2
typedef unsigned long u16;
#elif SIZEOF_SHORT == 2
typedef unsigned short u16;
#else
#error Cannot find 16-bit data type!
#endif

typedef unsigned char u8;
typedef u8 byte;

/* handy data type for a counted buffer. */
struct gale_data {
	byte *p;
	size_t l;
};

/* -- gale version and initialization -------------------------------------- */

/* Initialize gale stuff.  First parameter is the program name ("gsub");
   the next two are argc and argv. */
void gale_init(const char *,int argc,char * const *argv);

/* The makefiles define this based on the "version" file. */
#ifndef GALE_VERSION
#error You must define GALE_VERSION.
#endif

/* A banner, suitable for usage messages. */
#define GALE_BANNER \
	("Gale version " ## GALE_VERSION ## ", copyright 1997 Dan Egnor")

/* -- management of message (puff) objects --------------------------------- */

/* The message object is reference counted; this is done mostly for the 
   server, but might prove useful elsewhere. */

struct gale_message {
	char *category;        /* Owned pointer.  NUL-terminated category. */
	/* these two should be a struct gale_data */
	int data_size;         /* Size of message data, in characters. */
	char *data;            /* Owned pointer to the data (not NUL-ended!) */
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
/* Destroy a link, and release any contained messages. */
void free_link(struct gale_link *);
/* Set limits on the link's buffer size -- both number of messages and maximum
   memory.  If either is exceeded, it will start dropping messages. */
void link_limits(struct gale_link *,int num,int mem);
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
void link_subscribe(struct gale_link *,const char *spec);
/* Add a message to the link's outgoing queue. */
void link_put(struct gale_link *,struct gale_message *);
/* Replace the current "will" message with this one.  It will be sent to the
   server at the next opportunity; the server will transmit the message when
   the connection fails. */
void link_will(struct gale_link *,struct gale_message *);
/* Send an ENQ message with a given cookie to the server at the next
   opportunity. */
void link_enq(struct gale_link *,int cookie);

/* Return the number of (incoming + outgoing) messages in the queue. */
int link_queue(struct gale_link *);
/* Return the number of messages the server dropped without sending to us
   (this happens if we're being slow).  Dependent on the server notifying
   us of this, of course.  When called, resets the counter to zero. */
int link_lossage(struct gale_link *);
/* Get the next incoming message.  NULL if there aren't any. */
struct gale_message *link_get(struct gale_link *);
/* If the other end of the link sent a "will", get it.  (Otherwise NULL.) */
struct gale_message *link_willed(struct gale_link *);
/* If the other end sent a subscription, get it.  (Otherwise NULL.) */
char *link_subscribed(struct gale_link *);
/* If the other end sent an ACK in response to our ENQ, return the cookie.
   (Otherwise zero.) */
int link_ack(struct gale_link *);

#endif
