#include "gale/all.h"
#include "global.h"
#include "rsaref.h"
#include "md5.h"

#include <sys/utsname.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>

/* Caches will get cleaned every time a store operation takes place more than
   twice the following interval after the last cache cleaning. */
#define CLEAN_INTERVAL (60 * 60 * 11)

struct cid {
	u32 size;
	u8 hash[16];
};

static int to_cid(struct cid *cid,struct gale_data data) {
	return (gale_unpack_u32(&data,&cid->size)
	    &&  gale_unpack_copy(&data,cid->hash,sizeof(cid->hash))
	    &&  0 == data.l);
}

static struct gale_data from_cid(struct cid cid) {
	struct gale_data data;
	data.p = gale_malloc(gale_u32_size() + 
                             gale_copy_size(sizeof(cid.hash)));
	data.l = 0;
	gale_pack_u32(&data,cid.size);
	gale_pack_copy(&data,cid.hash,sizeof(cid.hash));
	return data;
}

static struct gale_data to_raw(struct gale_group group) {
	struct gale_data raw;
	raw.p = gale_malloc(gale_group_size(group));
	raw.l = 0;
	gale_pack_group(&raw,group);
	return raw;
}

static int from_raw(struct gale_group *group,struct gale_data data) {
	return gale_unpack_group(&data,group);
}

static struct cid compute(struct gale_data data) {
	struct cid cid;
	MD5_CTX ctx;
	cid.size = data.l;
	MD5Init(&ctx);
	MD5Update(&ctx,data.p,data.l);
	MD5Final(cid.hash,&ctx);
	return cid;
}

static int compare(struct cid a,struct cid b) {
	int i;
	if (a.size != b.size) return b.size - a.size;
	for (i = 0; i < (sizeof(a.hash) / sizeof(a.hash[0])); ++i)
		if (a.hash[i] != b.hash[i]) return b.hash[i] - a.hash[i];
	return 0;
}

/* -------------------------------------------------------------------------- */

static struct gale_text encode(struct cid cid) {
	char sz[2*sizeof(cid.hash)];
	int i;
	for (i = 0; i < sizeof(cid.hash); ++i) {
		sz[2*i] = (cid.hash[i] >> 4) + 'a';
		sz[2*i + 1] = 'z' - (cid.hash[i] & 0xF);
	}
	return gale_text_concat(2,G_("cache."),
	                        gale_text_from(NULL,sz,sizeof(sz)));
}

static struct gale_text temp(void) {
	struct utsname un;
	static int seq = 0;
	pid_t pid;

	uname(&un);
	pid = getpid();

	return gale_text_concat(6,G_("temp."),
	       gale_text_from(gale_global->enc_sys,un.nodename,-1),G_("."),
	       gale_text_from_number(pid,10,0),G_("."),
	       gale_text_from_number(++seq,10,0));
}

static struct gale_text stamp(int ofs) {
	int num = (time(NULL) / CLEAN_INTERVAL) + ofs;
	return gale_text_concat(2,G_("stamp."),
	       gale_text_from_number(num,10,0));
}

static int find(struct cid cid,struct gale_text dir,struct gale_text file,
                struct gale_data *pdata) 
{
	struct stat buf;
	struct gale_data data;
	struct gale_text filename = dir_file(dir,file);
	const char *sz = gale_text_to(gale_global->enc_filesys,filename);
	int fd = -1,r;

	if (NULL != pdata) {
		pdata->p = NULL;
		pdata->l = 0;
	}

	if (0 > (fd = open(sz,O_RDONLY))) {
		if (ENOENT == errno) return 0;
		gale_alert(GALE_WARNING,filename,errno);
		goto error;
	}
	if (!fstat(fd,&buf)) {
		gale_alert(GALE_WARNING,filename,errno);
		goto error;
	}
	if (!S_ISREG(buf.st_mode)) {
		gale_alert(GALE_WARNING,gale_text_concat(3,
			G_("\""),filename,G_("\": not an ordinary file")),0);
		goto error;
	}
	if (buf.st_size != cid.size) {
		gale_alert(GALE_WARNING,gale_text_concat(6,
			G_("\""),filename,G_("\": expected size "),
			gale_text_from_number(cid.size,10,0),
			G_(", found size "),
			gale_text_from_number(buf.st_size,10,0)),0);
		goto error;
	}

	data.p = gale_malloc(cid.size);
	data.l = cid.size;
	r = read(fd,data.p,cid.size);
	if (r < 0) {
		gale_alert(GALE_WARNING,filename,errno);
		goto error;
	}
	if (r != cid.size) {
		gale_alert(GALE_WARNING,gale_text_concat(3,
			G_("\""),filename,G_("\": read truncated")),0);
		goto error;
	}

	if (0 != compare(cid,compute(data))) {
		gale_alert(GALE_WARNING,gale_text_concat(3,
			G_("\""),filename,G_("\": invalid checksum")),0);
		goto error;
	}

