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

struct gale_text dir_file(struct gale_text path,struct gale_text file) {
	struct gale_text r = null_text,part = null_text;
	if (0 == path.l) return file;

	while (gale_text_token(file,'/',&part)) {
		if (part.p + part.l < file.p + file.l) ++part.l;
		if (gale_text_compare(part,G_(".."))
		&&  gale_text_compare(part,G_("../")))
			r = gale_text_concat(2,r,part);
		else {
			gale_alert(GALE_WARNING,
			           "replaced .. with __ in filename",0);
			r = gale_text_concat(3,r,G_("__"),
				gale_text_right(part,-2));
		}
	}

	return gale_text_concat(3,path,G_("/"),r);
}

struct gale_text dir_search(struct gale_text fn,int f,struct gale_text t,...) {
	va_list ap;
	struct gale_text r = null_text;

	if (fn.l > 0 && fn.p[0] == '/') {
		if (access(gale_text_to_local(fn),F_OK)) 
			return null_text;
		else
			return fn;
	}

	if (f && !access(gale_text_to_local(fn),F_OK))
		return fn;

	va_start(ap,t);
	while (0 == r.l && 0 != t.l) {
		r = dir_file(t,fn);
		if (access(gale_text_to_local(r),F_OK)) r.l = 0;
		t = va_arg(ap,struct gale_text);
	}
	va_end(ap);
	return r;
}

void make_dir(struct gale_text path,int mode) {
	struct stat buf;
	if (stat(gale_text_to_local(path),&buf) || !S_ISDIR(buf.st_mode))
		if (mode && mkdir(gale_text_to_local(path),mode))
			gale_alert(GALE_WARNING,gale_text_to_local(path),errno);
}

struct gale_text sub_dir(struct gale_text path,struct gale_text sub,int mode) {
	struct stat buf;
	struct gale_text ret = dir_file(path,sub);
	if ((stat(gale_text_to_local(ret),&buf) || !S_ISDIR(buf.st_mode)))
		if (mkdir(gale_text_to_local(ret),mode))
			gale_alert(GALE_WARNING,gale_text_to_local(ret),errno);
	return ret;
}

struct gale_text up_dir(struct gale_text path) {
	while (path.l > 1 && path.p[--path.l] != '/') ;
	return path;
}
