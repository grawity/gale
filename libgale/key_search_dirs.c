#include "key_i.h"
#include "gale/key.h"
#include "gale/globals.h"

#include <errno.h>

enum dir_type { public_dir, cache_dir, trusted_dir, private_dir };

struct dir_data {
	struct gale_text dir;
	enum dir_type type;
};

struct dir_filename {
	struct gale_text name;
	struct gale_file_state *state;
	struct gale_key_assertion *ass;
};

struct dir_cache {
	struct gale_time last;
	const struct gale_key_assertion *public_written,*private_written;
	struct dir_filename old,public,private;
};

static const int size_limit = 65536;
static const int poll_interval = 10;

static void get_file(int trust,struct dir_filename *f) {
	if (NULL == f->state || gale_file_changed(f->state)) {
		struct gale_key *owner = gale_key_owner(f->ass);
		struct gale_data d = gale_read_file(
			f->name,size_limit,!trust,&f->state);
		gale_key_retract(f->ass,trust);
		if (0 == d.l)
			f->ass = NULL;
		else {
			f->ass = gale_key_assert(d,
				gale_get_file_time(f->state),trust);
			if (NULL != owner && NULL == gale_key_owner(f->ass))
				gale_alert(GALE_WARNING,gale_text_concat(3,
					G_("someone replaced \""),f->name,
					G_("\" with a bad key")),0);
		}
	}
}

static void wipe_file(int do_trust,struct dir_filename *f,
	const struct gale_key_assertion *one,
	const struct gale_key_assertion *two)
{
	if (NULL == f->state || NULL == f->ass) return;

	if (f->ass == one || f->ass == two) {
		const struct gale_time stamp = gale_key_time(f->ass);
		if (gale_time_compare(stamp,gale_get_file_time(f->state)) > 0)
			gale_set_file_time(f->state,stamp);
		return;
	}

	if (gale_erase_file(f->state))
		gale_alert(GALE_NOTICE,gale_text_concat(3,
			G_("erased \""),f->name,G_("\"")),0);
	else if (0 != errno && ENOENT != errno)
		gale_alert(GALE_WARNING,f->name,errno);
	gale_key_retract(f->ass,do_trust);
	f->ass = NULL;
}

static void put_file(int trust,
	const struct gale_key_assertion *ass,
	struct dir_filename *f) 
{
	const struct gale_data d = gale_key_raw(ass);
	if (gale_write_file(f->name,d,0,&f->state)) {
		gale_alert(GALE_NOTICE,gale_text_concat(3,
			G_("wrote \""),f->name,G_("\"")),0);
		gale_key_retract(f->ass,trust);
		f->ass = gale_key_assert(d,gale_get_file_time(f->state),trust);
	} else
		gale_alert(GALE_WARNING,gale_text_concat(3,
			G_("could not write \""),f->name,G_("\"")),errno);
}

static void dir_hook(struct gale_time now,oop_source *oop,
	struct gale_key *key,int flags,
	struct gale_key_request *handle,
	void *user,void **ptr) 
{
	struct dir_data *data = (struct dir_data *) user;
	struct dir_cache *cache;
	const int trusted = trusted_dir == data->type 
	                 || private_dir == data->type;

	if (NULL != *ptr) 
		cache = *ptr;
	else {
		const struct gale_text name = gale_key_name(key);
		gale_create(cache);
		memset(cache,0,sizeof(*cache));
		*ptr = cache;

		cache->old.name = dir_file(data->dir,key_i_swizzle(name));
		cache->public.name = dir_file(data->dir,
			gale_text_concat(2,name,G_(".gpub")));
		cache->private.name = dir_file(data->dir,
			gale_text_concat(2,name,G_(".gpri")));
	}

	if (0 < gale_time_compare(now,
		gale_time_add(cache->last,gale_time_seconds(poll_interval))))
	{
		get_file(trusted,&cache->old);
		get_file(trusted,&cache->public);
		if (trusted) get_file(1,&cache->private);
		cache->last = now;
		cache->public_written = NULL;
		cache->private_written = NULL;
	}

	if (cache_dir == data->type || private_dir == data->type) {
		const struct gale_key_assertion *pub = gale_key_public(key,now);
		const struct gale_key_assertion *priv = gale_key_private(key);
		if (NULL == pub
		|| (cache_dir == data->type && NULL == gale_key_signed(pub))
		|| (private_dir == data->type && !gale_key_trusted(pub)))
			pub = NULL;
		if (private_dir != data->type)
			priv = NULL;

		wipe_file(trusted,&cache->old,pub,priv);
		wipe_file(trusted,&cache->public,pub,NULL);
		wipe_file(trusted,&cache->private,priv,NULL);

		if (NULL != pub
		&&  pub != cache->public_written
		&&  pub != cache->old.ass
		&&  pub != cache->public.ass) {
			put_file(trusted,pub,&cache->public);
			cache->public_written = pub;
		}

		if (NULL != priv
		&&  priv != cache->private_written
		&&  priv != cache->old.ass
		&&  priv != cache->private.ass) {
			put_file(trusted,priv,&cache->private);
			cache->private_written = priv;
		}
	}

	gale_key_hook_done(oop,key,handle);
}

static void add_dir(struct gale_text dir,enum dir_type type) {
	struct dir_data *data;
	gale_create(data);
	data->dir = dir;
	data->type = type;
	gale_key_add_hook(dir_hook,data);
}

void key_i_init_dirs(void) {
	struct gale_text dot_auth,sys_auth;

	dot_auth = sub_dir(gale_global->dot_gale,G_("auth"),0700);
	sys_auth = sub_dir(gale_global->sys_dir,G_("auth"),0777);

	add_dir(sub_dir(dot_auth,G_("private"),0700),private_dir);
	add_dir(sub_dir(sys_auth,G_("private"),0700),trusted_dir);

	add_dir(sub_dir(dot_auth,G_("trusted"),0777),trusted_dir);
	add_dir(sub_dir(sys_auth,G_("trusted"),0777),trusted_dir);

	add_dir(sub_dir(dot_auth,G_("local"),0777),public_dir);
	add_dir(sub_dir(sys_auth,G_("local"),0777),public_dir);

	add_dir(sub_dir(sys_auth,G_("cache"),0777),cache_dir);
}
