#include "gale/all.h"

#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <stdarg.h>
#include <syslog.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>

static struct link {
	void (*func)(void);
	pid_t pid;
	struct link *next;
} *list = NULL;

void gale_do_cleanup(void) {
	pid_t pid = getpid();
	while (list) {
		if (pid == list->pid) list->func();
		list = list->next;
	}
}

static void sig(int sig) {
	gale_do_cleanup();
	exit(sig);
}

static void ignore_sig(int sig) {
	struct sigaction act;
	if (sigaction(sig,NULL,&act)) gale_alert(GALE_ERROR,"sigaction",errno);
	act.sa_handler = SIG_IGN;
	if (sigaction(sig,&act,NULL)) gale_alert(GALE_ERROR,"sigaction",errno);
}

static void trap_sig(int num) {
	struct sigaction act;
	if (sigaction(num,NULL,&act)) gale_alert(GALE_ERROR,"sigaction",errno);
	if (act.sa_handler != SIG_DFL) return;
	act.sa_handler = sig;
	if (sigaction(num,&act,NULL)) gale_alert(GALE_ERROR,"sigaction",errno);
}

static void debug(int level,int idelta,const char *fmt,va_list ap) {
	static int indent = 0;
	int i;
	if (idelta < 0) indent += idelta;
	if (level < gale_global->debug_level) {
		for (i = 0; i < indent; ++i) fputc(' ',stderr);
		vfprintf(stderr,fmt,ap);
		fflush(stderr);
	}
	if (idelta > 0) indent += idelta;
}

void gale_dprintf(int level,const char *fmt,...) {
	va_list ap;
	va_start(ap,fmt);
	debug(level,0,fmt,ap);
	va_end(ap);
}

void gale_diprintf(int level,int indent,const char *fmt,...) {
	va_list ap;
	if (level >= gale_global->debug_level) return;
	va_start(ap,fmt);
	debug(level,indent,fmt,ap);
	va_end(ap);
}

void gale_daemon(int keep_tty) {
	if (!gale_global->debug_level) {
		if (keep_tty) {
			ignore_sig(SIGINT);
			ignore_sig(SIGQUIT);
			ignore_sig(SIGTTOU);
		} else {
			int fd;
			gale_global->error_handler = gale_error_syslog;
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
	struct link *l;
	gale_create(l);
	l->func = func;
	l->next = list;
	l->pid = getpid();
	list = l;
	if (l->next == NULL) {
		atexit(gale_do_cleanup);
		trap_sig(SIGINT);
		trap_sig(SIGQUIT);
		trap_sig(SIGHUP);
		trap_sig(SIGTERM);
	}
}
