#include "gale/client.h"
#include "gale/key.h"
#include "gale/crypto.h"

#include "client_i.h"

#include <assert.h>

struct unpack {
	gale_call_message *func;
	void *user;
	struct gale_message *message;
	int is_sealed;
	int from_count,to_count,count;
};

struct unpack_key {
	struct gale_location **store;
	struct unpack *unpack;
};

static void compress(struct gale_location **list,int count) {
	int i = 0,j = 0;
	if (NULL == list) return;
	for (i = 0; i < count; ++i) if (NULL != list[i]) list[j++] = list[i];
}

static gale_call_location on_loc;

static void *on_unpack(oop_source *oop,struct timeval now,void *x) {
	struct unpack *ctx = (struct unpack *) x;
	const struct gale_text *sender = gale_crypto_sender(ctx->message->data);

	assert(0 == ctx->count);

	/* Reconstruct 'from' array from signatures. */

	{
		const struct gale_data *bundled = 
			gale_crypto_bundled(ctx->message->data);
		while (NULL != bundled && 0 != bundled->l)
			gale_key_assert(*bundled++,0);
	}

	if (NULL != sender && 0 != sender[0].l) {
		int i;

		assert(0 == ctx->from_count);
		do
			++(ctx->from_count);
		while (NULL != sender && 0 != sender[ctx->from_count].l);

		gale_create_array(ctx->message->from,1 + ctx->from_count);
		ctx->message->from[ctx->from_count] = NULL;

		i = 0;
		++(ctx->count);
		while (NULL != sender && 0 != sender[i].l) {
			struct unpack_key *key;
			++(ctx->count);
			gale_create(key);
			key->unpack = ctx;
			key->store = &ctx->message->from[i];
			gale_find_exact_location(oop,sender[i++],on_loc,key);
		}

		if (0 != --(ctx->count)) return OOP_CONTINUE;
	}

	assert(0 == ctx->count);

	/* Remove NULL entries from 'from' and 'to' arrays. */

	compress(ctx->message->from,ctx->from_count);
	compress(ctx->message->to,ctx->to_count);
	if (NULL == ctx->message
	||  NULL == ctx->message->to 
	||  NULL == ctx->message->to[0])
		return ctx->func(NULL,ctx->user);

	if (NULL == ctx->message->from) {
		gale_create(ctx->message->from);
		ctx->message->from[0] = NULL;
	}

	/* Verify signatures. */

	if (NULL != ctx->message->from
	&&  NULL != ctx->message->from[0]) {
		int i;
		struct gale_group *keys;

		for (i = 0; NULL != ctx->message->from[i]; ++i) ;
		gale_create_array(keys,i);
		for (i = 0; NULL != ctx->message->from[i]; ++i)
			keys[i] = gale_key_data(gale_key_public(
				gale_location_key(ctx->message->from[i]),
				gale_time_now()));

		if (!gale_crypto_verify(i,keys,ctx->message->data))
			return ctx->func(NULL,ctx->user);

		ctx->message->data = gale_crypto_original(ctx->message->data);
	}

	return ctx->func(ctx->message,ctx->user);
}

static void *on_loc(struct gale_text name,struct gale_location *l,void *x) {
	struct unpack_key *key = (struct unpack_key *) x;
	*(key->store) = l;
	if (0 == --(key->unpack->count))
		return on_unpack(NULL,OOP_TIME_NOW,key->unpack);
	return OOP_CONTINUE;
}

static void *on_key(oop_source *oop,struct gale_key *key,void *x) {
	struct unpack *ctx = (struct unpack *) x;
	if (ctx->is_sealed) {
		const struct gale_key_assertion *ass = gale_key_private(key);
		if (NULL != ass
		&&  gale_crypto_open(gale_key_data(ass),&ctx->message->data))
			ctx->is_sealed = 0;
	}

	if (0 == --(ctx->count)) return on_unpack(NULL,OOP_TIME_NOW,ctx);
	return OOP_CONTINUE;
}

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
        gale_call_message *func,void *user)
{
	struct unpack *ctx;

	gale_create(ctx);
	ctx->func = func;
	ctx->user = user;
	gale_create(ctx->message);
	ctx->message->data = gale_group_empty();
	ctx->message->from = NULL;
	ctx->message->to = NULL;
	ctx->is_sealed = 0;
	ctx->from_count = 0;
	ctx->to_count = 0;
	ctx->count = 1;

	{
		struct gale_data copy = pack->content;
		if (!gale_unpack_group(&copy,&ctx->message->data)) {
			gale_alert(GALE_WARNING,gale_text_concat(3,
				G_("error decoding message on \""),
				pack->routing,G_("\"")),0);
			oop->on_time(oop,OOP_TIME_NOW,on_unpack,ctx);
			return;
		}
	}

	/* Reconstruct 'to' array from routing string. */

	{
		int i = 0;
		struct gale_text cat = null_text;
		while (gale_text_token(pack->routing,':',&cat)) ++i;
		gale_create_array(ctx->message->to,1 + i);

		cat = null_text;
		while (gale_text_token(pack->routing,':',&cat)) {
			int slash = 1;
			struct unpack_key *key;
			struct gale_text local,domain,name,part;
			if (0 == cat.l || '@' != cat.p[0]) 
				continue;

			for (; slash < cat.l && cat.p[slash] != '/'; ++slash) ;
			domain = gale_text_left(cat,slash);
			local = gale_text_right(cat,-slash);
			if (gale_text_compare(gale_text_left(local,6),G_("/user/")))
				continue;

			name = part = null_text;
			local = gale_text_right(local,-6);
			if ('/' == local.p[local.l - 1]) --local.l;
			while (gale_text_token(local,'/',&part))
				name = (0 == name.l) ? part
				     : gale_text_concat(3,name,G_("."),part);

			++(ctx->count);
			gale_create(key);
			key->unpack = ctx;
			key->store = &ctx->message->to[ctx->to_count++];
			gale_find_exact_location(oop,
				gale_text_concat(2,name,domain),on_loc,key);
		}

		ctx->message->to[ctx->to_count] = NULL;
		if (0 == ctx->to_count)
			gale_alert(GALE_WARNING,gale_text_concat(3,
				G_("incompatible message routing \""),
				pack->routing,G_("\"")),0);
	}

	/* Find keys to decrypt message, if necessary. */

	{
		const struct gale_text *sender = 
			gale_crypto_target(ctx->message->data);
		if (NULL != sender)
			ctx->is_sealed = 1;
		while (NULL != sender && 0 != sender->l) {
			++(ctx->count);
			gale_key_search(oop,
				gale_key_handle(*sender++),search_all,
				on_key,ctx);
		}
	}

	if (0 == --(ctx->count))
		oop->on_time(oop,OOP_TIME_NOW,on_unpack,ctx);
}
