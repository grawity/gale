#include "gale/core.h"
#include "gale/misc.h"
#include "gale/globals.h"
#include "oop.h"

#include <assert.h>
#include <signal.h>
#include <unistd.h>

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

static void *on_term(oop_source *source,int sig,void *user) {
	return OOP_HALT;
}

void gale_init_signals(oop_source *source) {
	source->on_signal(source,SIGUSR1,on_restart,NULL);
	source->on_signal(source,SIGUSR2,on_report,NULL);
	source->on_signal(source,SIGPIPE,on_cont,NULL);
	source->on_signal(source,SIGINT,on_term,NULL);
	source->on_signal(source,SIGQUIT,on_term,NULL);
	source->on_signal(source,SIGHUP,on_term,NULL);
	source->on_signal(source,SIGTERM,on_term,NULL);
}
