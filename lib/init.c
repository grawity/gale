#include <errno.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <pwd.h>
#include <assert.h>

#include "gale/all.h"

struct gale_text dot_gale,home_dir,sys_dir;

static int main_argc;
static char * const *main_argv;
static sigset_t blocked;
extern char **environ;

extern auth_hook _gale_find_id;
static auth_hook find_id,*old_find;

static int find_id(struct auth_id *id) {
	if (_gale_find_id(id)) return 1;
	if (old_find && old_find(id)) return 1;
	return 0;
}

static void init_vars(struct passwd *pwd) {
	struct utsname un;

	if (!getenv("GALE_DOMAIN"))
		gale_alert(GALE_ERROR,"GALE_DOMAIN not set",0);

	if (uname(&un) < 0) gale_alert(GALE_ERROR,"uname",errno);

	if (!gale_var(G_("HOST")).l)
		gale_set(G_("HOST"),gale_text_from_local(un.nodename,-1));

	if (!gale_var(G_("LOGNAME")).l)
		gale_set(G_("LOGNAME"),gale_text_from_local(pwd->pw_name,-1));

	{
		struct gale_text new = gale_text_concat(5,
			dir_file(dot_gale,G_("bin")),G_(":"),
			dir_file(sys_dir,G_("bin")),G_(":"),
			dir_file(dot_gale,G_(".")));
		struct gale_text old = gale_var(G_("PATH"));

		if (gale_text_compare(gale_text_left(old,new.l),new)) {
			if (0 != old.l)
				new = gale_text_concat(3,new,G_(":"),old);
			gale_set(G_("PATH"),new);
		}
	}

	if (!gale_var(G_("GALE_FROM")).l) {
		char *name = strtok(pwd->pw_gecos,",");
		if (!name || !*name) name = "unknown";
		gale_set(G_("GALE_FROM"),gale_text_from_local(name,-1));
	}

	if (!gale_var(G_("GALE_ID")).l)
		gale_set(G_("GALE_ID"),gale_text_from_local(pwd->pw_name,-1));

	if (!gale_var(G_("GALE_SUBS")).l) {
		struct auth_id *id = lookup_id(gale_var(G_("GALE_ID")));
		gale_set(G_("GALE_SUBS"),id_category(id,G_("user"),G_("")));
	}
}

static char *read_line(FILE *fp) {
	static char *buf = NULL;
	static int alloc = 0;
	int ch,size = 0;

	if (!fp) return NULL;

	while ((ch = fgetc(fp)) != EOF && ch != '\n') {
		if (size >= alloc - 1) {
			char *old = buf;
			buf = gale_malloc(alloc = alloc ? alloc * 2 : 256);
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

void gale_restart(void) {
	sigprocmask(SIG_SETMASK,&blocked,NULL);
	assert(main_argv[main_argc] == NULL);
	execvp(main_argv[0],main_argv);
	gale_alert(GALE_WARNING,main_argv[0],errno);
}

static void sig_usr1(int x) {
	(void) x;
	gale_alert(GALE_NOTICE,"SIGUSR1 received, restarting",0);
	gale_restart();
}

static void sig_pipe(int x) {
	(void) x;
	/* do nothing */
}

void gale_init(const char *s,int argc,char * const *argv) {
	struct passwd *pwd = NULL;
	char *user;
	struct sigaction act;
	sigset_t empty;

#ifdef HAVE_SOCKS
	SOCKSinit(argv[0]);
#endif

	if (getuid() != geteuid()) {
		environ = malloc(sizeof(*environ));
		environ[0] = NULL;
	}

	main_argc = argc;
	main_argv = argv;

	sigemptyset(&empty);
	sigprocmask(SIG_BLOCK,&empty,&blocked);

	sigaction(SIGUSR1,NULL,&act);
	act.sa_handler = sig_usr1;
	sigaction(SIGUSR1,&act,NULL);

	sigaction(SIGPIPE,NULL,&act);
	act.sa_flags &= ~SA_RESETHAND;
	act.sa_handler = sig_pipe;
	sigaction(SIGPIPE,&act,NULL);

	if ((user = getenv("LOGNAME"))) pwd = getpwnam(user);
	if (!pwd) pwd = getpwuid(geteuid());
	if (!pwd) gale_alert(GALE_ERROR,"you do not exist",0);

	gale_error_prefix = s;

	home_dir = gale_var(G_("HOME"));
	if (0 == home_dir.l) home_dir = gale_text_from_local(pwd->pw_dir,-1);
	make_dir(home_dir,0777);

	dot_gale = gale_var(G_("GALE_DIR"));
	if (0 != dot_gale.l) 
		make_dir(dot_gale,0777);
	else
		dot_gale = sub_dir(home_dir,G_(".gale"),0777);

	read_conf(dir_file(dot_gale,G_("conf")));

	sys_dir = gale_var(G_("GALE_SYS_DIR"));
	if (!sys_dir.l) sys_dir = gale_text_from_local(GALE_SYS_DIR,-1);
	make_dir(sys_dir,0);

	read_conf(dir_file(sys_dir,G_("conf")));

	old_find = hook_find_public;
	hook_find_public = find_id;
	init_vars(pwd);
}
