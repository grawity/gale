#include "key_i.h"
#include "gale/crypto.h"
#include "gale/core.h"
#include "gale/client.h"

#include <assert.h>

struct cache {
	oop_source *oop;
	struct gale_key *key;
	struct gale_key_request *handle;
	struct gale_link *link;
	struct gale_server *server;
	struct gale_text local,domain;
	struct gale_message *query_message;
	struct gale_text key_routing;
	struct gale_time last_attempt;
	struct gale_time last_refresh;
	int waiting_for_query;
};

static const int timeout_interval = 20;
static const int retry_interval = 300;
static const int refresh_interval = 86400;

static oop_call_time on_timeout;

static void *on_packed_query(struct gale_packet *packet,void *x) {
	struct cache *cache = (struct cache *) x;
	packet->routing = gale_text_concat(7,
		packet->routing,G_(":"),G_("@"),
		gale_text_replace(gale_text_replace(cache->domain,
			G_(":"),G_("..")),
			G_("/"),G_(".|")),
		G_("/auth/query/"),
		gale_text_replace(cache->local,G_(":"),G_("..")),G_("/"));

	link_put(cache->link,packet);
	return OOP_CONTINUE;
}

static void *on_connect(struct gale_server *s,
	struct gale_text h,struct sockaddr_in a,void *x)
{
	struct cache *cache = (struct cache *) x;
	assert(s == cache->server);

	if (0 != cache->key_routing.l) 
		link_subscribe(cache->link,cache->key_routing);

	if (!(cache->waiting_for_query = (NULL == cache->query_message)))
		gale_pack_message(
			cache->oop,cache->query_message,
			on_packed_query,cache);

	return OOP_CONTINUE;
}

static void end_search(struct cache *cache) {
	struct gale_key_request * const handle = cache->handle;
	if (NULL != handle) {
		cache->handle = NULL;
		gale_key_hook_done(cache->oop,cache->key,handle);
	}
}

static void *on_ignore(oop_source *oop,struct gale_key *key,void *x) {
	return OOP_CONTINUE;
}

static void *on_timeout(oop_source *oop,struct timeval when,void *x) {
	struct cache * const cache = (struct cache *) x;
	const struct gale_time now = gale_time_now();
	const struct gale_key_assertion * const ass = 
		gale_key_public(cache->key,now);
	cache->waiting_for_query = 0;

	if (NULL != cache->handle) {
		gale_alert(GALE_WARNING,gale_text_concat(3,
			G_("cannot find \""),
			gale_key_name(cache->key),G_("\", giving up")),0);
		end_search(cache);
	}

	if (NULL != cache->server) {
		gale_close(cache->server);
		cache->server = NULL;
	}

	if (NULL != ass) {
		/* Update the timestamp if we didn't find anything. */
		if (!gale_time_compare(cache->last_refresh,gale_key_time(ass)))
			gale_key_retract(gale_key_assert(
				gale_key_raw(ass),gale_key_from(ass),now,0),0);

		/* Push the new timestamp into the rest of our world. */
		gale_key_search(oop,cache->key,
			(search_all & ~search_private & ~search_slow),
			on_ignore,NULL);
	}

	cache->oop = NULL;
	return OOP_CONTINUE;
}

static void *on_packet(struct gale_link *l,struct gale_packet *packet,void *x) {
	struct gale_group group,original;
	struct gale_fragment frag;
        struct gale_text from;
	struct gale_time now = gale_time_now(),then;
	const struct gale_data *bundled;
	struct cache *cache = (struct cache *) x;
	struct gale_key *signer = gale_key_parent(cache->key);
	struct gale_data copy = packet->content;
	if (!gale_unpack_group(&copy,&group)) {
		gale_alert(GALE_WARNING,gale_text_concat(3,
			G_("error decoding message on \""),
			packet->routing,G_("\"")),0);
		return OOP_CONTINUE;
	}

	original = gale_crypto_original(group);
	if (gale_group_lookup(original,G_("id/time"),frag_time,&frag))
		then = frag.value.time;
	else
		then = now;

        if (gale_group_lookup(original,G_("id/instance"),frag_text,&frag))
                from = frag.value.text;
        else
                from = G_("(unknown)");

	bundled = gale_crypto_bundled(group);
	while (NULL != bundled && 0 != bundled->l)
		gale_key_assert(*bundled++,gale_text_concat(2,
                            G_("bundled with AKD response from "),from),then,0);

	if (gale_group_lookup(original,G_("answer/key"),frag_data,&frag)
	||  gale_group_lookup(original,G_("answer.key"),frag_data,&frag)) {
		gale_key_assert(frag.value.data,gale_text_concat(2,
                            G_("in AKD response from "),from),then,0);
        }

	if (NULL != gale_key_public(cache->key,now))
		end_search(cache);

	if (NULL != signer && NULL != cache->handle
	&& (gale_group_lookup(original,G_("answer/key/error"),frag_text,&frag)
	||  gale_group_lookup(original,G_("answer.key.error"),frag_text,&frag)))
	{
		const struct gale_key_assertion *pub;
		pub = gale_key_public(signer,now);
		if (NULL != pub) {
			struct gale_group verify = gale_key_data(pub);
			if (gale_crypto_verify(1,&verify,group)) {
				gale_alert(GALE_WARNING,frag.value.text,0);
				end_search(cache);
			}
		}
	}

	return OOP_CONTINUE;
}

