/* test-oop.c, liboop, copyright 1999 Dan Egnor

   This is free software; you can redistribute it and/or modify it under the
   terms of the GNU Lesser General Public License, version 2.1 or later.
   See the file COPYING for details. */

#include <stdio.h>
#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "oop.h"
#include "oop-read.h"

#ifdef HAVE_GLIB
#include <glib.h>
#include "oop-glib.h"
GMainLoop *glib_loop;
#endif

#ifdef HAVE_TCL
#include <tcl.h>
#include "oop-tcl.h"
#endif

#ifdef HAVE_WWW
/* Yuck: */
#define HAVE_CONFIG_H
#undef PACKAGE
#undef VERSION

#include "oop.h"
#include "HTEvent.h"
#include "oop-www.h"

#include "WWWLib.h"
#include "WWWInit.h"
#endif

#ifdef HAVE_READLINE
#include <readline/readline.h>
#include "oop-rl.h"
#endif

struct timer {
	struct timeval tv;
	int delay;
};

static oop_source_sys *source_sys;
static oop_adapter_signal *source_signal;

static void usage(void) {
	fputs(
"usage:   test-oop <source> <sink> [<sink> ...]\n"
"sources: sys      system event source\n"
"         signal   system event source with signal adapter\n"
#ifdef HAVE_GLIB
"         glib     GLib source adapter\n"
#endif
#ifdef HAVE_TCL
"         tcl      Tcl source adapter\n"
#endif
"sinks:   timer    some timers\n"
"         signal   some signal handlers\n"
"         echo     a stdin->stdout copy\n"
#ifdef HAVE_READLINE
"         readline like echo but with line editing\n"
#endif
#ifdef HAVE_ADNS
"         adns     some asynchronous DNS lookups\n"
#endif
#ifdef HAVE_WWW
"         libwww   some HTTP GET operations\n"
#endif
"         read:<delim-spec><nul-mode><shortrec-mode>[<maxrecsz>][,<bufsz>][:<data>]\n"
"                  <delim-spec>:    n | s<delim> | k<delim>\n"
"                  <delim>:         =<char> | n | <hex><hex>\n"
"                  <nul-mode>:      f | d | p\n"
"                  <shortrec-mode>: f | e | l | s\n"
	,stderr);
	exit(1);
}

/* -- timer ---------------------------------------------------------------- */

static oop_call_time on_timer;
static void *on_timer(oop_source *source,struct timeval tv,void *data) {
	struct timer *timer = (struct timer *) data;
	timer->tv = tv;
	timer->tv.tv_sec += timer->delay;
	source->on_time(source,timer->tv,on_timer,data);
	printf("timer: once every ");
	if (1 == timer->delay) printf("second\n");
	else printf("%d seconds\n",timer->delay);
	return OOP_CONTINUE;
}

static oop_call_signal stop_timer;
static void *stop_timer(oop_source *source,int sig,void *data) {
	struct timer *timer = (struct timer *) data;
	source->cancel_time(source,timer->tv,on_timer,timer);
	source->cancel_signal(source,SIGQUIT,stop_timer,timer);
	return OOP_CONTINUE;
}

static void add_timer(oop_source *source,int interval) {
	struct timer *timer = malloc(sizeof(*timer));
	gettimeofday(&timer->tv,NULL);
	timer->delay = interval;
	source->on_signal(source,SIGQUIT,stop_timer,timer);
	on_timer(source,timer->tv,timer);
}

/* -- signal --------------------------------------------------------------- */

static oop_call_signal on_signal;
static void *on_signal(oop_source *source,int sig,void *data) {
	switch (sig) {
	case SIGINT:
		puts("signal: SIGINT (control-C) caught.  "
		     "(Use SIGQUIT, control-\\, to terminate.)");
		break;
	case SIGQUIT:
		puts("signal: SIGQUIT (control-\\) caught, terminating.");
		source->cancel_signal(source,SIGINT,on_signal,NULL);
		source->cancel_signal(source,SIGQUIT,on_signal,NULL);
		break;
	default:
		assert(0 && "unknown signal?");
	}
	return OOP_CONTINUE;
}

