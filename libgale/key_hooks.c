#include "key_i.h"
#include "gale/key.h"
#include "gale/globals.h"

enum dir_type { normal_dir, cache_dir, trusted_dir };

struct dir_data {
	struct gale_text dir;
	enum dir_type type;
};

struct dir_filename {
	struct gale_file_state *state;
	struct gale_key_assertion *ass;
};

struct dir_cache {
	struct dir_filename old;
};

static const int size_limit = 65536;

static void sync_file(
	struct gale_text file,
	struct dir_filename *filename,
	const struct gale_key_assertion *ass)
{
	/* TODO: offer a warning if these fail */
	if (filename->ass != ass && !gale_key_trusted(ass)) {
		if (NULL == ass) /* gale_erase_file(*state) */;
		filename->state = NULL;
		filename->ass = NULL;
		if (NULL != ass) gale_write_file(
			file,gale_key_raw(ass),
			0,&filename->state);
	}
}

static void get_file(int do_trust,int do_store_public,int do_store_private,
	struct gale_time now,
	struct gale_key *key,
	struct gale_text file,
	struct dir_filename *filename)
{
	if (NULL == filename->state 
	||  gale_file_changed(filename->state)) {
		struct gale_data d = gale_read_file(
			file,size_limit,!do_trust,&filename->state);
		if (NULL != filename->ass) gale_key_retract(filename->ass);
		filename->ass = (0 != d.l) ? gale_key_assert(d,do_trust) : NULL;
	}

	if (do_store_public)
		sync_file(file,filename,gale_key_public(key,now));
	if (do_store_private)
		sync_file(file,filename,gale_key_private(key));
}

static void dir_hook(struct gale_time now,oop_source *oop,
	struct gale_key *key,
	struct gale_key_request *handle,
	void *user,void **ptr) 
{
	struct dir_data *data = (struct dir_data *) user;
	struct dir_cache *cache;

	if (NULL != *ptr) 
		cache = *ptr;
	else {
		gale_create(cache);
		memset(cache,0,sizeof(*cache));
		*ptr = cache;
	}

	get_file(trusted_dir == data->type,cache_dir == data->type,0,now,key,
		dir_file(data->dir,key_i_swizzle(gale_key_name(key))),
		&cache->old);

	gale_key_hook_done(oop,key,handle);
}

static void add_dir(struct gale_text dir,enum dir_type type) {
	struct dir_data *data;
	gale_create(data);
	data->dir = dir;
	data->type = type;
	gale_key_add_hook(dir_hook,data);
}

void key_i_init(void) {
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
