#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <pwd.h>
#include <assert.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "oop.h"
#include "key_i.h"
#include "gale/all.h"

extern char **environ;

extern void _gale_globals(void);

/* Set default environment values, if they weren't already set. */

static void set_defaults(struct passwd *pwd) {
	struct utsname un;

	if (0 == gale_var(G_("GALE_DOMAIN")).l)
		gale_alert(GALE_ERROR,G_("GALE_DOMAIN not set"),0);

	if (uname(&un) < 0) gale_alert(GALE_ERROR,G_("uname"),errno);

	if (0 == gale_var(G_("HOST")).l)
		gale_set(G_("HOST"),
			gale_text_from(gale_global->enc_sys,un.nodename,-1));

	if (!gale_var(G_("LOGNAME")).l)
		gale_set(G_("LOGNAME"),gale_text_from(
			gale_global->enc_sys,pwd->pw_name,-1));

	{
		struct gale_text old = gale_var(G_("PATH"));
		struct gale_text new = gale_text_concat(6,
			dir_file(gale_global->dot_gale,G_("bin")),G_(":"),
			dir_file(gale_global->sys_dir,G_("bin")),G_(":"),
			dir_file(gale_global->dot_gale,G_(".")),G_(":"));

		if (gale_text_compare(gale_text_left(old,new.l),new))
			gale_set(G_("PATH"),gale_text_concat(2,new,old));
	}

	if (0 != gale_var(G_("GALE_FROM")).l) {
		gale_set(G_("GALE_NAME"),gale_var(G_("GALE_FROM")));
		gale_set(G_("GALE_FROM"),null_text);
	}

	if (0 == gale_var(G_("GALE_NAME")).l) {
		const char *comma = pwd->pw_gecos;
		while (NULL != comma && '\0' != *comma && ',' != *comma) 
			++comma;

		if (pwd->pw_gecos != comma)
			gale_set(G_("GALE_NAME"),gale_text_from(
				gale_global->enc_sys,
				pwd->pw_gecos,comma - pwd->pw_gecos));
		else
			gale_set(G_("GALE_NAME"),G_("unknown"));
	}

	if (0 == gale_var(G_("GALE_ID")).l)
		gale_set(G_("GALE_ID"),gale_text_from(
			gale_global->enc_sys,pwd->pw_name,-1));
}

void gale_init(const char *s,int argc,char * const *argv) {
	struct passwd *pwd = NULL;
	char *user;

	/* If we are running setuid, destroy the environment for safety. */

	if (getuid() != geteuid()) {
		environ = malloc(sizeof(*environ));
		environ[0] = NULL;
	}

	oop_malloc = gale_malloc_safe;
	oop_free = gale_free;

#ifdef HAVE_SOCKS
	SOCKSinit(s);
#endif

	/* Identify the user. */

	if ((user = getenv("LOGNAME"))) pwd = getpwnam(user);
	if (NULL == pwd) pwd = getpwuid(geteuid());
	if (NULL == pwd) gale_alert(GALE_ERROR,G_("you do not exist"),0);

	if (0 == gale_var(G_("HOME")).l)
		gale_set(G_("HOME"),gale_text_from(NULL,pwd->pw_dir,-1));

	/* Set up global variables. */

	_gale_globals();
	gale_global->main_argc = argc;
	gale_global->main_argv = argv;
	gale_global->error_prefix = s;
	gale_global->report = gale_make_report(NULL);

	/* Round out the environment. */

	set_defaults(pwd);

	key_i_init_dirs();
	key_i_init_akd();
}