/* -- echo ----------------------------------------------------------------- */

static oop_call_fd on_data;
static void *on_data(oop_source *source,int fd,oop_event event,void *data) {
	char buf[BUFSIZ];
	int r = read(fd,buf,sizeof(buf));
	write(1,buf,r);
	return OOP_CONTINUE;
}

static oop_call_signal stop_data;
static void *stop_data(oop_source *source,int sig,void *data) {
	source->cancel_fd(source,0,OOP_READ);
	source->cancel_signal(source,SIGQUIT,stop_data,NULL);
	return OOP_CONTINUE;
}

/* -- readline ------------------------------------------------------------- */

#ifdef HAVE_READLINE

static void on_readline(const char *input) {
	if (NULL == input)
		puts("\rreadline: EOF");
	else {
		fputs("readline: \"",stdout);
		fputs(input,stdout);
		puts("\"");
	}
}

static void *stop_readline(oop_source *src,int sig,void *data) {
	oop_readline_cancel(src);
	src->cancel_signal(src,SIGQUIT,stop_readline,NULL);
	rl_callback_handler_remove();
	return OOP_CONTINUE;
}

static void add_readline(oop_source *src) {
	rl_callback_handler_install(
		(char *) "> ", /* readline isn't const-correct */
		(VFunction *) on_readline);
	oop_readline_register(src);
	src->on_signal(src,SIGQUIT,stop_readline,NULL);
}

#else

static void add_readline(oop_source *src) {
	fputs("sorry, readline not available\n",stderr);
	usage();
}

#endif

/* -- adns ----------------------------------------------------------------- */

#ifdef HAVE_ADNS

#include "adns.h"
#include "oop-adns.h"

#undef ADNS_CANCEL
#define NUM_Q 6
oop_adns_query *q[NUM_Q];
oop_adapter_adns *adns;

static void cancel_adns(void) {
	int i;

	for (i = 0; i < NUM_Q; ++i)
		if (NULL != q[i]) {
			oop_adns_cancel(q[i]);
			q[i] = NULL;
		}

	if (NULL != adns) {
		oop_adns_delete(adns);
		adns = NULL;
	}
}

static void *stop_lookup(oop_source *src,int sig,void *data) {
	cancel_adns();
	src->cancel_signal(src,SIGQUIT,stop_lookup,NULL);
	return OOP_CONTINUE;
}

static void *on_lookup(oop_adapter_adns *adns,adns_answer *reply,void *data) {
	int i;
	for (i = 0; i < NUM_Q; ++i) if (data == &q[i]) q[i] = NULL;

	printf("adns: %s =>",reply->owner);
	if (adns_s_ok != reply->status)
		printf(" error: %s\n",adns_strerror(reply->status));
	else {
		if (NULL != reply->cname) printf(" (%s)",reply->cname);
		assert(adns_r_a == reply->type);
		for (i = 0; i < reply->nrrs; ++i)
			printf(" %s",inet_ntoa(reply->rrs.inaddr[i]));
		printf("\n");
	}

	free(reply);
#ifdef ADNS_CANCEL
	cancel_adns();
#endif
	return OOP_CONTINUE;
}

static void get_name(int i,const char *name) {
	q[i] = oop_adns_submit(
	       adns,NULL,name,adns_r_a,adns_qf_owner,
	       on_lookup,&q[i]);
}

static void add_adns(oop_source *src) {
	adns = oop_adns_new(src,0,NULL);
	get_name(0,"g.mp");
	get_name(1,"cnn.com");
	get_name(2,"slashdot.org");
	get_name(3,"love.ugcs.caltech.edu");
	get_name(4,"intel.ugcs.caltech.edu");
	get_name(5,"ofb.net");
	src->on_signal(src,SIGQUIT,stop_lookup,NULL);
}

#else

static void add_adns(oop_source *src) {
	fputs("sorry, adns not available\n",stderr);
	usage();
}

#endif

/* -- libwww --------------------------------------------------------------- */

