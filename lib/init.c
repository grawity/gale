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

char *gale_idtocat(const char *prefix,const char *id,const char *suffix) {
	char *at = strrchr(id,'@');
	char *tmp;
	int len = strlen(prefix) + strlen(id) + strlen(suffix);
	if (at) {
		tmp = gale_malloc(len + 3);
		sprintf(tmp,"%s/%s/",prefix,at + 1);
		strncat(tmp,id,at - id);
	} else {
		const char *domain = getenv("GALE_DOMAIN");
		tmp = gale_malloc(len + strlen(domain) + 4);
		sprintf(tmp,"%s/%s/%s",prefix,domain,id);
	}
	if (suffix) {
		strcat(tmp,"/");
		strcat(tmp,suffix);
	}
	return tmp;
}

static void init_vars(struct passwd *pwd) {
	char *tmp;
	const char *domain,*id,*reply;

	domain = getenv("GALE_DOMAIN");
	if (!domain) gale_alert(GALE_ERROR,"GALE_DOMAIN not set",0);

	if (!getenv("LOGNAME")) {
		tmp = gale_malloc(strlen(pwd->pw_name) + 30);
		sprintf(tmp,"LOGNAME=%s",pwd->pw_name);
		putenv(tmp);
	}

	id = getenv("GALE_ID");
	if (!id) {
		tmp = gale_malloc(strlen(pwd->pw_name) + strlen(domain) + 30);
		sprintf(tmp,"GALE_ID=%s@%s",pwd->pw_name,domain);
		putenv(tmp);
		id = getenv("GALE_ID");
	} else if (!strchr(id,'@')) {
		tmp = gale_malloc(strlen(id) + strlen(domain) + 30);
		sprintf(tmp,"GALE_ID=%s@%s",id,domain);
		putenv(tmp);
		id = getenv("GALE_ID");
	}

	reply = getenv("GALE_REPLY_TO");
	if (!reply) {
		char *cat = gale_idtocat("user",id,"");
		tmp = gale_malloc(strlen(cat) + 30);
		sprintf(tmp,"GALE_REPLY_TO=%s",cat);
		gale_free(cat);
		putenv(tmp);
		reply = getenv("GALE_REPLY_TO");
	}
	if (!getenv("GALE_FROM")) {
		const char *name = strtok(pwd->pw_gecos,",");
		tmp = gale_malloc(strlen(id) + strlen(name) + 30);
		sprintf(tmp,"GALE_FROM=%s <%s>",name,id);
		putenv(tmp);
	}

	if (!getenv("GALE_SUBS")) {
		tmp = gale_malloc(strlen(reply) + 30);
		sprintf(tmp,"GALE_SUBS=%s",reply);
		putenv(tmp);
	}
}

static void read_conf(const char *s) {
	char ch,var[40],value[256];
	int num;
	FILE *fp = fopen(s,"r");
	if (fp == NULL) return;
	do {
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
