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
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "oop.h"
#include "gale/all.h"

static int main_argc;
static char * const *main_argv;
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
	struct hostent *host;

	if (!getenv("GALE_DOMAIN"))
		gale_alert(GALE_ERROR,"GALE_DOMAIN not set",0);

	if (uname(&un) < 0) gale_alert(GALE_ERROR,"uname",errno);

	if (!gale_var(G_("HOST")).l)
		gale_set(G_("HOST"),
			gale_text_from(gale_global->enc_system,un.nodename,-1));

	host = gethostbyname(un.nodename);
	if (NULL == host) {
		gale_create(gale_global->local_addrs);
		gale_global->local_addrs->s_addr = 0;
	} else {
		int num;
		assert(AF_INET == host->h_addrtype);
		assert(sizeof(gale_global->local_addrs[0]) == host->h_length);
		for (num = 0; NULL != host->h_addr_list[num]; ++num) ;
		gale_create_array(gale_global->local_addrs,1 + num);
		for (num = 0; NULL != host->h_addr_list[num]; ++num)
			memcpy(
				&gale_global->local_addrs[num],
				host->h_addr_list[num],
				host->h_length);
		gale_global->local_addrs[num].s_addr = 0;
	}

	if (!gale_var(G_("LOGNAME")).l)
		gale_set(G_("LOGNAME"),gale_text_from(
			gale_global->enc_system,pwd->pw_name,-1));

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
		gale_set(G_("GALE_FROM"),gale_text_from(
			gale_global->enc_system,name,-1));
	}

	if (!gale_var(G_("GALE_ID")).l)
		gale_set(G_("GALE_ID"),gale_text_from(
			gale_global->enc_system,pwd->pw_name,-1));
}

void gale_restart(void) {
	assert(main_argv[main_argc] == NULL);
	alarm(0);
	execvp(main_argv[0],main_argv);
	gale_alert(GALE_WARNING,main_argv[0],errno);
}

static void *on_restart(oop_source *source,int sig,void *user) {
	gale_alert(GALE_NOTICE,"SIGUSR1 received, restarting",0);
	gale_restart();
	return OOP_HALT;
}

static void *on_report(oop_source *source,int sig,void *user) {
	struct gale_text fn = dir_file(gale_global->dot_gale,
		gale_text_concat(4,
			G_("report."),
			gale_text_from(
				gale_global->enc_system,
				gale_global->error_prefix,-1),
			G_("."),
			gale_text_from_number(getpid(),10,0)));

	FILE *fp = fopen(gale_text_to(gale_global->enc_system,fn),"w");
	if (NULL == fp) 
		gale_alert(GALE_WARNING,
			gale_text_to(gale_global->enc_console,fn),errno);
	else {
		fputs(gale_text_to(gale_global->enc_system,
			gale_report_run(gale_global->report)),fp);
		fclose(fp);
	}

	return OOP_CONTINUE;
}

static void *on_cont(oop_source *source,int sig,void *user) {
	return OOP_CONTINUE;
}

static void *on_term(oop_source *source,int sig,void *user) {
	return OOP_HALT;
}

void gale_init_signals(oop_source *source) {
	source->on_signal(source,SIGUSR1,on_restart,NULL);
	source->on_signal(source,SIGUSR2,on_report,NULL);
	source->on_signal(source,SIGPIPE,on_cont,NULL);
	source->on_signal(source,SIGINT,on_term,NULL);
	source->on_signal(source,SIGQUIT,on_term,NULL);
	source->on_signal(source,SIGHUP,on_term,NULL);
	source->on_signal(source,SIGTERM,on_term,NULL);
}

void gale_init(const char *s,int argc,char * const *argv) {
	struct passwd *pwd = NULL;
	char *user;

	if (getuid() != geteuid()) {
		environ = malloc(sizeof(*environ));
		environ[0] = NULL;
	}

	main_argc = argc;
	main_argv = argv;

	oop_malloc = gale_malloc_safe;
	oop_free = gale_free;

#ifdef HAVE_SOCKS
	SOCKSinit(s);
#endif

	/* Identify the user. */

	if ((user = getenv("LOGNAME"))) pwd = getpwnam(user);
	if (!pwd) pwd = getpwuid(geteuid());
	if (!pwd) gale_alert(GALE_ERROR,"you do not exist",0);

	/* Set up global variables. */

	_gale_globals(pwd);
	gale_global->error_prefix = s;
	gale_global->report = gale_make_report(NULL);

	/* Install AKD handler. */

	old_find = gale_global->find_public;
	gale_global->find_public = find_id;

	/* Round out the environment. */

	init_vars(pwd);
}
