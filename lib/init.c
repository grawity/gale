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

static int main_argc;
static char * const *main_argv;
static sigset_t blocked;
extern char **environ;

extern auth_hook _gale_find_id;
static auth_hook find_id,*old_find;

extern void _gale_globals(struct passwd *pwd);

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
			dir_file(gale_global->dot_gale,G_("bin")),G_(":"),
			dir_file(gale_global->sys_dir,G_("bin")),G_(":"),
			dir_file(gale_global->dot_gale,G_(".")));
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

	if (getuid() != geteuid()) {
		environ = malloc(sizeof(*environ));
		environ[0] = NULL;
	}

	main_argc = argc;
	main_argv = argv;

#ifdef HAVE_SOCKS
	SOCKSinit(s);
#endif

	/* Rationalize signal handling. */

	sigemptyset(&empty);
	sigprocmask(SIG_BLOCK,&empty,&blocked);

	sigaction(SIGUSR1,NULL,&act);
	act.sa_handler = sig_usr1;
	sigaction(SIGUSR1,&act,NULL);

	sigaction(SIGPIPE,NULL,&act);
#ifdef SA_RESETHAND
	act.sa_flags &= ~SA_RESETHAND;
#endif
	act.sa_handler = sig_pipe;
	sigaction(SIGPIPE,&act,NULL);

	/* Identify the user. */

	if ((user = getenv("LOGNAME"))) pwd = getpwnam(user);
	if (!pwd) pwd = getpwuid(geteuid());
	if (!pwd) gale_alert(GALE_ERROR,"you do not exist",0);

	/* Set up global variables. */

	_gale_globals(pwd);
	gale_global->error_prefix = s;

	/* Install AKD handler. */

	old_find = gale_global->find_public;
	gale_global->find_public = find_id;

	/* Round out the environment. */

	init_vars(pwd);
	gale_global->user_id = lookup_id(gale_var(G_("GALE_ID")));
}
