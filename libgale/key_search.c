#include "key_i.h"
#include "gale/key.h"

#include <assert.h>

struct key_callback {
	gale_key_call *func;
	void *user;
	struct key_callback *next;
};

struct gale_key_request {
	void *cache;
	const struct gale_key_assertion *last_public,*last_private;
	int is_active,flags;
	struct gale_key_request *next;
};

struct gale_key_search {
	struct key_callback *chain;
	struct gale_key_request *status;
	struct gale_time last;
	int last_flags;
	int in_wakeup;
};

struct key_hook {
	gale_key_hook *func;
	void *user;
	struct key_hook *next;
};

static const int retry_interval = 5;
static const int refresh_flag = 0x10000000;

static struct key_hook **hook_list = NULL;

static void wakeup(oop_source *oop,struct gale_key *key) {
	struct gale_time now;
	struct key_hook *hook;
	int is_active,is_stable;

	if (key->search->in_wakeup) return;
	key->search->in_wakeup = 1;
	now = gale_time_now();

	do {
		struct gale_key_request **req = &key->search->status;
		const struct gale_key_assertion *pub = gale_key_public(key,now);
		const struct gale_key_assertion *pri = gale_key_private(key);

		is_active = 0;
		is_stable = 1;
		hook = hook_list ? *hook_list : NULL;
		while (NULL != hook) {
			if (NULL == *req) {
				gale_create(*req);
				(*req)->cache = NULL;
				(*req)->last_public = NULL;
				(*req)->last_private = NULL;
				(*req)->is_active = 0;
				(*req)->flags = key->search->last_flags;
				(*req)->next = NULL;
				assert(0 != (*req)->flags);
			}

			if ((*req)->is_active)
				is_active = 1;
			else 
			if (0 != (*req)->flags
			||  pub != (*req)->last_public
			||  pri != (*req)->last_private) {
				const int flags = (*req)->flags;
				is_stable = 0;

				if ((NULL == pub && NULL != (*req)->last_public)
				||  (NULL == pri && NULL != (*req)->last_private))
					(*req)->cache = NULL;

				(*req)->last_public = pub;
				(*req)->last_private = pri;
				(*req)->is_active = 1;
				(*req)->flags = 0;
				hook->func(now,oop,
					key,flags,*req,
					hook->user,&(*req)->cache);
			}

			hook = hook->next;
			req = &(*req)->next;
		}
	} while (!is_stable);

	key->search->in_wakeup = 0;
	if (!is_active) {
		struct key_callback *call = key->search->chain;
		key->search->chain = NULL;
		while (NULL != call) {
			call->func(oop,key,call->user);
			call = call->next;
		}
	}
}

static void *search_chain(oop_source *oop,struct gale_key *key,void *user) {
	struct gale_key *search = (struct gale_key *) user;
	wakeup(oop,search);
	return OOP_CONTINUE;
}

/** Search for key data.
 *  \param source Liboop event source to use.
 *  \param key Key handle from gale_key_handle().
 *  \param flags Control flags, normally search_all.
 *  \param call Callback to invoke when the search is complete.
 *  \param user User-specified opaque pointer to pass to the callback. */
void gale_key_search(oop_source *source,
	struct gale_key *key,int flags,
	gale_key_call *call,void *user) 
{
	const struct gale_time now = gale_time_now();
	struct gale_key *parent = NULL;
	struct key_callback *callback;
	struct gale_key_request *req;

	if (NULL == key->search) {
		gale_create(key->search);
		key->search->chain = NULL;
		key->search->status = NULL;
		key->search->last = gale_time_zero();
		key->search->last_flags = 0;
		key->search->in_wakeup = 0;
	}

	gale_create(callback);
	callback->func = call;
	callback->user = user;
	callback->next = key->search->chain;
	key->search->chain = callback;

	if (0 < gale_time_compare(now,gale_time_add(
		key->search->last,
		gale_time_seconds(retry_interval))))
		key->search->last_flags = 0;

	flags |= refresh_flag | key->search->last_flags;
	if (flags != key->search->last_flags) {
		parent = gale_key_parent(key);
		key->search->last = now;
		key->search->last_flags = flags;
		for (req = key->search->status; NULL != req; req = req->next)
			req->flags |= flags;
	}

	if (NULL == parent)
		search_chain(source,NULL,key);
	else
		gale_key_search(source,parent,0,search_chain,key);
}

/** Add a search strategy hook that will be called when looking for keys.
 *  \param hook Callback to invoke whenever the system is looking for a key.
 *  \param user User-specified opaque pointer to pass to the callback. */
void gale_key_add_hook(gale_key_hook *hook,void *user) {
	struct key_hook **ptr;
	if (NULL == hook_list) {
		hook_list = gale_malloc_safe(sizeof(*hook_list));
		*hook_list = NULL;
	}

	ptr = hook_list;
	while (NULL != *ptr) ptr = &(*ptr)->next;
	gale_create(*ptr);
	(*ptr)->func = hook;
	(*ptr)->user = user;
	(*ptr)->next = NULL;
}

/** Notify the system that search is complete.
 *  \param source Liboop event source to use.
 *  \param key Key handle, as passed to gale_key_hook().
 *  \param handle Request handle, as passed to gale_key_hook(). */
void gale_key_hook_done(oop_source *source,
	struct gale_key *key,struct gale_key_request *handle) 
{
	assert(handle->is_active);
	handle->is_active = 0;
	wakeup(source,key);
}
