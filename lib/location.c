#include "gale/misc.h"
#include "gale/auth.h"
#include "gale/client.h"
#include "gale/globals.h"

#include "location.h"
#include "id.h"

#include <assert.h>

static oop_call_time on_complete;

/* XXX this is all super inefficient... */

static int find_key_test(struct auth_id **ptr,struct gale_text key_name) {
	init_auth_id(ptr,key_name);
	return auth_id_public(*ptr);
}

static struct auth_id *find_key_search(struct gale_text key_name) {
	struct auth_id *key;
	if (find_key_test(&key,key_name)) return key;

	if (key_name.l > 0 && '*' == key_name.p[0]) {
		key_name = gale_text_right(key_name,-1);
		if (key_name.l > 0) {
			if ('@' == key_name.p[0]) return NULL;
			if ('.' == key_name.p[0])
				key_name = gale_text_right(key_name,-1);
		}
	}

	while (0 != key_name.l && key_name.p[0] != '.' && key_name.p[0] != '@')
		key_name = gale_text_right(key_name,-1);

	return find_key_search(gale_text_concat(2,G_("*"),key_name));
}

static struct auth_id *find_key(struct gale_text key_name) {
	struct auth_id *key;
	disable_gale_akd();
	key = find_key_search(key_name);
	enable_gale_akd();
	if (NULL != key) return key;
	return find_key_search(key_name);
}

static void schedule(oop_source *oop,struct gale_location *loc) {
	if (loc->scheduled) return;
	loc->scheduled = 1;
	oop->on_time(oop,OOP_TIME_NOW,on_complete,loc);
}

static void *on_complete(oop_source *oop,struct timeval tv,void *user) {
	struct gale_location *loc = (struct gale_location *) user;
	void *ret = OOP_CONTINUE;

	loc->scheduled = 0;
	while (OOP_CONTINUE == ret && NULL != loc->list) {
		struct gale_location_callback *call = loc->list;
		loc->list = loc->list->next;
		ret = call->func(loc->name,
			(loc->successful ? loc : NULL),
			call->user);
	}

	if (NULL != loc->list) schedule(oop,loc);
	return ret;
}

static struct gale_text mangle_category(struct gale_text name) {
	struct gale_text ret,domain,pt = null_text,local = null_text;
	gale_text_token(name,'@',&local);
	domain = local;
	gale_text_token(name,'\0',&domain);
	ret = gale_text_concat(3,G_("@"),domain,G_("/user/"));
	while (gale_text_token(local,'.',&pt))
		ret = gale_text_concat(3,ret,pt,G_("/"));
	return ret;
}

static struct gale_text mangle_auth_id(struct gale_text name) {
	struct gale_text domain,local = null_text,pt = null_text,r = null_text;
	gale_text_token(name,'@',&local);
	domain = local;
	gale_text_token(name,'\0',&domain);

	while (gale_text_token(local,'.',&pt))
		r = (0 == r.l) ? pt
		    : gale_text_concat(3,pt,G_("."),r);

	return gale_text_concat(3,r,G_("@"),domain);
}

static struct gale_text demangle_auth_id(struct gale_text name) {
	/* reversal is its own inverse */
	return mangle_auth_id(name);
}

static void *on_root(struct gale_text name,struct gale_location *root,void *x) {
	struct gale_location *loc = (struct gale_location *) x;
	if (NULL != root) {
		loc->root = root;
		loc->successful = 1;
	}
	return on_complete(NULL,OOP_TIME_NOW,loc);
}

void gale_find_exact_location(oop_source *oop,struct gale_text name,
	gale_call_location *func,void *user) 
{
	struct gale_location *loc;
	struct gale_location_callback *call;

	/* TODO: validate syntax */

	if (NULL == gale_global->location_tree)
		gale_global->location_tree = gale_make_map(1);

	loc = (struct gale_location *)
	gale_map_find(gale_global->location_tree,gale_text_as_data(name));
	if (NULL == loc) {
		gale_create(loc);
		loc->scheduled = 0;
		loc->name = name;
		loc->root = loc; /* XXX */
		loc->list = NULL;

		/* TODO: This part should really be asynchronous */
		/* init_auth_id(&loc->key,mangle_auth_id(name)); */
		loc->routing = mangle_category(name);
		loc->key = find_key(mangle_auth_id(name));
		if (NULL != loc->key) {
			const struct gale_text auth = auth_id_name(loc->key);
			const struct gale_text found = demangle_auth_id(auth);
			if (0 == gale_text_compare(name,found))
				loc->successful = 1;
			else {
				gale_find_exact_location(oop,found,on_root,loc);
				loc->scheduled = 1;
			}
		}
	}

	schedule(oop,loc);
	gale_create(call);
	call->func = func;
	call->user = user;
	call->next = loc->list;
	loc->list = call;
}

void gale_find_default_location(oop_source *oop,
	gale_call_location *func,void *user)
{
	gale_find_location(oop,auth_id_name(gale_user()),func,user);
}

struct gale_text gale_location_name(struct gale_location *loc) {
	return loc->name;
}

struct gale_group gale_location_public_data(struct gale_location *loc) {
	return loc->key->pub_data;
}

struct gale_group gale_location_private_data(struct gale_location *loc) {
	return loc->key->priv_data;
}

struct gale_location *gale_location_root(struct gale_location *loc) {
	return loc->root;
}

int gale_location_send_ok(struct gale_location *loc) {
	return 1;
}

int gale_location_receive_ok(struct gale_location *loc) {
	/* TODO: evaluate the whole group */
	return auth_id_private(loc->key);
}
