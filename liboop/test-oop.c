/* test-oop.c, liboop, copyright 1999 Dan Egnor

   This is free software; you can redistribute it and/or modify it under the
   terms of the GNU Lesser General Public License, version 2.1 or later.
   See the file COPYING for details. */

#include <stdio.h>
#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "oop.h"

#ifdef HAVE_GLIB
#include <glib.h>
#include "oop-glib.h"
GMainLoop *glib_loop;
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
"sinks:   timer    some timers\n"
"         echo     a stdin->stdout copy\n"
"         signal   some signal handlers\n"
#ifdef HAVE_ADNS
"         adns     some asynchronous DNS lookups\n"
#endif
#ifdef HAVE_WWW
"         libwww   some HTTP GET operations\n"
#endif
	,stderr);
	exit(1);
}

/* -- timer ---------------------------------------------------------------- */

oop_call_time on_timer;
void *on_timer(oop_source *source,struct timeval tv,void *data) {
	struct timer *timer = (struct timer *) data;
	timer->tv = tv;
	timer->tv.tv_sec += timer->delay;
	source->on_time(source,timer->tv,on_timer,data);
	printf("timer: once every ");
	if (1 == timer->delay) printf("second\n");
	else printf("%d seconds\n",timer->delay);
	return OOP_CONTINUE;
}

oop_call_signal stop_timer;
void *stop_timer(oop_source *source,int sig,void *data) {
	struct timer *timer = (struct timer *) data;
	source->cancel_time(source,timer->tv,on_timer,timer);
	source->cancel_signal(source,SIGQUIT,stop_timer,timer);
	return OOP_CONTINUE;
}

void add_timer(oop_source *source,int interval) {
	struct timer *timer = malloc(sizeof(*timer));
	gettimeofday(&timer->tv,NULL);
	timer->delay = interval;
	source->on_signal(source,SIGQUIT,stop_timer,timer);
	on_timer(source,timer->tv,timer);
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

/* -- adns ----------------------------------------------------------------- */

#ifdef HAVE_ADNS

#include "adns.h"
#include "oop-adns.h"

#define NUM_Q 6
oop_adns_query *q[NUM_Q];
oop_adapter_adns *adns;

void *stop_lookup(oop_source *src,int sig,void *data) {
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

	src->cancel_signal(src,SIGQUIT,stop_lookup,NULL);
	return OOP_CONTINUE;
}

void *on_lookup(oop_adapter_adns *adns,adns_answer *reply,void *data) {
	int i;
	for (i = 0; i < NUM_Q; ++i) if (data == &q[i]) q[i] = NULL;

	printf("adns: %s =>",reply->owner);
	if (adns_s_ok != reply->status) {
		printf(" error: %s\n",adns_strerror(reply->status));
		return OOP_CONTINUE;
	}
	if (NULL != reply->cname) printf(" (%s)",reply->cname);
	assert(adns_r_a == reply->type);
	for (i = 0; i < reply->nrrs; ++i)
		printf(" %s",inet_ntoa(reply->rrs.inaddr[i]));
	printf("\n");
	free(reply);

	return OOP_CONTINUE;
}

static void get_name(int i,const char *name) {
	q[i] = oop_adns_submit(
	       adns,name,adns_r_a,adns_qf_owner,
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

/* Yuck: */
#define HAVE_CONFIG_H
#undef PACKAGE
#undef VERSION

#include "oop.h"
#include "HTEvent.h"
#include "oop-www.h"

#include "WWWLib.h"
#include "WWWInit.h"

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

void add_www(oop_source *source) {
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

void add_www(oop_source *source) {
	fputs("sorry, libwww not available\n",stderr);
	usage();
}

#endif

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
	||  !strcmp(name,"signal"))
		oop_sys_run(source_sys);

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

	if (!strcmp(name,"echo")) {
		src->on_fd(src,0,OOP_READ,on_data,NULL);
		src->on_signal(src,SIGQUIT,stop_data,NULL);
		return;
	}

	if (!strcmp(name,"signal")) {
		src->on_signal(src,SIGINT,on_signal,NULL);
		src->on_signal(src,SIGQUIT,on_signal,NULL);
		return;
	}

#ifdef HAVE_ADNS
	if (!strcmp(name,"adns")) {
		add_adns(src);
		return;
	}
#endif

#ifdef HAVE_WWW
	if (!strcmp(name,"libwww")) {
		add_www(src);
		return;
	}
#endif

	fprintf(stderr,"unknown sink \"%s\"\n",name);
	usage();
}

int main(int argc,char *argv[]) {
	oop_source *source;
	int i;

	if (argc < 3) usage();
	puts("test-oop: use ^\\ (SIGQUIT) for clean shutdown");
	source = create_source(argv[1]);
	source->on_signal(source,SIGQUIT,stop_loop,NULL);
	for (i = 2; i < argc; ++i)
		add_sink(source,argv[i]);

	run_source(argv[1]);
	delete_source(argv[1]);
	return 0;
}
