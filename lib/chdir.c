#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "gale/client.h"

static void die(const char *s) {
	perror(s);
	exit(1);
}

void gale_subdir(const char *s,int perm) {
	if (chdir(s) && (errno != ENOENT || mkdir(s,perm) || chdir(s))) die(s);
}

void gale_unsubdir(void) {
	if (chdir("..")) die("..");
}

void gale_chdir(void) {
	static int did_chdir = 0;
	char *dir;
	if (did_chdir) return;
	did_chdir = 1;
	dir = getenv("GALE_DIR");
	if (!dir) {
		dir = getenv("HOME");
		if (!dir) {
			fprintf(stderr,"error: $HOME not set.\r\n");
			exit(1);
		}
		if (chdir(dir)) die(dir);
		gale_subdir(".gale",0777);
	} else if (chdir(dir)) die("dir");
}
