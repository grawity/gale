#include "key_i.h"
#include "gale/key.h"
#include "gale/globals.h"

enum dir_type { normal_dir, cache_dir, trusted_dir };

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
	const struct gale_key_assertion *written;
	struct dir_filename old,public,private;
};

static const int size_limit = 65536;
static const int poll_interval = 10;

static void get_file(int do_trust,struct dir_filename *f) {
	if (NULL == f->state || gale_file_changed(f->state)) {
		struct gale_data d = gale_read_file(
			f->name,size_limit,!do_trust,&f->state);
		gale_key_retract(f->ass);
		f->ass = (0 != d.l) ? gale_key_assert(d,do_trust) : NULL;
	}
}

static void wipe_file(const struct gale_key_assertion *ass,struct dir_filename *f) {
	if (NULL == f->state || NULL == f->ass || f->ass == ass) return;
	if (gale_erase_file(f->state))
		gale_alert(GALE_NOTICE,gale_text_concat(3,
			G_("erased \""),f->name,G_("\"")),0);
	else if (0 != errno)
		gale_alert(GALE_WARNING,gale_text_concat(3,
			G_("could not erase \""),f->name,G_("\"")),errno);
	gale_key_retract(f->ass);
	f->ass = NULL;
}

static void put_file(
	const struct gale_key_assertion *ass,
	struct dir_filename *f) 
{
	if (gale_write_file(f->name,gale_key_raw(ass),0,&f->state))
		gale_alert(GALE_NOTICE,gale_text_concat(3,
			G_("wrote \""),f->name,G_("\"")),0);
	else
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
		get_file(trusted_dir == data->type,&cache->old);
		get_file(trusted_dir == data->type,&cache->public);
		if (trusted_dir == data->type) get_file(1,&cache->private);
		cache->last = now;
		cache->written = NULL;
	}

	if (cache_dir == data->type) {
		const struct gale_key_assertion *cur = gale_key_public(key,now);
		if (gale_key_trusted(cur)) cur = NULL;
		wipe_file(cur,&cache->old);
		wipe_file(cur,&cache->public);
		if (NULL != cur
		&& !gale_key_trusted(cur)
		&&  cur != cache->written
		&&  cur != cache->old.ass
		&&  cur != cache->public.ass) {
			put_file(cur,&cache->public);
			cache->written = cur;
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

	add_dir(sub_dir(dot_auth,G_("trusted"),0777),trusted_dir);
	add_dir(sub_dir(sys_auth,G_("trusted"),0777),trusted_dir);

	add_dir(sub_dir(dot_auth,G_("private"),0700),trusted_dir);
	add_dir(sub_dir(sys_auth,G_("private"),0700),trusted_dir);

	add_dir(sub_dir(dot_auth,G_("local"),0777),normal_dir);
	add_dir(sub_dir(sys_auth,G_("local"),0777),normal_dir);

	add_dir(sub_dir(sys_auth,G_("cache"),0777),cache_dir);
}
