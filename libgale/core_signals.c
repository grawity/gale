#include "gale/core.h"
#include "gale/misc.h"
#include "gale/globals.h"
#include "oop.h"

#include <assert.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>

void gale_restart(void) {
	assert(gale_global->main_argv[gale_global->main_argc] == NULL);
	alarm(0);
	execvp(gale_global->main_argv[0],gale_global->main_argv);
	gale_alert(GALE_WARNING,
	    gale_text_from(
		gale_global->enc_cmdline,
		gale_global->main_argv[0],-1),errno);
}

static void *on_restart(oop_source *source,int sig,void *user) {
	gale_alert(GALE_NOTICE,G_("SIGUSR1 received, restarting"),0);
	gale_restart();
	return OOP_HALT;
}

static void *on_report(oop_source *source,int sig,void *user) {
	struct gale_text fn = dir_file(gale_global->dot_gale,
		gale_text_concat(4,
			G_("report."),
			gale_text_from(NULL,gale_global->error_prefix,-1),
			G_("."),
			gale_text_from_number(getpid(),10,0)));

	FILE *fp = fopen(gale_text_to(gale_global->enc_filesys,fn),"w");
	if (NULL == fp) 
		gale_alert(GALE_WARNING,fn,errno);
	else {
		fputs(gale_text_to(gale_global->enc_filesys,
			gale_report_run(gale_global->report)),fp);
		fclose(fp);
	}

	return OOP_CONTINUE;
}

static void *on_cont(oop_source *source,int sig,void *user) {
	return OOP_CONTINUE;
}

static void *on_halt(oop_source *source,int sig,void *user) {
	return OOP_HALT;
}

void gale_init_signals(oop_source *source) {
	source->on_signal(source,SIGUSR1,on_restart,NULL);
	source->on_signal(source,SIGUSR2,on_report,NULL);
	source->on_signal(source,SIGPIPE,on_cont,NULL);
	source->on_signal(source,SIGINT,on_halt,NULL);
	source->on_signal(source,SIGHUP,on_halt,NULL);
	source->on_signal(source,SIGTERM,on_halt,NULL);
	source->on_signal(source,SIGQUIT,on_halt,NULL);
}


struct gale_cleanup {
	void (*func)(void *);
	void *data;
	pid_t pid;
	struct gale_cleanup *next;
};

/** Daemonize (go into the background).
 *  \param source Liboop event source to use for signals. */
void gale_daemon(oop_source *source) {
	if (!gale_global->debug_level) {
		/* Ignore these signals, or else we'll get killed as a
		   side effect of other terminal activity. */
		source->on_signal(source,SIGTTOU,on_cont,NULL);
		source->on_signal(source,SIGHUP,on_cont,NULL);
		source->on_signal(source,SIGINT,on_cont,NULL);
		source->on_signal(source,SIGQUIT,on_cont,NULL);
		source->cancel_signal(source,SIGHUP,on_halt,NULL);
		source->cancel_signal(source,SIGINT,on_halt,NULL);
		source->cancel_signal(source,SIGQUIT,on_halt,NULL);
		if (0 != fork()) exit(0);
		setsid();
	}
}

/** Detach from the terminal (like galed but unlike gsub). 
 *  \param source Liboop event source to use for signals. */
void gale_detach(oop_source *source) {
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

		/* Now that we're detached, these signals are OK again. */
		source->cancel_signal(source,SIGHUP,on_cont,NULL);
		source->cancel_signal(source,SIGINT,on_cont,NULL);
		source->cancel_signal(source,SIGQUIT,on_cont,NULL);
		source->on_signal(source,SIGHUP,on_halt,NULL);
		source->on_signal(source,SIGINT,on_halt,NULL);
		source->on_signal(source,SIGQUIT,on_halt,NULL);
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
