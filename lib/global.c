#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <pwd.h>

#include "gale/globals.h"

struct gale_global_data *gale_global;
extern char **environ;

static char *read_line(FILE *fp) {
	static char *buf = NULL;
	static int alloc = 0;
	int ch,size = 0;

	if (NULL == fp) return NULL;
	if (0 == alloc) buf = gale_malloc_safe(alloc = 256);

	while ((ch = fgetc(fp)) != EOF && ch != '\n') {
		if (size >= alloc - 1) {
			char *old = buf;
			buf = gale_malloc_safe(alloc *= 2);
			memcpy(buf,old,size);
			gale_free(old);
		}
		buf[size++] = ch;
	}

	if (ch == EOF) return NULL;
	buf[size++] = '\0';
	return buf;
}

static void read_conf(struct gale_text fn) {
	FILE *fp = fopen(gale_text_to_local(fn),"r");
	char *s = read_line(fp);

	while (s) {
		struct gale_text var,value;
		size_t len;

		while (*s && isspace(*s)) ++s;
		if (!*s || *s == '#') {
			s = read_line(fp);
			continue;
		}

		for (len = 0; s[len] && !isspace(s[len]); ++len) ;
		var = gale_text_from_local(s,len);

		s += len;
		while (*s && isspace(*s)) ++s;
		value = gale_text_from_local(s,-1);

		s = read_line(fp);
		while (s && *s && isspace(*s)) {
			do ++s; while (*s && isspace(*s));
			if (*s == '#') break;

			value = gale_text_concat(2,value,
				gale_text_from_local(s,-1));

			s = read_line(fp);
		}

		if (0 == gale_var(var).l) gale_set(var,value);
	}
}

void _gale_globals(struct passwd *pwd) {
	struct gale_global_data *G = gale_malloc_safe(sizeof(*gale_global));
	struct gale_text conf;
	memset(G,'\0',sizeof(*gale_global));
	gale_global = G;

	/* These are in this particular order to allow each 'conf' file to
	   redirect the location of the next one. */

	G->error = NULL;
	G->cleanup_list = NULL;
	G->home_dir = gale_var(G_("HOME"));
	if (0 == G->home_dir.l) 
		G->home_dir = gale_text_from_local(pwd->pw_dir,-1);
	make_dir(G->home_dir,0777);

	G->dot_gale = gale_var(G_("GALE_DIR"));
	if (0 != G->dot_gale.l) 
		make_dir(G->dot_gale,0700);
	else
		G->dot_gale = sub_dir(G->home_dir,G_(".gale"),0700);

	conf = gale_var(G_("GALE_CONF"));
	if (0 != conf.l) read_conf(dir_file(G->dot_gale,conf));
	read_conf(dir_file(G->dot_gale,G_("conf")));

	G->sys_dir = gale_var(G_("GALE_SYS_DIR"));
	if (0 == G->sys_dir.l) 
		G->sys_dir = gale_text_from_local(GALE_SYS_DIR,-1);
	make_dir(G->sys_dir,0);

	read_conf(dir_file(G->sys_dir,G_("conf")));

	/* Now we initialize lots of directories. */

	G->dot_auth = sub_dir(G->dot_gale,G_("auth"),0700);
	G->dot_trusted = sub_dir(G->dot_auth,G_("trusted"),0777);
	G->dot_private = sub_dir(G->dot_auth,G_("private"),0700);
	G->dot_local = sub_dir(G->dot_auth,G_("local"),0777);

	G->sys_auth = sub_dir(G->sys_dir,G_("auth"),0777);
	G->sys_trusted = sub_dir(G->sys_auth,G_("trusted"),0777);
	G->sys_private = sub_dir(G->sys_auth,G_("private"),0777);
	G->sys_local = sub_dir(G->sys_auth,G_("local"),0777);
        chmod(gale_text_to_local(dir_file(G->sys_local,G_("."))),01777);
	G->sys_cache = sub_dir(G->sys_auth,G_("cache"),0777);

	G->user_cache = sub_dir(G->dot_gale,G_("cache"),0700);
	G->system_cache = sub_dir(G->sys_dir,G_("cache"),0777);

	G->environ = environ;
}
