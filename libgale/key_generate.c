#include "gale/key.h"
#include "gale/crypto.h"
#include "gale/misc.h"

#include "key_i.h" /* for key_i_create() and key_i_swizzle() */

#include <assert.h>
#include <errno.h>

struct generation {
	struct gale_key *key;
	struct gale_group data;
	gale_key_call *call;
	void *user;
};

static void *finish(oop_source *oop,struct generation *gen,int do_public) {
	gale_key_assert_group(gen->data,1);
	if (do_public) gale_key_assert_group(gale_crypto_public(gen->data),1);
	/* TODO: store the key somewhere? */

	return gen->call(oop,gen->key,gen->user);
}

static void *on_parent(oop_source *oop,struct gale_key *key,void *user) {
	struct generation *gen = (struct generation *) user;
	const struct gale_time now = gale_time_now();
	const struct gale_key_assertion * const priv = gale_key_private(key);
	const struct gale_key_assertion * const pub = gale_key_public(key,now);

	if (NULL != priv) {
		/* Attempt to sign the key ourselves. */
		struct gale_group signer = gale_key_data(priv);
		struct gale_group signee = gale_crypto_public(gen->data);
		struct gale_fragment frag;

		frag.type = frag_data;
		frag.name = G_("key.source");
		frag.value.data = gale_key_raw(pub);
		gale_group_replace(&signer,frag);

		frag.type = frag_time;
		frag.name = G_("key.signed");
		frag.value.time = now;
		gale_group_replace(&signee,frag);

		if (gale_crypto_sign(1,&signer,&signee)) {
			struct gale_key_assertion *ass =
				gale_key_assert_group(signee,1);
			if (ass == gale_key_public(gen->key,now)
			&&  gale_key_owner(gale_key_signed(ass)) == key)
				return finish(oop,gen,0);
			gale_key_retract(ass,1);
		}

		gale_alert(GALE_WARNING,gale_text_concat(3,
			G_("couldn't sign \""),
			gale_key_name(gen->key),
			G_("\" directly")),0);
	}

	if (NULL != pub) {
		/* Try to use gksign. */
		const struct gale_text name = G_("gksign");
		const struct gale_data bits = key_i_create(gen->data);
		int in,out;

		gale_alert(GALE_NOTICE,gale_text_concat(3,
			G_("passing \""),
			gale_key_name(gen->key),
			G_("\" through gksign")),0);

		/* TODO: make this asynchronous */
		gale_exec(oop,name,1,&name,&in,&out,NULL,NULL,NULL);
		if (!gale_write_to(in,bits)) {
			close(in);
			close(out);
			gale_alert(GALE_WARNING,G_("couldn't write key"),errno);
		} else {
			struct gale_data newbits;
			struct gale_key_assertion *ass;

			close(in);
			newbits = gale_read_from(out,0);
			close(out);

			ass = gale_key_assert(newbits,1);
			if (ass == gale_key_public(gen->key,now)
			&&  gale_key_owner(gale_key_signed(ass)) == key)
				return finish(oop,gen,0);
		}
	}

	gale_alert(GALE_WARNING,gale_text_concat(3,
		G_("couldn't sign \""),gale_key_name(gen->key),G_("\"")),0);
	return finish(oop,gen,1);
}

static void *on_delay(oop_source *oop,struct timeval when,void *user) {
	return finish(oop,(struct generation *) user,1);
}

/** Generate a new key.
 *  \param source Liboop event source to use.
 *  \param key Key handle from gale_key_handle().
 *  \param extra Fragments (like key.owner) to add to key data, if any.
 *  \param call Callback to invoke when generation is complete.
 *  \param user User-specified opaque pointer to pass to the callback. */
void gale_key_generate(oop_source *source,
        struct gale_key *key,struct gale_group extra,
        gale_key_call *call,void *user)
{
	struct gale_key * const parent = gale_key_parent(key);
	struct generation *gen;

	gale_create(gen);
	gen->key = key;
	gen->data = gale_crypto_generate(gale_key_name(key));
	gen->call = call;
	gen->user = user;
	gale_group_append(&gen->data,extra);

	if (NULL != parent)
		gale_key_search(source,parent,search_private,on_parent,gen);
	else
		source->on_time(source,OOP_TIME_NOW,on_delay,gen);
}
