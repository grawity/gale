#include "gale/all.h"

#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <stdarg.h>
#include <syslog.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>

struct gale_cleanup {
	void (*func)(void *);
	void *data;
	pid_t pid;
	struct gale_cleanup *next;
};

static void *on_ignore(oop_source *source,int sig,void *data) {
	return OOP_CONTINUE;
}

/** Daemonize (go into the background).
 *  \param source Liboop event source to use for signals. */
void gale_daemon(oop_source *source) {
	if (!gale_global->debug_level) {
		if (0 != fork()) exit(0);
		setsid();
		source->on_signal(source,SIGTTOU,on_ignore,NULL);
	}
}

/** Detach from the terminal (like galed but unlike gsub). */
void gale_detach() {
	if (!gale_global->debug_level) {
		int fd = open("/dev/null",O_RDWR);
		if (fd >= 0) {
			dup2(fd,0);
			dup2(fd,1);
			dup2(fd,2);
			if (fd > 2) close(fd);
		} else {
			close(0);
			close(1);
			close(2);
		}
	}
}

void gale_do_cleanup(void) {
	pid_t pid = getpid();
	while (NULL != gale_global && NULL != gale_global->cleanup_list) {
		struct gale_cleanup *link = gale_global->cleanup_list;
		gale_global->cleanup_list = gale_global->cleanup_list->next;
		if (pid == link->pid) link->func(link->data);
	}
}

void gale_cleanup(void (*func)(void *),void *data) {
	struct gale_cleanup *l;
	gale_create(l);
	l->func = func;
	l->data = data;
	l->next = gale_global->cleanup_list;
	l->pid = getpid();
	gale_global->cleanup_list = l;
	if (l->next == NULL) atexit(gale_do_cleanup);
}
