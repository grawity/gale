#include "gale/key.h"
#include "key_i.h"

static struct gale_map **key_map = NULL;

static struct gale_text signer(struct gale_text name) {
	int dot,at;
	for (at = name.l - 1; at >= 0 && '@' != name.p[at]; --at) ;
	if (at >= 0) {
		for (dot = at - 1; dot >= 0 && '.' != name.p[dot]; --dot) ;
		if (dot < 0) return gale_text_right(name,-at - 1);
		return gale_text_concat(2,
			gale_text_left(name,dot),
			gale_text_right(name,-at));
	}

	for (dot = 0; dot < name.l && '.' != name.p[dot]; ++dot) ;
	if (dot < name.l)
		return gale_text_right(name,-dot - 1);
	return G_("ROOT");
}

/** Get a key handle.
 *  \param name Name of the key.
 *  \return Key handle. */
struct gale_key *gale_key_handle(struct gale_text name) {
	struct gale_key *key;

	if (NULL == key_map) {
		key_map = gale_malloc_safe(sizeof(*key_map));
		*key_map = gale_make_map(1);
	}

	key = (struct gale_key *) 
		gale_map_find(*key_map,gale_text_as_data(name));
	if (NULL == key) {
		struct gale_text s = signer(name);

		gale_create(key);
		key->name = name;
		key->public = NULL;
		key->private = NULL;
		key->search = NULL;
		key->signer = gale_text_compare(s,name) 
		            ? gale_key_handle(s)
		            : NULL;

		gale_map_add(*key_map,gale_text_as_data(key->name),key);
	}

	return key;
}

/** Get a key's parent.
 *  \param key Handle from gale_key_handle().
 *  \return Handle of the key's parent, or NULL if the key is ROOT. */
struct gale_key *gale_key_parent(struct gale_key *key) {
	return key ? key->signer : NULL;
}

/** Get a key's name.
 *  \param key Handle from gale_key_handle() or gale_key_parent().
 *  \return Key's name, as passed to gale_key_handle(). */
struct gale_text gale_key_name(struct gale_key *key) {
	return key ? key->name : null_text;
}
