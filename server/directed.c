#include "directed.h"
#include "attach.h"
#include "subscr.h"
#include "server.h"

#include "gale/misc.h"

#include "oop.h"

#include <assert.h>

struct directed {
	struct gale_text host;
	int ref,is_busy;
	int is_old,is_empty;
	struct attach *attach;
	struct timeval timeout;
};

static struct gale_wt *dirs = NULL;

static struct directed *get_dir(struct gale_text host) {
	struct directed *dir;
	if (NULL == dirs) dirs = gale_make_wt(0);
	dir = (struct directed *) gale_wt_find(dirs,gale_text_as_data(host));
	if (NULL == dir) {
		gale_create(dir);
		/* initialize dir */
		dir->host = host;
		dir->ref = 0;
		dir->is_busy = 0;
		dir->is_old = 0;
		dir->is_empty = 0;
		dir->attach = NULL;
		gale_wt_add(dirs,gale_text_as_data(host),dir);
	}
	return dir;
}

static void check_done(struct directed *dir) {
	if (!dir->is_busy && dir->is_old && dir->is_empty) {
		dir->is_busy = 1;
		close_attach(dir->attach);
		dir->attach = NULL;
		assert(0 == dir->ref);
		gale_wt_add(dirs,gale_text_as_data(dir->host),NULL);
		dir->is_busy = 0;
	}
}

static void *on_timeout(oop_source *src,struct timeval tv,void *d) {
	struct directed *dir = (struct directed *) d;
	dir->is_old = 1;
	check_done(dir);
	return OOP_CONTINUE;
}

static void *on_empty(struct attach *att,void *d) {
	struct directed *dir = (struct directed *) d;
	dir->is_empty = 1;
	on_empty_attach(dir->attach,NULL,NULL);
	check_done(dir);
	return OOP_CONTINUE;
}

static struct gale_message *cat_filter(struct gale_message *msg,void *d) {
	struct directed *dir = (struct directed *) d;
	struct gale_message *rewrite = new_message();
	struct gale_text cat = null_text;
	int do_transmit = 0;

	rewrite->data = msg->data;
	while (gale_text_token(msg->cat,':',&cat)) {
		struct gale_text base,host;
		int orig_flag;
		int flag = is_directed(cat,&orig_flag,&base,&host) && orig_flag
			&& !gale_text_compare(host,dir->host);
		base = category_escape(base,flag);
		do_transmit |= flag;
                rewrite->cat = gale_text_concat(3,rewrite->cat,G_(":"),base);
        }

	if (!do_transmit) {
		gale_dprintf(5,"*** no positive categories; dropped message\n");
		return NULL;
	}

        /* strip leading colon */
        if (rewrite->cat.l > 0) rewrite->cat = gale_text_right(rewrite->cat,-1);
	gale_dprintf(5,"*** \"%s\": rewrote categories to \"%s\"\n",
	             gale_text_to_local(dir->host),
	             gale_text_to_local(rewrite->cat));
	return rewrite;
}

static void activate(oop_source *src,struct directed *dir) {
	if (dir->is_busy) return;
	dir->is_busy = 1;

	if (NULL == dir->attach) {
		struct gale_text cat = gale_text_concat(3,
			G_("@"),dir->host,G_("/"));
		dir->attach = new_attach(src,dir->host,cat_filter,dir,cat,cat);
	}

	src->cancel_time(src,dir->timeout,on_timeout,dir);
	on_empty_attach(dir->attach,NULL,NULL);

	if (dir->ref <= 1) {
		gettimeofday(&dir->timeout,NULL);
		dir->timeout.tv_sec += DIRECTED_TIMEOUT;
		src->on_time(src,dir->timeout,on_timeout,dir);
		on_empty_attach(dir->attach,on_empty,dir);
	}

	dir->is_busy = 0;
	dir->is_old = 0;
	dir->is_empty = 0;
}

int is_directed(struct gale_text cat,int *flag,
                struct gale_text *base,struct gale_text *host) 
{
	struct gale_text base_buf,host_buf;
	int flag_buf;

	/* Allow NULL. */
	if (NULL == base) base = &base_buf;
	if (NULL == host) host = &host_buf;
	if (NULL == flag) flag = &flag_buf;

	*host = null_text;
	*flag = category_flag(cat,base);
	if (base->l < 1 || base->p[0] != '@') return 0;

	gale_text_token(gale_text_right(*base,-1),'/',host);
	if (host->l == base->l - 1) *base = gale_text_concat(2,*base,G_("/"));
	return *flag;
}

void sub_directed(oop_source *src,struct gale_text host) {
	struct directed * const dir = get_dir(host);
	++(dir->ref);
	activate(src,dir);
}

void unsub_directed(oop_source *src,struct gale_text host) {
	struct directed * const dir = get_dir(host);
	--(dir->ref);
	activate(src,dir);
}

void send_directed(oop_source *src,struct gale_text host) {
	struct directed * const dir = get_dir(host);
	activate(src,dir);
}
