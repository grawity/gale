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

	gale_alert(GALE_WARNING,gale_text_to_local(out),0);
}

void init_auth_id(struct auth_id **pid,struct gale_text name) {
	struct auth_id *ptr;

	if (NULL == gale_global->auth_tree)
		gale_global->auth_tree = gale_make_wt(1);

	ptr = (struct auth_id *) 
	gale_wt_find(gale_global->auth_tree,gale_text_as_data(name));
	if (NULL == ptr) {
		gale_dprintf(11,"(auth) new key: \"%s\"\n",
		             gale_text_to_local(name));
		gale_create(ptr);
		ptr->version = 0;
		ptr->trusted = 0;
		ptr->sign_time = gale_time_zero();
		ptr->expire_time = gale_time_forever();
		ptr->find_time = gale_time_zero();
		ptr->name = name;
		ptr->comment = G_("(uninitialized)");
		ptr->source = _ga_init_inode();
		ptr->public = NULL;
		ptr->private = NULL;
		_ga_init_sig(&ptr->sig);
		gale_wt_add(gale_global->auth_tree,gale_text_as_data(name),ptr);
	}

	*pid = ptr;
}

struct gale_text auth_id_name(struct auth_id *id) {
	return id->name;
}

struct gale_text auth_id_comment(struct auth_id *id) {
	auth_id_public(id);
	return id->comment;
}
