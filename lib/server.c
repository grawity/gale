#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <stdarg.h>
#include <syslog.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>

#include "gale/server.h"
#include "gale/compat.h"
#include "gale/error.h"

int gale_debug = 0;
static void (*cleanup)(void);

static void terminate(void) {
	if (cleanup) cleanup();
	cleanup = NULL;
}

static void sig(int sig) {
	terminate();
	exit(sig);
}

void gale_dprintf(int level,const char *fmt,...) {
	va_list ap;
	if (level >= gale_debug) return;
	va_start(ap,fmt);
	vfprintf(stderr,fmt,ap);
	va_end(ap);
}

void gale_daemon(int keep_tty) {
	if (!gale_debug) {
		if (keep_tty) {
			signal(SIGINT,SIG_IGN);
			signal(SIGQUIT,SIG_IGN);
			signal(SIGTTOU,SIG_IGN);
		} else {
			int fd;
			gale_error_handler = gale_error_syslog;
			setsid();
			fd = open("/dev/null",O_RDWR);
			if (fd >= 0) {
				dup2(fd,0);
				dup2(fd,1);
				dup2(fd,2);
				if (fd > 2) close(fd);
			}
		}
		if (fork()) exit(0);
	}
}

void gale_cleanup(void (*func)(void)) {
	static int first = 1;
	cleanup = func;
	if (first) {
		atexit(terminate);
		signal(SIGINT,sig);
		signal(SIGHUP,sig);
		signal(SIGTERM,sig);
	}
}
