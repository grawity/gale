#include "common.h"
#include "id.h"

#include "gale/misc.h"
#include "gale/globals.h"

#include <assert.h>
#include <stdlib.h>
#include <stdarg.h>

void _ga_warn_id(struct gale_text text,...) {
	struct gale_text out,tok = null_text;
	va_list ap;

	tok = null_text;
	gale_text_token(text,'%',&tok);
	out = tok;

	va_start(ap,text);
	while (gale_text_token(text,'%',&tok)) {
		struct auth_id *id = va_arg(ap,struct auth_id *);
		out = gale_text_concat(3,out,auth_id_name(id),tok);
	}
	va_end(ap);

	gale_alert(GALE_WARNING,out,0);
}

void init_auth_id(struct auth_id **pid,struct gale_text name) {
	struct auth_id *ptr;

	if (NULL == gale_global->auth_tree)
		gale_global->auth_tree = gale_make_wt(1);

	ptr = (struct auth_id *) 
	gale_wt_find(gale_global->auth_tree,gale_text_as_data(name));
	if (NULL == ptr) {
		gale_dprintf(11,"(auth) new key: \"%s\"\n",
		             gale_text_to(gale_global->enc_console,name));
		gale_create(ptr);
		ptr->name = name;
		ptr->priv_time_fast = gale_time_zero();
		ptr->priv_time_slow = gale_time_zero();
		ptr->priv_data = gale_group_empty();
		ptr->priv_inode = _ga_init_inode();

		ptr->pub_time_fast = gale_time_zero();
		ptr->pub_time_slow = gale_time_zero();
		ptr->pub_data = gale_group_empty();
		ptr->pub_orig = null_data;
		ptr->pub_signer = NULL;
		ptr->pub_inode = _ga_init_inode();
		ptr->pub_trusted = 0;

		gale_wt_add(gale_global->auth_tree,gale_text_as_data(name),ptr);
	}

	*pid = ptr;
}

struct gale_text auth_id_name(struct auth_id *id) {
	return id->name;
}

struct gale_text auth_id_comment(struct auth_id *id) {
	struct gale_fragment frag;

	auth_id_public(id);
	if (!gale_group_lookup(id->pub_data,G_("key.owner"),frag_text,&frag))
		return null_text;
	return frag.value.text;
}
