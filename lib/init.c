#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <pwd.h>

#include "gale/all.h"

struct gale_dir *dot_gale,*home_dir,*sys_dir;
struct gale_id *user_id;

static void init_vars(struct passwd *pwd) {
	char *tmp;
	struct utsname un;

	if (!getenv("GALE_DOMAIN"))
		gale_alert(GALE_ERROR,"GALE_DOMAIN not set",0);

	if (uname(&un)) gale_alert(GALE_ERROR,"uname",errno);

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

	tmp = getenv("GALE_ID");
	user_id = lookup_id(tmp ? tmp : pwd->pw_name);

	tmp = getenv("GALE_FROM");
	if (tmp) {
		gale_free(user_id->comment);
		user_id->comment = gale_strdup(tmp);
	}

	if (!getenv("GALE_SUBS")) {
		tmp = id_category(user_id,"GALE_SUBS=user","");
		putenv(tmp);
	}
}

static void read_conf(const char *s) {
	int num;
	FILE *fp = fopen(s,"r");
	if (fp == NULL) return;
	do {
		char ch,var[40],value[256];
		while (fscanf(fp," #%*[^\n]%c",&ch) == 1) ;
		num = fscanf(fp,"%39s %255[^\n]",var,value);
		if (num == 2) {
			char *both,*prev = getenv(var);
			if (prev && prev[0]) continue;
			both = gale_malloc(strlen(var) + strlen(value) + 2);
			sprintf(both,"%s=%s",var,value);
			putenv(both);
		}
	} while (num == 2);
}

void gale_init(const char *s) {
	struct passwd *pwd = NULL;
	char *user,*dir;

	if ((user = getenv("LOGNAME"))) pwd = getpwnam(user);
	if (!pwd) pwd = getpwuid(getuid());
	if (!pwd) gale_alert(GALE_ERROR,"you do not exist",0);

	gale_error_prefix = s;

	dir = getenv("GALE_SYS_DIR");
	if (!dir) dir = SYS_DIR;
	sys_dir = make_dir(dir,0);

	read_conf(dir_file(sys_dir,"conf"));

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

	init_vars(pwd);
}