#ifdef HAVE_WWW

static int remaining = 0;

static int on_print(const char *fmt,va_list args) {
	return (vfprintf(stdout,fmt,args));
}

static int on_trace (const char *fmt,va_list args) {
	return (vfprintf(stderr,fmt,args));
}

static int on_complete(HTRequest *req,HTResponse *resp,void *x,int status) {
	HTChunk *chunk = (HTChunk *) HTRequest_context(req);
	char *address = HTAnchor_address((HTAnchor *) HTRequest_anchor(req));

	HTPrint("%d: done with %s\n",status,address);
	HTMemory_free(address);
	HTRequest_delete(req);

	if (NULL != chunk) HTChunk_delete(chunk);

	if (0 == --remaining) {
	/* stop ... */
	}

	return HT_OK;
}

static void get_uri(const char *uri) {
	HTRequest *req = HTRequest_new();
	HTRequest_setOutputFormat(req, WWW_SOURCE);
	HTRequest_setContext(req,HTLoadToChunk(uri,req));
	++remaining;
}

static void *stop_www(oop_source *source,int sig,void *x) {
	oop_www_cancel();
	HTProfile_delete();
	source->cancel_signal(source,sig,stop_www,x);
	return OOP_CONTINUE;
}

static void add_www(oop_source *source) {
	puts("libwww: known bug: termination (^\\) may abort due to cached "
             "connections, sorry.");
	HTProfile_newNoCacheClient("test-www","1.0");
	oop_www_register(source);

	HTPrint_setCallback(on_print);
	HTTrace_setCallback(on_trace);

	HTNet_addAfter(on_complete, NULL, NULL, HT_ALL, HT_FILTER_LAST);
	HTAlert_setInteractive(NO);

	get_uri("http://ofb.net/~egnor/oop/");
	get_uri("http://ofb.net/does.not.exist");
	get_uri("http://slashdot.org/");
	get_uri("http://www.w3.org/Library/");
	get_uri("http://does.not.exist/");

	source->on_signal(source,SIGQUIT,stop_www,NULL);
}

#else

static void add_www(oop_source *source) {
	fputs("sorry, libwww not available\n",stderr);
	usage();
}

#endif

typedef void on_read_err_func(oop_source*,oop_read*);

static void *on_read(oop_source *source, oop_read *rd,
		     oop_rd_event event, const char *errmsg, int errnoval,
		     const char *data, size_t recsz, void *next_v) {
	on_read_err_func **next= next_v;
	size_t off;
	int c;

	printf("read %s %s%s%s%s %s ",
	       next ? "error" : "ok",

	       event == OOP_RD_OK      ? "OK" :
	       event == OOP_RD_EOF     ? "EOF" :
	       event == OOP_RD_PARTREC ? "PARTREC" :
	       event == OOP_RD_LONG    ? "LONG" :
	       event == OOP_RD_NUL     ? "NUL" :
	       event == OOP_RD_SYSTEM  ? "SYSTEM" :
	       (assert(!"event must be valid"), (char*)0),
	       
	       errmsg?" `":"", errmsg?errmsg:"", errmsg?"'":"",
	       errnoval ? strerror(errnoval) : "Zero");

	if (data) {

		printf("%lu:\"", (unsigned long)recsz);

		for (off=0; off<recsz; off++) {
			c = (unsigned char)data[off];
			if (c==' ' || (isprint(c) && !isspace(c))) {
				putchar(c);
			} else {
				switch (c) {
				case '\n': fputs("\\n",stdout); break;
				case '\t': fputs("\\t",stdout); break;
			default:   printf("\\x%02x",c);
				}
			}
		}

		assert(!data[recsz]);

		putchar('"');

	} else {
		fputs("null",stdout);
	}

	putchar('\n');

	if (next)
		(*next)(source,rd);

	return OOP_CONTINUE;
}

static void *stop_read(oop_source *src,int sig,void *rd_v) {
	oop_read *rd= rd_v;

	src->cancel_signal(src,SIGQUIT,stop_read,rd);
	oop_rd_delete_tidy(rd);
	return OOP_CONTINUE;
}

