#include "gale/client.h"
#include "gale/crypto.h"
#include "gale/key.h"
#include "oop.h"

struct deffind {
	oop_source *oop;
	gale_call_location *func;
	void *user;
};

static void *on_key(oop_source *oop,struct gale_key *key,void *x) {
	struct deffind *find = (struct deffind *) x;
	gale_find_exact_location(find->oop,
		gale_key_name(key),
		find->func,find->user);
	return OOP_CONTINUE;
}

static void *on_location(struct gale_text n,struct gale_location *l,void *x) {
	struct deffind *find = (struct deffind *) x;
	if (NULL != l) {
		if (NULL == gale_key_private(gale_location_key(l))) {
			gale_alert(GALE_WARNING,gale_text_concat(3,
				G_("no private key for \""), 
				gale_location_name(l), G_("\"")),0);
			gale_alert(GALE_NOTICE,
				G_("found a public key; not generating new keys"),0);
			gale_alert(GALE_NOTICE,gale_text_concat(3,
				G_("to make new public and private keys, run: gkgen \""),
				gale_location_name(l),G_("\"")),0);
		}

		return find->func(n,l,find->user);
	}

	gale_key_generate(find->oop,
		gale_key_handle(n),gale_crypto_generate(n),
		on_key,find);
	return OOP_CONTINUE;
}

/** Look up the default user location.
 *  Start looking up the local user's default "personal" location.  When the
 *  lookup is complete (whether it succeeded or failed), the supplied callback
 *  is invoked.
 *  \param oop Liboop event source to use.
 *  \param func Function to call when location lookup completes.
 *  \param user User-defined parameter to pass the function.
 *  \sa gale_find_location() */
void gale_find_default_location(oop_source *oop,
        gale_call_location *func,void *user)
{
	struct deffind *find;
	gale_create(find);
	find->oop = oop;
	find->func = func;
	find->user = user;
	gale_find_location(oop,gale_var(G_("GALE_ID")),on_location,find);
}
