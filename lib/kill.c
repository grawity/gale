#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <signal.h>

#include "gale/all.h"

struct gale_text dotfile = { NULL, 0 };

static void remove_dotfile(void *data) {
	if (0 != dotfile.l) {
		struct gale_text df = dir_file(gale_global->dot_gale,dotfile);
		unlink(gale_text_to(gale_global->enc_system,df));
	}
}

static int send_kill(int pid,int sig,const char *name) {
	if (!kill(pid,sig)) {
		gale_alert(GALE_NOTICE,gale_text_to(gale_global->enc_console,
			gale_text_concat(4,
				G_("sent "),
				gale_text_from(gale_global->enc_system,name,-1),
				G_(" signal to process "),
				gale_text_from_number(pid,10,0))),0);
		return 1;
	}

	if (ESRCH != errno && ENOENT != errno)
		gale_alert(GALE_WARNING,"kill",errno);
	return 0;
}

static int wait_for(int pid) {
	static const unsigned long delay[] = { 10, 40, 150, 300, 500 };
	int i = 0;

	for (i = 0; i < sizeof(delay) / sizeof(delay[0]); ++i) {
		struct timeval tv;
		if (kill(pid,0)) return 1;
		gettimeofday(&tv,NULL);
		tv.tv_sec = 0;
		tv.tv_usec = 1000 * delay[0];
		select(0,NULL,NULL,NULL,&tv);
	}

	return !!kill(pid,0);
}

static void terminate(int pid) {
	if (send_kill(pid,SIGTERM,"TERM")
	&& !wait_for(pid)) send_kill(pid,SIGKILL,"KILL");
}

void gale_kill(struct gale_text class,int do_kill) {
	int fd,len,pid = getpid();
	DIR *pdir;
	struct dirent *de;
	const char *df;

	dotfile = gale_text_concat(6,
		gale_text_from(
			gale_global->enc_system,
			gale_global->error_prefix,-1),G_("."),
		gale_var(G_("HOST")),G_("."),
		class,G_("."));
	len = dotfile.l;
	dotfile = gale_text_concat(2,dotfile,gale_text_from_number(pid,10,0));

	gale_cleanup(remove_dotfile,NULL);
	df = gale_text_to(gale_global->enc_system,
		dir_file(gale_global->dot_gale,dotfile));
	fd = creat(df,0666);
	if (fd >= 0) 
		close(fd);
	else
		gale_alert(GALE_WARNING,
			gale_text_to(gale_global->enc_console,dotfile),errno);

	if (do_kill) {
		pdir = opendir(gale_text_to(gale_global->enc_system,
			dir_file(gale_global->dot_gale,G_("."))));
		if (pdir == NULL) {
			gale_alert(GALE_WARNING,"opendir",errno);
			return;
		}

		while ((de = readdir(pdir))) {
			struct gale_text dn;
			dn = gale_text_from(
				gale_global->enc_system,
				de->d_name,-1);
			if (!gale_text_compare(
				gale_text_left(dn,len),
				gale_text_left(dotfile,len))) {
				int kpid = gale_text_to_number(
					gale_text_right(dn,-len));
				if (kpid != pid) {
					terminate(kpid);
					unlink(gale_text_to(gale_global->enc_system,
						dir_file(gale_global->dot_gale,dn)));
				}
			}
		}

		closedir(pdir);
	}
}

struct watch {
	int fd;
	struct timeval tv;
};

static void *on_watch(oop_source *source,struct timeval tv,void *user) {
	struct watch *watch = (struct watch *) user;
	sigset_t set,oldset;
	sigfillset(&set);
	sigprocmask(SIG_BLOCK,&set,&oldset);
	if (!isatty(watch->fd)) 
		raise(SIGHUP);
	else {
		gettimeofday(&tv,NULL);
		tv.tv_sec += 15;
		source->on_time(source,tv,on_watch,watch);
	}
	sigprocmask(SIG_SETMASK,&oldset,NULL);
	return OOP_CONTINUE;
}

void gale_watch_tty(oop_source *source,int fd) {
	struct watch *watch;
	gale_create(watch);
	watch->fd = fd;
	watch->tv = OOP_TIME_NOW;
	source->on_time(source,watch->tv,on_watch,watch);
}
