/** \file
 *  Key knowledge base management. */

#ifndef GALE_KEY_H
#define GALE_KEY_H

#include "gale/misc.h"
#include "oop.h"

struct gale_key;
struct gale_key_assertion;
struct gale_key_request;

struct gale_key *gale_key_handle(struct gale_text name);
struct gale_key *gale_key_parent(struct gale_key *);
struct gale_text gale_key_name(struct gale_key *);
const struct gale_key_assertion *gale_key_private(struct gale_key *);
const struct gale_key_assertion *gale_key_public(
	struct gale_key *,
	struct gale_time);

struct gale_key_assertion *gale_key_assert(
	struct gale_data,struct gale_time,int trust);
struct gale_key_assertion *gale_key_assert_group(
	struct gale_group,struct gale_time,int trust);
void gale_key_retract(struct gale_key_assertion *,int trust);

int gale_key_trusted(const struct gale_key_assertion *);
struct gale_key *gale_key_owner(const struct gale_key_assertion *);
const struct gale_key_assertion *gale_key_signed(
	const struct gale_key_assertion *);

struct gale_group gale_key_data(const struct gale_key_assertion *);
struct gale_data gale_key_raw(const struct gale_key_assertion *);
struct gale_time gale_key_time(const struct gale_key_assertion *);

/** Callback for gale_key_search() and gale_key_generate().
 *  \param key Key handle for which search was initiated.
 *  \param user User-specified opaque pointer.
 *  \return Liboop continuation code (usually OOP_CONTINUE). */
typedef void *gale_key_call(oop_source *oop,struct gale_key *key,void *user);

enum { search_private = 1, search_slow = 2, search_all = 3, search_harder = 4 };
void gale_key_search(oop_source *source,
	struct gale_key *,int flags,
	gale_key_call *,void *user);

void gale_key_generate(oop_source *source,
	struct gale_key *,struct gale_group,
	gale_key_call *,void *user);

/** Callback for key search strategy drivers.
 *  \param now The current time (to avoid redundant calls to gale_time_now()).
 *  \param oop Liboop event source to use.
 *  \param key Key handle for which search was initiated.
 *  \param do_private Zero if we're only interested in public key data.
 *  \param handle Request handle for use with gale_key_hook_done().
 *  \param user User-specified opaque pointer.
 *  \warning Make sure to call gale_key_hook_done() when the callback is
 *           finished with its search.
 *  \sa gale_key_add_hook(), gale_key_hook_done(). */
typedef void gale_key_hook(
	struct gale_time now,oop_source *oop,
	struct gale_key *key,int do_private,
	struct gale_key_request *handle,
	void *user,void **cache);

void gale_key_add_hook(gale_key_hook *,void *user);
void gale_key_hook_done(oop_source *,
	struct gale_key *,struct gale_key_request *);

#endif
