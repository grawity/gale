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

static void remove_dotfile(void) {
	if (0 != dotfile.l) {
		struct gale_text df = dir_file(gale_global->dot_gale,dotfile);
		unlink(gale_text_to_local(df));
	}
}

void gale_kill(struct gale_text class,int do_kill) {
	int fd,len,pid = getpid();
	DIR *pdir;
	struct dirent *de;
	const char *df;

	dotfile = gale_text_concat(6,
		gale_text_from_local(gale_global->error_prefix,-1),G_("."),
		gale_var(G_("HOST")),G_("."),
		class,G_("."));
	len = dotfile.l;
	dotfile = gale_text_concat(2,dotfile,gale_text_from_number(pid,10,0));

	gale_cleanup(remove_dotfile);
	df = gale_text_to_local(dir_file(gale_global->dot_gale,dotfile));
	fd = creat(df,0666);
	if (fd >= 0) 
		close(fd);
	else
		gale_alert(GALE_WARNING,gale_text_to_local(dotfile),errno);

	if (do_kill) {
		pdir = opendir(gale_text_to_local(dir_file(gale_global->dot_gale,G_("."))));
		if (pdir == NULL) {
			gale_alert(GALE_WARNING,"opendir",errno);
			return;
		}

		while ((de = readdir(pdir))) {
			struct gale_text dn;
			dn = gale_text_from_local(de->d_name,-1);
			if (!gale_text_compare(
				gale_text_left(dn,len),
				gale_text_left(dotfile,len))) {
				int kpid = gale_text_to_number(
					gale_text_right(dn,-len));
				if (kpid != pid) {
					kill(kpid,SIGTERM);
					unlink(gale_text_to_local(
						dir_file(gale_global->dot_gale,dn)));
				}
			}
		}

		closedir(pdir);
	}
}

static int watch_fd;

static void alarm_received(int sig) {
	if (!isatty(watch_fd)) raise(SIGHUP);
	gale_watch_tty(watch_fd);
}

void gale_watch_tty(int fd) {
	struct sigaction act;
	watch_fd = fd;
	if (sigaction(SIGALRM,NULL,&act)) 
		gale_alert(GALE_ERROR,"sigaction",errno);
	act.sa_handler = alarm_received;
#ifdef SA_RESTART
	act.sa_flags |= SA_RESTART;
#endif
	if (sigaction(SIGALRM,&act,NULL)) 
		gale_alert(GALE_ERROR,"sigaction",errno);
	alarm(15);
}
