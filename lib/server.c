#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <syslog.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>

#include "gale/server.h"
#include "gale/compat.h"

int gale_debug = 0;

void gale_dprintf(int level,const char *fmt,...) {
	va_list ap;
	if (level >= gale_debug) return;
	va_start(ap,fmt);
	vfprintf(stderr,fmt,ap);
	va_end(ap);
}

void gale_daemon(void) {
	int fd;
	if (!gale_debug) {
		setsid();
		fd = open("/dev/null",O_RDWR);
		if (fd >= 0) {
			dup2(fd,0);
			dup2(fd,1);
			dup2(fd,2);
			if (fd > 2) close(fd);
		}
		if (fork()) exit(0);
	}
}

void gale_die(char *s,int err) {
	if (err) {
		syslog(LOG_ERR,"fatal error (%s): %s\n",s,strerror(err));
		fprintf(stderr,"fatal error (%s): %s\n",s,strerror(err));
	} else {
		syslog(LOG_ERR,"fatal error: %s\n",s);
		fprintf(stderr,"fatal error: %s\n",s);
	}
	exit(1);
}

void gale_warn(char *s,int err) {
	if (err) {
		syslog(LOG_WARNING,"warning (%s): %s\n",s,strerror(err));
		gale_dprintf(0,"warning (%s): %s\n",s,strerror(err));
	} else {
		syslog(LOG_WARNING,"warning: %s\n",s);
		gale_dprintf(0,"warning: %s\n",s);
	}
	exit(1);
}