static void on_read_immed_err(oop_source *src, oop_read *rd) {
	puts("read: terminating");
	stop_read(src,0,rd);
}

static void on_read_std_err(oop_source *src, oop_read *rd) {
	static on_read_err_func *const next= on_read_immed_err;
	int r;

	puts("read: switching to plain immediate mode");
	r= oop_rd_read(rd,OOP_RD_STYLE_IMMED,0, on_read,0, on_read,(void*)&next);
	if (r) { perror("oop_rd_read[2]"); exit(1); }
}

static void read_bad(const char *errmsg) {
	fprintf(stderr,"invalid modes for read:...: %s\n",errmsg);
	usage();
}

static void add_read(oop_source *source, const char *modes) {
	static on_read_err_func *const next= on_read_std_err;

	oop_readable *ra;
	oop_read *rd;
	oop_rd_style style;
	size_t bufsz, maxrecsz;
	int r;
	char delimspec[3];
	char *ep;

	switch (*modes++) {
	case 'n': style.delim_mode= OOP_RD_DELIM_NONE;  break;
	case 's': style.delim_mode= OOP_RD_DELIM_STRIP; break;
	case 'k': style.delim_mode= OOP_RD_DELIM_KEEP;  break;
	default: read_bad("invalid delim_mode, must be one of nsk");
	}
	if (style.delim_mode != OOP_RD_DELIM_NONE) {
		switch ((delimspec[0] = *modes++)) {
		case '=': style.delim= *modes++; break;
		case 'n': style.delim= '\n'; break;
		case '\0': read_bad("missing delimiter");
		default:
			delimspec[1]= *modes++;
			delimspec[2]= 0;
			style.delim= strtoul(delimspec,&ep,16);
			if (ep != modes) read_bad("invalid delimiter");
		}
	}

	switch (*modes++) {
        case 'f': style.nul_mode= OOP_RD_NUL_FORBID;  break;
        case 'd': style.nul_mode= OOP_RD_NUL_DISCARD; break;
        case 'p': style.nul_mode= OOP_RD_NUL_PERMIT;  break;
	default: read_bad("invalid nul_mode, must be one of fdp");
	}

	switch (*modes++) {
        case 'f': style.shortrec_mode= OOP_RD_SHORTREC_FORBID;  break;
        case 'e': style.shortrec_mode= OOP_RD_SHORTREC_EOF;     break;
        case 'l': style.shortrec_mode= OOP_RD_SHORTREC_LONG;    break;
        case 's': style.shortrec_mode= OOP_RD_SHORTREC_SOONEST; break;
	default: read_bad("invalid shortrec_mode, must be one of fels");
	}

	maxrecsz= strtoul(modes,&ep,10);
	if (*ep && *ep != ',' && *ep != ':') read_bad("invalid maxrecsz");
	modes= *ep==',' ? ep+1 : ep;

	bufsz= strtoul(modes,&ep,10);
	if (*ep && *ep != ':') read_bad("invalid bufsz");

	if (*ep != ':') {
		ra= oop_readable_fd(source,0);
		if (!ra) { perror("oop_readable_fd"); exit(1); }
	} else {
		modes= ep+1;
		ra= oop_readable_mem(source,modes,strlen(modes));
		if (!ra) { perror("oop_readable_fd"); exit(1); }
	}		

	rd= oop_rd_new(source,ra,
		       bufsz ? malloc(bufsz) : 0,
		       bufsz);
	r= oop_rd_read(rd,&style,maxrecsz, on_read,0, on_read,(void*)&next);
	if (r) { perror("oop_rd_read"); exit(1); }
	source->on_signal(source,SIGQUIT,stop_read,rd);
}

/* -- core ----------------------------------------------------------------- */

static void *stop_loop_delayed(oop_source *source,struct timeval tv,void *x) {
	return OOP_HALT;
}

static void *stop_loop(oop_source *source,int sig,void *x) {
	/* give everyone else a chance to shut down. */
	source->on_time(source,OOP_TIME_NOW,stop_loop_delayed,NULL);
	source->cancel_signal(source,SIGQUIT,stop_loop,NULL);
	return OOP_CONTINUE;
}