static void *on_query_location(
	struct gale_text name,
	struct gale_location *loc,void *x)
{
	struct cache * const cache = (struct cache *) x;
	struct gale_fragment frag;

	gale_create(cache->query_message);
	cache->query_message->data = gale_group_empty();

	frag.type = frag_text;
	frag.name = G_("question.key");
	frag.value.text = gale_key_name(cache->key);
	gale_group_add(&cache->query_message->data,frag);

	frag.type = frag_text;
	frag.name = G_("question/key");
	frag.value.text = gale_text_concat(3,
		cache->local,G_("@"),cache->domain);
	gale_group_add(&cache->query_message->data,frag);

	gale_create_array(cache->query_message->to,2);
	cache->query_message->to[0] = loc;
	cache->query_message->to[1] = NULL;
	cache->query_message->from = NULL;

	if (cache->waiting_for_query) {
		cache->waiting_for_query = 0;
		gale_pack_message(
			cache->oop,cache->query_message,
			on_packed_query,cache);
	}

	return OOP_CONTINUE;
}

static void *on_key_location(
	struct gale_text name,
	struct gale_location *loc,void *x)
{
	struct gale_location *list[] = { loc, NULL };
	const struct gale_text r = gale_pack_subscriptions(list,NULL);
	struct cache * const cache = (struct cache *) x;

	assert(NULL != loc && 0 != r.l); /* _gale is built in! */
	cache->key_routing = gale_text_concat(6,r,G_(":"),
		G_("@"),
		gale_text_replace(gale_text_replace(cache->domain,
			G_(":"),G_("..")),
			G_("/"),G_(".|")),
		G_("/auth/key/"),
		gale_text_replace(cache->local,G_(":"),G_("..")));
	link_subscribe(cache->link,cache->key_routing);
	return OOP_CONTINUE;
}

static void on_search(struct gale_time now,oop_source *oop,
	struct gale_key *key,int flags,
	struct gale_key_request *handle,
	void *x,void **ptr)
{
	const struct gale_text key_name = gale_key_name(key);
	struct cache *cache = (struct cache *) *ptr;
	const struct gale_key_assertion *old;
	struct timeval timeout;

	if (!(flags & search_slow)
	|| !gale_text_compare(gale_text_left(key_name,6),G_("_gale."))
	|| !gale_text_compare(gale_text_left(key_name,6),G_("_gale@"))) {
	skip:
		gale_key_hook_done(oop,key,handle);
		return;
	}

	if (NULL == cache) {
		int at;
		const struct gale_text name = key_i_swizzle(key_name);
		for (at = name.l - 1; at >= 0 && '@' != name.p[at]; --at) ;
		if (at < 0 || at == name.l - 1) goto skip;

		gale_create(cache);
		cache->oop = NULL;
		cache->key = key;
		cache->handle = NULL;
		cache->server = NULL;
		cache->local = gale_text_left(name,at);
		cache->domain = gale_text_right(name,-at - 1);
		cache->query_message = NULL;
		cache->key_routing = null_text;
		cache->last_attempt = gale_time_zero();
		cache->last_refresh = gale_time_zero();
		cache->waiting_for_query = 0;
		cache->link = new_link(oop);
		*ptr = cache;

		link_on_message(cache->link,on_packet,cache);
		gale_find_exact_location(oop,gale_text_concat(2,
			G_("_gale.query."),key_name),
			on_query_location,cache);
		gale_find_exact_location(oop,gale_text_concat(2,
			G_("_gale.key."),key_name),
			on_key_location,cache);
	}

	/* Don't perform AKD too often. */
	if (0 > gale_time_compare(now,gale_time_add(
		cache->last_attempt,
		gale_time_seconds(retry_interval)))) goto skip;

	old = gale_key_public(key,now);
	if (NULL != old && !(flags & search_harder)) {
		struct gale_data random = gale_crypto_random(sizeof(unsigned));
		const unsigned variant = refresh_interval +
			(* (unsigned *) random.p) % refresh_interval;

		cache->last_refresh = gale_key_time(old);
		if (0 < gale_time_compare(cache->last_refresh,
			gale_time_diff(now,gale_time_seconds(variant))))
			goto skip;

		gale_alert(GALE_NOTICE,gale_text_concat(3,
			G_("refreshing \""),
			key_name,G_("\"")),0);
		gale_key_hook_done(oop,key,handle);
		handle = NULL;
	}

	/* BUG?  We're assuming the timeout is actually scheduled... */
	assert(NULL == cache->oop 
	    && NULL == cache->handle 
	    && NULL == cache->server);
	cache->oop = oop;
	cache->handle = handle;
	cache->server = gale_make_server(oop,cache->link,null_text,0);
	gale_on_connect(cache->server,on_connect,cache);

	cache->last_attempt = now;
	gale_time_to(&timeout,now);
	timeout.tv_sec += timeout_interval;
	oop->on_time(oop,timeout,on_timeout,cache);

	gale_alert(GALE_NOTICE,gale_text_concat(3,
		G_("requesting key \""),key_name,G_("\"")),0);
}

void key_i_init_akd(void) {
	gale_key_add_hook(on_search,NULL);
}
