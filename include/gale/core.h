/** \file
 *  Core, low-level Gale functionality. */

#ifndef GALE_CORE_H
#define GALE_CORE_H

#include "gale/types.h"
#include "oop.h"

/** \name Initialization 
 *  First call gale_init(), then initialize liboop, 
 *  then call gale_init_signals(). */
/*@{*/
/** Initialize Gale.
 *  \param name The program name (e.g. "gsub").
 *  \param argc Argument count from main().
 *  \param argv Argument vector from main(). 
 *  \sa gale_init_signals() */
void gale_init(const char *name,int argc,char * const *argv);

/** Set up standard signal handlers. 
 *  This is distinct from gale_init() because it requires liboop 
 *  initialization, but liboop can't be initialized until after Gale is. 
 *  \param oop Liboop event source to use for signal dispatch.
 *  \sa gale_init() */
void gale_init_signals(oop_source *oop);

/** Get an environment or configuration variable. */
struct gale_text gale_var(struct gale_text name);

/** Set an environment or configuration variable. */
void gale_set(struct gale_text var,struct gale_text value);

struct gale_environ;

/** Save the environment as a whole. 
 *  \return Snapshot of the environment.
 *  \sa gale_restore_environ() */
struct gale_environ *gale_save_environ(void);

/** Restore the environment from a saved state. 
 *  \param env Snapshot of the environment from gale_save_environ(). 
 *  \sa gale_save_environ() */
void gale_restore_environ(struct gale_environ *env);

#ifndef VERSION
#error You must define VERSION.
#endif

/** The standard Gale banner (including the copyright message), good for
 *  including in a usage message. */
#define GALE_BANNER ( \
"Gale version " VERSION ", copyright 1997-2001 Dan Egnor\n" \
"This software comes with ABSOLUTELY NO WARRANTY.  You may redistribute\n" \
"it under certain conditions.  Run \"gale-config --license\" for information." \
)
/*@}*/

/** Raw, unprocessed Gale data packet. */
struct gale_packet {
	/** Routing information (location string) */
	struct gale_text routing;
	/** Data content */
	struct gale_data content;
};

/** \name Gale Protocol 
 *  The ::gale_link structure represents the protocol state of an active 
 *  connection to a Gale server; it depends on a physical connection
 *  (socket file descriptor) maintained by other means. */
/*@{*/

struct gale_link;

/** The default port number to use for connections. */
static const int gale_port = 11512;

struct gale_link *new_link(struct oop_source *oop);
void delete_link(struct gale_link *);
void link_shutdown(struct gale_link *);
void link_set_fd(struct gale_link *,int fd);
int link_get_fd(struct gale_link *);

void link_on_error(struct gale_link *,
     void *(*)(struct gale_link *,int,void *),
     void *);

void link_put(struct gale_link *,struct gale_packet *);
void link_will(struct gale_link *,struct gale_packet *);
void link_subscribe(struct gale_link *,struct gale_text spec);

int link_queue_num(struct gale_link *);
size_t link_queue_mem(struct gale_link *);
struct gale_time link_queue_time(struct gale_link *);
void link_queue_drop(struct gale_link *);

void link_on_empty(struct gale_link *, 
     void *(*)(struct gale_link *,void *),
     void *);
void link_on_message(struct gale_link *,
     void *(*)(struct gale_link *,struct gale_packet *,void *),
     void *);
void link_on_will(struct gale_link *,
     void *(*)(struct gale_link *,struct gale_packet *,void *),
     void *);
void link_on_subscribe(struct gale_link *,
     void *(*)(struct gale_link *,struct gale_text,void *),
     void *);
/*@}*/

/** \name Sticky Categories
 *  This functionality isn't really implemented yet, so it's not
 *  documented, either. */
/*@{*/
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
/*@}*/

/** \name Cache Management 
 *  Global file cache management (not used by anything yet).
 *  Many of these functions have "raw" versions which operate on gale_data
 *  instead of the normal versions which operate on gale_group structures.
 *  Where possible, eschew the "raw" version and operate on structured data.  */
/*@{*/

/** Compute the cache ID for some data. */
struct gale_data cache_id(struct gale_group);
/** Raw version of cache_id(). */
struct gale_data cache_id_raw(struct gale_data);

/** Attempt to find an item in the cache; returns true iff successful. */
int cache_find(struct gale_data id,struct gale_group *data);
/** Raw version of cache_find_raw(). */
int cache_find_raw(struct gale_data id,struct gale_data *data);

/** Store an item in the cache (if possible), optionally returning a file. */
void cache_store(struct gale_data id,struct gale_group data);
/** Raw version of cache_store(). */
void cache_store_raw(struct gale_data id,struct gale_data data);
/*@}*/

#endif