static oop_source *create_source(const char *name) {
	if (!strcmp(name,"sys")) {
		source_sys = oop_sys_new();
		return oop_sys_source(source_sys);
	}

	if (!strcmp(name,"signal")) {
		source_sys = oop_sys_new();
		source_signal = oop_signal_new(oop_sys_source(source_sys));
		return oop_signal_source(source_signal);
	}

#ifdef HAVE_GLIB
	if (!strcmp(name,"glib")) {
		puts("glib: known bug: termination (^\\) won't quit, sorry.");
		glib_loop = g_main_new(FALSE);
		return oop_glib_new();
	}
#endif

	fprintf(stderr,"unknown source \"%s\"\n",name);
	usage();
	return NULL;
}

static void run_source(const char *name) {
	if (!strcmp(name,"sys")
	||  !strcmp(name,"signal")) {
		oop_sys_run_once(source_sys);
		oop_sys_run_once(source_sys);
		oop_sys_run_once(source_sys);
		oop_sys_run(source_sys);
	}

#ifdef HAVE_GLIB
	if (!strcmp(name,"glib"))
		g_main_run(glib_loop);
#endif
}

static void delete_source(const char *name) {
	if (!strcmp(name,"sys"))
		oop_sys_delete(source_sys);
	if (!strcmp(name,"signal")) {
		oop_signal_delete(source_signal);
		oop_sys_delete(source_sys);
	}

#ifdef HAVE_GLIB
	if (!strcmp(name,"glib")) {
		oop_glib_delete();
		g_main_destroy(glib_loop);
	}
#endif
}

static void add_sink(oop_source *src,const char *name) {
	if (!strcmp(name,"timer")) {
		add_timer(src,1);
		add_timer(src,2);
		add_timer(src,3);
		return;
	}

	if (!strcmp(name,"signal")) {
		src->on_signal(src,SIGINT,on_signal,NULL);
		src->on_signal(src,SIGQUIT,on_signal,NULL);
		return;
	}

	if (!strcmp(name,"echo")) {
		src->on_fd(src,0,OOP_READ,on_data,NULL);
		src->on_signal(src,SIGQUIT,stop_data,NULL);
		return;
	}

	if (!strcmp(name,"readline")) {
		add_readline(src);
		return;
	}

	if (!strcmp(name,"adns")) {
		add_adns(src);
		return;
	}

	if (!strcmp(name,"libwww")) {
		add_www(src);
		return;
	}

	if (!strncmp(name,"read:",5)) {
		add_read(src,name+5);
		return;
	}

	fprintf(stderr,"unknown sink \"%s\"\n",name);
	usage();
}

static void init(oop_source *source,int count,char *sinks[]) {
	int i;
	source->on_signal(source,SIGQUIT,stop_loop,NULL);
	puts("test-oop: use ^\\ (SIGQUIT) for clean shutdown or "
	     "^C (SIGINT) to stop abruptly.");

	for (i = 0; i < count; ++i)
		add_sink(source,sinks[i]);
}

/* -- tcl source ----------------------------------------------------------- */

#ifdef HAVE_TCL
static int tcl_count;
static char **tcl_sinks;

static int tcl_init(Tcl_Interp *interp) {
	init(oop_tcl_new(),tcl_count,tcl_sinks);
	return TCL_OK;
} 
#endif

/* -- main ----------------------------------------------------------------- */

int main(int argc,char *argv[]) {
	if (argc < 3) usage();

#ifdef HAVE_TCL
	if (!strcmp(argv[1],"tcl")) { /* Tcl is a little ... different. */
		tcl_count = argc - 2;
		tcl_sinks = argv + 2;
		Tcl_Main(1,argv,tcl_init);
	} else
#endif
	{
		oop_source * const source = create_source(argv[1]);
		init(source,argc - 2,argv + 2);
		run_source(argv[1]);
		delete_source(argv[1]);
	}

	return 0;
}
