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

struct gale_dir *dot_gale,*home_dir,*sys_dir;
struct gale_id *user_id;

static int main_argc;
static char * const *main_argv;

static sigset_t blocked;

extern char **environ;

static void init_vars(struct passwd *pwd) {
	char *tmp;
	struct utsname un;

	if (!getenv("GALE_DOMAIN"))
		gale_alert(GALE_ERROR,"GALE_DOMAIN not set",0);

	if (uname(&un) < 0) gale_alert(GALE_ERROR,"uname",errno);

	if (!getenv("HOST")) {
		tmp = gale_malloc(strlen(un.nodename) + 30);
		sprintf(tmp,"HOST=%s",un.nodename);
		putenv(tmp);
	}

	if (!getenv("LOGNAME")) {
		tmp = gale_malloc(strlen(pwd->pw_name) + 30);
		sprintf(tmp,"LOGNAME=%s",pwd->pw_name);
		putenv(tmp);
	}

	{
		char *buf,*old = getenv("PATH");
		int len = 5 + (old ? strlen(old) : 0);

		len += strlen(dir_file(dot_gale,"bin")) + 1;
		len += strlen(dir_file(sys_dir,"bin")) + 1;
		len += strlen(dir_file(dot_gale,".")) + 1;

		buf = gale_malloc(len + 1);
		strcpy(buf,"PATH=");
		strcat(buf,dir_file(dot_gale,"bin")); strcat(buf,":");
		strcat(buf,dir_file(sys_dir,"bin")); strcat(buf,":");
		strcat(buf,dir_file(dot_gale,".")); 
		if (old && *old) strcat(buf,":");

		if (!old || strncmp(buf + 5,old,strlen(buf) - 5)) {
			if (old) strcat(buf,old);
			putenv(buf);
		}
	}

	tmp = getenv("GALE_ID");
	user_id = lookup_id(tmp ? tmp : pwd->pw_name);

	tmp = getenv("GALE_FROM");
	if (!tmp) {
		char *gecos = pwd->pw_gecos;
		char *comma = strchr(gecos,',');
		if (!comma) comma = gecos + strlen(gecos);
		tmp = gale_malloc(30 + comma - gecos);
		sprintf(tmp,"GALE_FROM=%.*s",comma - gecos,gecos);
		putenv(tmp);
	}

#if 0
	if (tmp) {
		gale_free(user_id->comment);
		user_id->comment = gale_strdup(tmp);
	}
#endif

	if (!getenv("GALE_SUBS")) {
		tmp = id_category(user_id,"GALE_SUBS=user","");
		putenv(tmp);
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

	if (!size) return NULL;
	buf[size++] = '\0';
	return buf;
}

static void read_conf(const char *fn) {
	FILE *fp = fopen(fn,"r");
	char *s = read_line(fp);

	while (s) {
		char *var,*value,*both,*prev;

		while (*s && isspace(*s)) ++s;
		if (!*s || *s == '#') {
			s = read_line(fp);
			continue;
		}
		var = s;

		while (*s && !isspace(*s)) ++s;
		value = s;
		while (*value && isspace(*value)) ++value;
		*s = '\0';

		prev = getenv(var);
		if (prev && prev[0]) continue;
		both = gale_malloc(strlen(var) + strlen(value) + 2);
		sprintf(both,"%s=%s",var,value);

		s = read_line(fp);
		while (s && *s && isspace(*s)) {
			char *old = both;
			do ++s; while (*s && isspace(*s));
			if (*s == '#') break;
			
			both = gale_malloc(strlen(old) + strlen(s) + 1);
			sprintf(both,"%s%s",old,s);
			gale_free(old);

			s = read_line(fp);
		}

		putenv(both);
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
	char *user,*dir;
	struct sigaction act;
	sigset_t empty;

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

	dir = getenv("HOME");
	if (!dir) dir = pwd->pw_dir;
	home_dir = make_dir(dir,0777);

	dir = getenv("GALE_DIR");
	if (dir) dot_gale = make_dir(dir,0777);
	else {
		dot_gale = dup_dir(home_dir);
		sub_dir(dot_gale,".gale",0777);
	}

	read_conf(dir_file(dot_gale,"conf"));

	dir = getenv("GALE_SYS_DIR");
	if (!dir) dir = SYS_DIR;
	sys_dir = make_dir(dir,0);

	read_conf(dir_file(sys_dir,"conf"));

	init_vars(pwd);
}
