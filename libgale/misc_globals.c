#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
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
	FILE *fp = fopen(gale_text_to(gale_global->enc_filesys,fn),"r");
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
		var = gale_text_from(gale_global->enc_filesys,s,len);

		s += len;
		while (*s && isspace(*s)) ++s;
		value = gale_text_from(gale_global->enc_filesys,s,-1);

		s = read_line(fp);
		while (s && *s && isspace(*s)) {
			do ++s; while (*s && isspace(*s));
			if (*s == '#') break;

			value = gale_text_concat(2,value,
				gale_text_from(gale_global->enc_filesys,s,-1));

			s = read_line(fp);
		}

		if (0 == gale_var(var).l) gale_set(var,value);
	}
}

static struct gale_encoding *get_charset(struct gale_text name) {
	struct gale_text enc;

	enc = gale_var(name);
	if (0 == enc.l) enc = gale_var(G_("GALE_CHARSET"));
	if (0 == enc.l) enc = gale_var(G_("CHARSET"));
	return gale_make_encoding(enc);
}

void _gale_globals(void) {
	struct gale_global_data *G = gale_malloc_safe(sizeof(*gale_global));
	struct gale_text conf;
	memset(G,'\0',sizeof(*gale_global));
	gale_global = G;

	/* These are in this particular order to allow each 'conf' file to
	   redirect the location of the next one. */

	assert(NULL == G->error);
	assert(NULL == G->cleanup_list);

	G->home_dir = gale_var(G_("HOME"));
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
		G->sys_dir = gale_text_from(
			gale_global->enc_filesys,GALE_SYS_DIR,-1);
	make_dir(G->sys_dir,0);

	read_conf(dir_file(G->sys_dir,G_("conf")));

	/* Set up character encodings. */

	G->enc_console = get_charset(G_("GALE_CHARSET_CONSOLE"));
	G->enc_filesys = get_charset(G_("GALE_CHARSET_FILESYSTEM"));
	G->enc_environ = get_charset(G_("GALE_CHARSET_ENVIRON"));
	G->enc_cmdline = get_charset(G_("GALE_CHARSET_CMDLINE"));
	G->enc_sys = get_charset(G_("GALE_CHARSET_SYSTEM"));

	/* Now we initialize lots of directories. */

	G->user_cache = sub_dir(G->dot_gale,G_("cache"),0700);
	G->system_cache = sub_dir(G->sys_dir,G_("cache"),0777);
}
