/* core.h -- core low-level libgale functions */

#ifndef GALE_CORE_H
#define GALE_CORE_H

#include "gale/types.h"
#include "oop.h"

/* -- gale version and initialization -------------------------------------- */

/* Initialize gale stuff.  First parameter is the program name ("gsub");
   the next two are argc and argv. */
void gale_init(const char *,int argc,char * const *argv);

/* Set up standard signal handlers. */
void gale_init_signals(oop_source *);

/* Get an environment variable. */
struct gale_text gale_var(struct gale_text);

/* Set an environment variable. */
void gale_set(struct gale_text var,struct gale_text value);

/* Save and restore the environment as a whole. */
struct gale_environ;
struct gale_environ *gale_save_environ(void);
void gale_restore_environ(struct gale_environ *);

/* The makefiles define this based on the "version" file. */
#ifndef VERSION
#error You must define VERSION.
#endif

/* A banner, suitable for usage messages. */
#define GALE_BANNER ( \
	"Gale version " VERSION ", copyright 1997-1999 Dan Egnor\n" \
	"This software comes with ABSOLUTELY NO WARRANTY.  You may redistribute\n" \
	"it under certain conditions.  Run \"gale-config --license\" for information." \
	)

/* -- management of message (puff) objects --------------------------------- */

struct gale_message {
	struct gale_text cat;   /* Category expression text. */
	struct gale_group data; /* Message data. */
};

/* Create a new message with empty fields. */
struct gale_message *new_message(void);

/* -- gale server connection management ------------------------------------ */

/* A link object represents an active connection to a Gale server. */
struct gale_link;

/* Protocol constants. */
static const int gale_port = 11512;

/* Create a new link using an event source. */
struct gale_link *new_link(struct oop_source *);
/* Close an old link. */
void delete_link(struct gale_link *);
/* Flush and close outgoing link; will call _on_empty upon incoming EOF. */
void link_shutdown(struct gale_link *);
/* Attach the link to a new file descriptor.  Use -1 to detach the link. */
void link_set_fd(struct gale_link *,int fd);
/* Get the file descriptor associated with a link (-1 if none). */
int link_get_fd(struct gale_link *);

/* Event handler for I/O errors. */
void link_on_error(struct gale_link *,
     void *(*)(struct gale_link *,int,void *),
     void *);

  /* -- version 0 operations --------------------------------------------- */

/* Transmit a message. */
void link_put(struct gale_link *,struct gale_message *);

/* Transmit a "will" message (held and transmitted if the connection fails) */
void link_will(struct gale_link *,struct gale_message *);

/* Transmit a subscription request. */
void link_subscribe(struct gale_link *,struct gale_text spec);

/* Manage the outgoing message queue. */
int link_queue_num(struct gale_link *);    /* Number of messages in queue. */
size_t link_queue_mem(struct gale_link *); /* Memory consumed by queue. */
struct gale_time link_queue_time(struct gale_link *); /* Oldest message. */
void link_queue_drop(struct gale_link *);  /* Drop the earliest message. */

/* Event handler for when the outgoing queue is empty. */
void link_on_empty(struct gale_link *, 
     void *(*)(struct gale_link *,void *),
     void *);

/* Event handler for arriving messages. */
void link_on_message(struct gale_link *,
     void *(*)(struct gale_link *,struct gale_message *,void *),
     void *);

/* Event handler for incoming "will" messages. */
void link_on_will(struct gale_link *,
     void *(*)(struct gale_link *,struct gale_message *,void *),
     void *);

/* Event handler for incoming subscription requests. */
void link_on_subscribe(struct gale_link *,
     void *(*)(struct gale_link *,struct gale_text,void *),
     void *);

  /* -- version 1 operations --------------------------------------------- */

void ltx_publish(struct gale_link *,struct gale_text spec);
void ltx_watch(struct gale_link *,struct gale_text category);
void ltx_forget(struct gale_link *,struct gale_text category);
void ltx_complete(struct gale_link *,struct gale_text category);
void ltx_assert(struct gale_link *,struct gale_text cat,struct gale_data cid);
void ltx_retract(struct gale_link *,struct gale_text cat,struct gale_data cid);
void ltx_fetch(struct gale_link *,struct gale_data cid);
void ltx_miss(struct gale_link *,struct gale_data cid);
void ltx_supply(struct gale_link *,struct gale_data cid,struct gale_data data);

int lrx_publish(struct gale_link *,struct gale_text *spec);
int lrx_watch(struct gale_link *,struct gale_text *category);
int lrx_forget(struct gale_link *,struct gale_text *category);
int lrx_complete(struct gale_link *,struct gale_text *category);
int lrx_assert(struct gale_link *,struct gale_text *cat,struct gale_data *cid);
int lrx_retract(struct gale_link *,struct gale_text *cat,struct gale_data *cid);
int lrx_fetch(struct gale_link *,struct gale_data *cid);
int lrx_miss(struct gale_link *,struct gale_data *cid);
int lrx_supply(struct gale_link *,struct gale_data *cid,struct gale_data *data);

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
