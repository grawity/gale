#include "key_i.h"
#include "gale/key.h"

static void builtin_hook(struct gale_time now,oop_source *oop,
	struct gale_key *key,int flags,
	struct gale_key_request *handle,
	void *user,void **p)
{
	const struct gale_text name = gale_key_name(key);
	struct gale_group data = gale_group_empty();
	struct gale_key_assertion ***cache = (struct gale_key_assertion ***) p;

	if (NULL != *cache && **cache == gale_key_public(key,now)) {
		gale_key_hook_done(oop,key,handle);
		return;
	}

	if (NULL == *cache) gale_create(*cache);
	**cache = NULL;

	if (!gale_text_compare(gale_text_left(name,8),G_("_gale.*@"))
	||  !gale_text_compare(gale_text_left(name,6),G_("_gale@"))) {
		struct gale_fragment frag;
		frag.type = frag_text;
		frag.name = G_("key.member");
		frag.value.text = G_("");
		gale_group_add(&data,frag);

		frag.type = frag_text;
		frag.name = G_("key.id");
		frag.value.text = name;
		gale_group_add(&data,frag);

		**cache = gale_key_assert_group(data,
			gale_time_seconds(989797178),1);
	}

	gale_key_hook_done(oop,key,handle);
}

void key_i_init_builtin(void) {
	gale_key_add_hook(builtin_hook,NULL);
}
