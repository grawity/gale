#include <stdarg.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pwd.h>

#include "gale/all.h"

struct gale_dir {
	int len,alloc;
	char *buf;
};

const char *dir_file(struct gale_dir *d,const char *s) {
	const int l = strlen(s);
	char *p;

	if (d == NULL) return s;
	if (l + 1 + d->len >= d->alloc) {
		char *n = gale_malloc(d->alloc = l + 1 + d->len + 1);
		strncpy(n,d->buf,d->len + 1);
		gale_free(d->buf);
		d->buf = n;
	}
	d->buf[d->len] = '/';
	strcpy(d->buf + d->len + 1,s);

	for (p = d->buf + d->len; p; p = strchr(p,'/')) {
		++p;
		if (!strcmp(p,"..") || !strncmp(p,"../",3)) {
			p[0] = p[1] = '_';
			gale_alert(GALE_WARNING,
			           "replaced .. with __ in filename",0);
		}
	}

	return d->buf;
}

const char *dir_search(const char *s,int cwd,struct gale_dir *d,...) {
	va_list ap;
	const char *r = NULL;
	if (s[0] == '/') return access(s,F_OK) ? NULL : s;
	if (cwd && !access(s,F_OK)) return s;
	va_start(ap,d);
	while (r == NULL && d != NULL) {
		r = dir_file(d,s);
		if (access(r,F_OK)) r = NULL;
		d = va_arg(ap,struct gale_dir *);
	}
	va_end(ap);
	return r;
}

struct gale_dir *dup_dir(struct gale_dir *d) {
	struct gale_dir *r = gale_malloc(sizeof(struct gale_dir));
	r->len = d->len;
	r->buf = gale_malloc(r->alloc = d->len + 1);
	strncpy(r->buf,d->buf,r->alloc);
	return r;
}

struct gale_dir *make_dir(const char *s,int mode) {
	struct stat buf;
	struct gale_dir *r = gale_malloc(sizeof(struct gale_dir));
	r->len = strlen(s);
	strcpy(r->buf = gale_malloc(r->alloc = r->len + 1),s);
	if (stat(r->buf,&buf) || !S_ISDIR(buf.st_mode))
		if (mode && mkdir(r->buf,mode))
			gale_alert(GALE_WARNING,r->buf,errno);
	return r;
}

void free_dir(struct gale_dir *d) {
	gale_free(d->buf);
	gale_free(d);
}

void sub_dir(struct gale_dir *d,const char *s,int mode) {
	struct stat buf;
	d->len = strlen(dir_file(d,s));
	if ((stat(d->buf,&buf) || !S_ISDIR(buf.st_mode)) && mkdir(d->buf,mode))
		gale_alert(GALE_WARNING,d->buf,errno);
}

void up_dir(struct gale_dir *d) {
	char *c;
	d->buf[d->len] = '\0';
	c = strrchr(d->buf,'/');
	if (c) d->len = c - d->buf;
}
