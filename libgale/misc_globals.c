#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <pwd.h>

#include "gale/globals.h"

struct gale_global_data *gale_global;
extern char **environ;

static int is_space(int ch) {
	return ' ' == ch || '\t' == ch || '\r' == ch || '\n' == ch;
}

static struct gale_text trim_space(struct gale_text line) {
	while (line.l > 0 && is_space(line.p[0])) {
		++line.p;
		--line.l;
	}
	return line;
}

static void read_conf(struct gale_text fn) {
	struct gale_text line;
	FILE * const fp = fopen(gale_text_to(gale_global->enc_filesys,fn),"r");
	if (NULL == fp) return;

	line = gale_read_line(fp);
	while (line.l > 0) {
		struct gale_text var;
		struct gale_text_accumulator val = null_accumulator;
		int i;

		line = trim_space(line);
		if (0 == line.l || '#' == line.p[0]) {
			line = gale_read_line(fp);
			continue;
		}

		i = 1;
		while (i < line.l && !is_space(line.p[i])) ++i;
		var = gale_text_left(line,i);

		while (i < line.l && is_space(line.p[i])) ++i;
		gale_text_accumulate(&val,gale_text_right(line,-i));

		line = gale_read_line(fp);
		while (line.l > 0 && (line.p[0] == ' ' || line.p[0] == '\t')) {
			line = trim_space(line);
			if (0 == line.l) {
				line = gale_read_line(fp);
				break;
			}

			gale_text_accumulate(&val,line);
			line = gale_read_line(fp);
		}

		if (0 == gale_var(var).l) {
			struct gale_text value = gale_text_collect(&val);
			while (value.l > 0 && is_space(value.p[value.l - 1]))
				--value.l;
			gale_set(var,trim_space(value));
		}
	}

	fclose(fp);
}

static struct gale_encoding *get_charset(
	struct gale_text name,
	struct gale_encoding *fallback) 
{
	const struct gale_text enc = gale_var(name);
	struct gale_encoding * const ret = gale_make_encoding(enc);
	if (NULL == ret && 0 != enc.l)
		gale_alert(GALE_WARNING,gale_text_concat(4,
			G_("unknown encoding \""),enc,
			G_("\" for "),name),0);

	return ret ? ret : fallback;
}

void _gale_globals(void) {
	struct gale_global_data *G = gale_malloc_safe(sizeof(*gale_global));
	struct gale_encoding *fallback;
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

	fallback = get_charset(G_("GALE_CHARSET"),NULL);
	if (NULL == fallback) fallback = get_charset(G_("CHARSET"),NULL);
	if (NULL == fallback) fallback = gale_make_encoding(G_("ASCII"));

	G->enc_console = get_charset(G_("GALE_CHARSET_CONSOLE"),fallback);
	G->enc_filesys = get_charset(G_("GALE_CHARSET_FILESYSTEM"),fallback);
	G->enc_environ = get_charset(G_("GALE_CHARSET_ENVIRON"),fallback);
	G->enc_cmdline = get_charset(G_("GALE_CHARSET_CMDLINE"),fallback);
	G->enc_sys = get_charset(G_("GALE_CHARSET_SYSTEM"),fallback);

	/* Now we initialize lots of directories. */

	G->user_cache = sub_dir(G->dot_gale,G_("cache"),0700);
	G->system_cache = sub_dir(G->sys_dir,G_("cache"),0777);
}
