#include "common.h"
#include "id.h"

#include "gale/misc.h"

#include <assert.h>
#include <stdlib.h>
#include <stdarg.h>

static struct auth_id *root = NULL;

static void detach_id(void *vid,void *x) {
	struct auth_id *id = (struct auth_id *) vid;
	(void) x;

	if (!id->left) {
		*(id->ptr) = id->right;
		if (id->right) id->right->ptr = id->ptr;
	} else if (!id->right) {
		*(id->ptr) = id->left;
		if (id->left) id->left->ptr = id->ptr;
	} else {
		struct auth_id *sub = id->left;
		while (sub->right) sub = sub->right;
		detach_id(sub,NULL);
		*(id->ptr) = sub;
		sub->ptr = id->ptr;
		sub->left = id->left;
		if (sub->left) sub->left->ptr = &sub->left;
		sub->right = id->right;
		if (sub->right) sub->right->ptr = &sub->right;
	}
}

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
	struct auth_id **ptr = &root;
	int c = 0;

	while (*ptr) {
		c = gale_text_compare(name,(*ptr)->name);
		if (c < 0) ptr = &(*ptr)->left;
		else if (c > 0) ptr = &(*ptr)->right;
		else break;
	}

	if (*ptr == NULL) {
		struct auth_id *id;
		gale_create(id);
		id->version = 0;
		id->trusted = 0;
		id->sign_time = gale_time_zero();
		id->expire_time = gale_time_forever();
		id->find_time = gale_time_zero();
		id->name = name;
		id->comment = G_("(uninitialized)");
		id->source = _ga_init_inode();
		id->public = NULL;
		id->private = NULL;
		_ga_init_sig(&id->sig);
		id->left = id->right = NULL;
		id->ptr = ptr;
		*ptr = id;
		gale_finalizer(id,detach_id,NULL);
	}

	*pid = *ptr;
}

struct gale_text auth_id_name(struct auth_id *id) {
	return id->name;
}

struct gale_text auth_id_comment(struct auth_id *id) {
	auth_id_public(id);
	return id->comment;
}
