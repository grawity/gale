#include "key_i.h"
#include "gale/crypto.h"
#include "gale/core.h"

#include <assert.h>

struct cache {
	oop_source *oop;
	struct timeval timeout;
	struct gale_key *key;
	struct gale_key_request *handle;
	struct gale_link *link;
	struct gale_server *server;
	struct gale_text local,domain;
};

static const int timeout_interval = 20;
static const int retry_interval = 300;

static oop_call_time on_timeout;

static void *on_connect(struct gale_server *s,
	struct gale_text h,struct sockaddr_in a,void *x)
{
	struct cache *cache = (struct cache *) x;
	struct gale_packet *req;
	struct gale_group group;
	struct gale_fragment frag;

	assert(s == cache->server);
	link_subscribe(cache->link,gale_text_concat(4,
		G_("@"),cache->domain,
		G_("/auth/key/"),cache->local));

	frag.type = frag_text;
	frag.name = G_("question/key");
	frag.value.text = gale_text_concat(3,
		cache->local,G_("@"),cache->domain);

	group = gale_group_empty();
	gale_group_add(&group,frag);

	gale_create(req);
	req->content.p = gale_malloc(gale_group_size(group));
	req->content.l = 0;
	gale_pack_group(&req->content,group);
	req->routing = gale_text_concat(4,
		G_("@"),cache->domain,
		G_("/auth/query/"),cache->local);
	link_put(cache->link,req);
	return OOP_CONTINUE;
}

static void end_search(struct cache *cache) {
	oop_source *oop = cache->oop;
	struct gale_key_request *handle = cache->handle;

	if (NULL != oop) {
		oop->cancel_time(oop,cache->timeout,on_timeout,cache);
		cache->oop = NULL;
	}

	if (NULL != cache->server) {
		gale_close(cache->server);
		cache->server = NULL;
	}

	if (NULL != handle) {
		cache->handle = NULL;
		if (NULL != oop) gale_key_hook_done(oop,cache->key,handle);
	}
}

static void *on_timeout(oop_source *oop,struct timeval now,void *x) {
	struct cache *cache = (struct cache *) x;
	gale_alert(GALE_WARNING,gale_text_concat(3,
		G_("cannot find \""),
		gale_key_name(cache->key),G_("\", giving up")),0);
	end_search(cache);
	return OOP_CONTINUE;
}

static void *on_packet(struct gale_link *l,struct gale_packet *packet,void *x) {
	struct gale_group group,original;
	struct gale_fragment frag;
	struct gale_time now = gale_time_now();
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

	bundled = gale_crypto_bundled(group);
	while (NULL != bundled && 0 != bundled->l)
		gale_key_assert(*bundled++,0);

	original = gale_crypto_original(group);
	if (gale_group_lookup(original,G_("answer/key"),frag_data,&frag))
		gale_key_assert(frag.value.data,0);

	if (NULL != gale_key_public(cache->key,now)) 
		end_search(cache);

	if (gale_group_lookup(original,G_("answer/key/error"),frag_text,&frag)
	&&  NULL != signer) {
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

static void on_search(struct gale_time now,oop_source *oop,
	struct gale_key *key,int flags,
	struct gale_key_request *handle,
	void *x,void **ptr)
{
	struct cache *cache = (struct cache *) *ptr;
	struct gale_time last;
	struct gale_text name;
	int at = 0;

	if (NULL == cache) {
		name = key_i_swizzle(gale_key_name(key));
		for (at = 0; at < name.l && '@' != name.p[at]; ++at) ;
	}

	if (!(flags & search_slow)
	||  (NULL == cache && name.l == at) 
	||   NULL != gale_key_public(key,now)) {
		gale_key_hook_done(oop,key,handle);
		return;
	}

	if (NULL == cache) {
		gale_create(cache);
		cache->oop = NULL;
		cache->key = key;
		cache->handle = NULL;
		cache->server = NULL;
		cache->local = gale_text_left(name,at);
		cache->domain = gale_text_right(name,-at - 1);
		cache->timeout.tv_sec = 0;
		cache->timeout.tv_usec = 0;
		cache->link = new_link(oop);
		link_on_message(cache->link,on_packet,cache);
		*ptr = cache;
	}

	gale_time_from(&last,&cache->timeout);
	if (gale_time_compare(now,
		gale_time_add(last,gale_time_seconds(retry_interval))) < 0) 
	{
		gale_key_hook_done(oop,key,handle);
		return;
	}

	assert(NULL == cache->oop 
	    && NULL == cache->handle 
	    && NULL == cache->server);
	cache->oop = oop;
	cache->handle = handle;
	cache->server = gale_make_server(oop,cache->link,null_text,0);
	gale_on_connect(cache->server,on_connect,cache);

	gale_time_to(&cache->timeout,now);
	cache->timeout.tv_sec += timeout_interval;
	oop->on_time(oop,cache->timeout,on_timeout,cache);

	gale_alert(GALE_NOTICE,gale_text_concat(3,
		G_("requesting key \""),gale_key_name(key),G_("\"")),0);
}

void key_i_init_akd(void) {
	gale_key_add_hook(on_search,NULL);
}