	if (NULL != pdata) *pdata = data;
	return 1;

error:
	{
		const char *szt = gale_text_to(
			gale_global->enc_filesys,
			dir_file(dir,temp()));
		if (!rename(sz,szt)) unlink(szt);
	}
	if (0 <= fd) close(fd);
	return 0;
}

static int getlock(struct gale_text dir,struct gale_text file) {
	const char *sz = gale_text_to(
		gale_global->enc_filesys,
		dir_file(dir,file));
	const char *szt = NULL;
	int fd = -1;
	struct stat buf;

	/* First, a quick check to see if the file exists. */
	if (!stat(sz,&buf)) goto failed;

	/* Now, create a unique file... */
	szt = gale_text_to(gale_global->enc_filesys,dir_file(dir,temp()));
	fd = creat(szt,0444);
	if (fd < 0) goto failed;
	close(fd); fd = -1;

	/* Make a link to the lockfile. */
	if (link(szt,sz)) goto failed;
	if (stat(szt,&buf)) goto failed;
	if (buf.st_nlink > 1) {
		unlink(szt);
		return 1;
	}

failed:
	if (fd >= 0) close(fd);
	if (NULL != szt) unlink(szt);
	return 0;
}

static void clean(struct gale_text dir) {
	pid_t pid;

	gale_alert(GALE_NOTICE,gale_text_concat(3,
		G_("our turn to clean \""),dir,G_("\"")),0);

	/* XXX -- perform cache cleaning here! */
}

static int store(struct gale_text dir,struct gale_text name,struct gale_data data) {
	struct gale_text file = dir_file(dir,name);
	const char *sz = gale_text_to(gale_global->enc_filesys,file);
	struct gale_text tempname = dir_file(dir,temp());
	const char *szt = gale_text_to(gale_global->enc_filesys,tempname);
	struct stat buf;
	int fd = -1,r;

	/* If the file already exists, assume it's valid (likely the result
	   of a race condition).  If not, it ought to get deleted at some
	   point, and this is just a cache anyway... */
	if (stat(sz,&buf)) return 1;

	fd = open(szt,O_WRONLY,0755);
	if (fd < 0) return 0;

	r = write(fd,data.p,data.l);
	if (r < 0) {
		gale_alert(GALE_WARNING,tempname,errno);
		goto error;
	}
	if (r < data.l) {
		gale_alert(GALE_WARNING,gale_text_concat(3,
			G_("\""),tempname,G_("\": write truncated")),0);
		goto error;
	}

	fchmod(fd,0555); /* Give the file away... */
	fchown(fd,0,-1); /* Make sure nobody can write it. */
	close(fd); fd = -1;

	if (rename(szt,sz)) {
		gale_alert(GALE_WARNING,file,errno);
		goto error;
	}

	if (getlock(dir,stamp(-1)) && getlock(dir,stamp(0))) {
		/* Oops, we lost Cache Roulette! */
		clean(dir);
	}

	return 1;

error:
	if (fd >= 0) close(fd);
	unlink(szt);
	return 0;
}

/* -------------------------------------------------------------------------- */

struct gale_data cache_id(struct gale_group group) {
	return cache_id_raw(to_raw(group));
}

struct gale_data cache_id_raw(struct gale_data data) {
	return from_cid(compute(data));
}

int cache_find(struct gale_data id,struct gale_group *data) {
	struct gale_data raw;
	return (cache_find_raw(id,&raw) && from_raw(data,raw));
}

int cache_find_raw(struct gale_data id,struct gale_data *data) {
	struct cid cid;
	void *pv;
	struct gale_text name;
	if (!to_cid(&cid,id)) return 0;
	name = encode(cid);

	/* Look in memory. */
	if (gale_global->cache_tree 
	&& (pv = gale_map_find(gale_global->cache_tree,gale_text_as_data(name)))) {
		*data = * (struct gale_data *) pv;
		return 1;
	}

	/* Look on disk. */
	if (find(cid,gale_global->user_cache,name,data)) return 1;
	if (find(cid,gale_global->system_cache,name,data)) return 1;
	return 0;
}

void cache_store(struct gale_data id,struct gale_group data) {
	cache_store_raw(id,to_raw(data));
}

void cache_store_raw(struct gale_data id,struct gale_data data)  {
	struct cid cid;
	struct gale_text name;
	struct gale_data *pdata;
	if (!to_cid(&cid,id)) return;
	name = encode(cid);

	/* Store in memory. */
	if (!gale_global->cache_tree) gale_global->cache_tree = gale_make_map(1);
	gale_create(pdata);
	*pdata = data;
	gale_map_add(gale_global->cache_tree,gale_text_as_data(name),pdata);

	/* Store on disk. */
	(void) (store(gale_global->system_cache,name,data) || 
	        store(gale_global->user_cache,name,data));
}
