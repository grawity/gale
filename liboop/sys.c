/* sys.c, liboop, copyright 1999 Dan Egnor

   This is free software; you can redistribute it and/or modify it under the
   terms of the GNU Lesser General Public License, version 2.1 or later.
   See the file COPYING for details. */

#include "oop.h"

#include <errno.h>
#include <assert.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <setjmp.h>
#include <string.h>

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>   /* Needed on NetBSD1.1/SPARC due to bzero/FD_ZERO. */
#endif

#ifdef HAVE_STRINGS_H
#include <strings.h>  /* Needed on AIX 4.2 due to bzero/FD_ZERO. */
#endif

#define MAGIC 0x9643

struct sys_time {
	struct sys_time *next;
	struct timeval tv;
	oop_call_time *f;
	void *v;
};

struct sys_signal_handler {
	struct sys_signal_handler *next;
	oop_call_signal *f;
	void *v;
};

struct sys_signal {
	struct sys_signal_handler *list,*ptr;
	struct sigaction old;
	volatile sig_atomic_t active;
};

struct sys_file_handler {
	oop_call_fd *f;
	void *v;
};

typedef struct sys_file_handler sys_file[OOP_NUM_EVENTS];

struct oop_source_sys {
	oop_source oop;
	int magic;
	int in_run;
	int num_events;

	/* Timeout queue */
	struct sys_time *time_queue,*time_run;

	/* Signal handling */
	struct sys_signal sig[OOP_NUM_SIGNALS];
	sigjmp_buf env;
	int do_jmp,sig_active;

	/* File descriptors */
	int num_files;
	sys_file *files;
};

struct oop_source_sys *sys_sig_owner[OOP_NUM_SIGNALS];

static oop_source_sys *verify_source(oop_source *source) {
	oop_source_sys *sys = (oop_source_sys *) source;
	assert(MAGIC == sys->magic && "corrupt oop_source structure");
	return sys;
}

static void sys_on_fd(oop_source *source,int fd,oop_event ev,
                      oop_call_fd *f,void *v) {
	oop_source_sys *sys = verify_source(source);
	assert(NULL != f && "callback must be non-NULL");
	if (fd >= sys->num_files) {
		int i,j,num_files = 1 + fd;
		sys_file *files = oop_malloc(num_files * sizeof(sys_file));
		if (NULL == files) return; /* ugh */

		memcpy(files,sys->files,sizeof(sys_file) * sys->num_files);
		for (i = sys->num_files; i < num_files; ++i)
			for (j = 0; j < OOP_NUM_EVENTS; ++j)
				files[i][j].f = NULL;

		if (NULL != sys->files) oop_free(sys->files);
		sys->files = files;
		sys->num_files = num_files;
	}

	assert(NULL == sys->files[fd][ev].f && "multiple handlers registered for a file event");
	sys->files[fd][ev].f = f;
	sys->files[fd][ev].v = v;
	++sys->num_events;
}

static void sys_cancel_fd(oop_source *source,int fd,oop_event ev) {
	oop_source_sys *sys = verify_source(source);
	if (fd < sys->num_files && NULL != sys->files[fd][ev].f) {
		sys->files[fd][ev].f = NULL;
		sys->files[fd][ev].v = NULL;
		--sys->num_events;
	}
}

static void sys_on_time(oop_source *source,struct timeval tv,
                        oop_call_time *f,void *v) {
	oop_source_sys *sys = verify_source(source);
	struct sys_time **p = &sys->time_queue;
	struct sys_time *time = oop_malloc(sizeof(struct sys_time));
	assert(tv.tv_usec >= 0 && "tv_usec must be positive");
	assert(tv.tv_usec < 1000000 && "tv_usec measures microseconds");
	assert(NULL != f && "callback must be non-NULL");
	if (NULL == time) return; /* ugh */
	time->tv = tv;
	time->f = f;
	time->v = v;

	while (NULL != *p
	&&    ((*p)->tv.tv_sec < tv.tv_sec
	||    ((*p)->tv.tv_sec == tv.tv_sec 
	&&     (*p)->tv.tv_usec <= tv.tv_usec))) p = &(*p)->next;
	time->next = *p;
	*p = time;

	++sys->num_events;
}

static int sys_remove_time(oop_source_sys *sys,
                           struct sys_time **p,struct timeval tv,
                           oop_call_time *f,void *v) {
	while (NULL != *p
	&&    ((*p)->tv.tv_sec < tv.tv_sec
	||    ((*p)->tv.tv_sec == tv.tv_sec 
	&&     (*p)->tv.tv_usec < tv.tv_usec))) p = &(*p)->next;
	while (NULL != *p
	&&     (*p)->tv.tv_sec == tv.tv_sec
	&&     (*p)->tv.tv_usec == tv.tv_usec
	&&    ((*p)->f != f || (*p)->v != v)) p = &(*p)->next;
	if (NULL != *p 
	&& (*p)->tv.tv_sec == tv.tv_sec && (*p)->tv.tv_usec == tv.tv_usec) {
		struct sys_time *time = *p;
		assert(f == time->f);
		assert(v == time->v);
		*p = time->next;
		oop_free(time);
		--sys->num_events;
		return 1;
	}
	return 0;
}

static void sys_cancel_time(oop_source *source,struct timeval tv,
                            oop_call_time *f,void *v) {
	oop_source_sys *sys = verify_source(source);
	if (!sys_remove_time(sys,&sys->time_run,tv,f,v))
		sys_remove_time(sys,&sys->time_queue,tv,f,v);
}

static void sys_signal_handler(int sig) {
	oop_source_sys *sys = sys_sig_owner[sig];
	struct sigaction act;
	assert(NULL != sys);

	/* Reset the handler, in case this is needed. */
	sigaction(sig,NULL,&act);
	act.sa_handler = sys_signal_handler;
	sigaction(sig,&act,NULL);

	assert(NULL != sys->sig[sig].list);
	sys->sig[sig].active = 1;
	sys->sig_active = 1;

	/* Break out of select() loop, if necessary. */
	if (sys->do_jmp) siglongjmp(sys->env,1);
}

static void sys_on_signal(oop_source *source,int sig,
                          oop_call_signal *f,void *v) {
	oop_source_sys *sys = verify_source(source);
	struct sys_signal_handler *handler = oop_malloc(sizeof(*handler));
	assert(NULL != f && "callback must be non-NULL");
	if (NULL == handler) return; /* ugh */

	assert(sig > 0 && sig < OOP_NUM_SIGNALS && "invalid signal number");

	handler->f = f;
	handler->v = v;
	handler->next = sys->sig[sig].list;
	sys->sig[sig].list = handler;
	++sys->num_events;

	if (NULL == handler->next) {
		struct sigaction act;

		assert(NULL == sys_sig_owner[sig]);
		sys_sig_owner[sig] = sys;

		assert(0 == sys->sig[sig].active);
		sigaction(sig,NULL,&act);
		sys->sig[sig].old = act;
		act.sa_handler = sys_signal_handler;
#ifdef SA_NODEFER /* BSD/OS doesn't have this, for one. */
		act.sa_flags &= ~SA_NODEFER;
#endif
		sigaction(sig,&act,NULL);
	}
}

static void sys_cancel_signal(oop_source *source,int sig,
                              oop_call_signal *f,void *v) {
	oop_source_sys *sys = verify_source(source);
	struct sys_signal_handler **pp = &sys->sig[sig].list;

	assert(sig > 0 && sig < OOP_NUM_SIGNALS && "invalid signal number");

	while (NULL != *pp && ((*pp)->f != f || (*pp)->v != v))
		pp = &(*pp)->next;

	if (NULL != *pp) {
		struct sys_signal_handler *p = *pp;

		if (NULL == p->next && &sys->sig[sig].list == pp) {
			sigaction(sig,&sys->sig[sig].old,NULL);
			sys->sig[sig].active = 0;
			sys_sig_owner[sig] = NULL;
		}

		*pp = p->next;
		if (sys->sig[sig].ptr == p) sys->sig[sig].ptr = *pp;
		--sys->num_events;
		oop_free(p);
	}
}

oop_source_sys *oop_sys_new(void) {
	oop_source_sys *source = oop_malloc(sizeof(oop_source_sys));
	int i;
	if (NULL == source) return NULL;
	source->oop.on_fd = sys_on_fd;
	source->oop.cancel_fd = sys_cancel_fd;
	source->oop.on_time = sys_on_time;
	source->oop.cancel_time = sys_cancel_time;
	source->oop.on_signal = sys_on_signal;
	source->oop.cancel_signal = sys_cancel_signal;
	source->magic = MAGIC;
	source->in_run = 0;
	source->num_events = 0;
	source->time_queue = source->time_run = NULL;

	source->do_jmp = 0;
	source->sig_active = 0;
	for (i = 0; i < OOP_NUM_SIGNALS; ++i) {
		source->sig[i].list = NULL;
		source->sig[i].ptr = NULL;
		source->sig[i].active = 0;
	}

	source->num_files = 0;
	source->files = NULL;

	return source;
}

static void *sys_time_run(oop_source_sys *sys) {
	void *ret = OOP_CONTINUE;
	while (OOP_CONTINUE == ret && NULL != sys->time_run) {
		struct sys_time *p = sys->time_run;
		sys->time_run = sys->time_run->next;
		--sys->num_events;
		ret = p->f(&sys->oop,p->tv,p->v); /* reenter! */
		oop_free(p);
	}
	return ret;
}

void *oop_sys_run(oop_source_sys *sys) {
	void *ret = OOP_CONTINUE;
	assert(!sys->in_run && "oop_sys_run is not reentrant");
	while (0 != sys->num_events && OOP_CONTINUE == ret)
		ret = oop_sys_run_once(sys);
	return ret;
}

void *oop_sys_run_once(oop_source_sys *sys) {
	void * volatile ret = OOP_CONTINUE;
	struct timeval * volatile ptv = NULL;
	struct timeval tv;
	fd_set rfd,wfd,xfd;
	int i,rv;

	assert(!sys->in_run && "oop_sys_run_once is not reentrant");
	sys->in_run = 1;

	if (NULL != sys->time_run) {
		/* interrupted, restart */
		ptv = &tv;
		tv.tv_sec = 0;
		tv.tv_usec = 0;
	} else if (NULL != sys->time_queue) {
		ptv = &tv;
		gettimeofday(ptv,NULL);
		if (sys->time_queue->tv.tv_usec < tv.tv_usec) {
			tv.tv_usec -= 1000000;
			tv.tv_sec ++;
		}
		tv.tv_sec = sys->time_queue->tv.tv_sec - tv.tv_sec;
		tv.tv_usec = sys->time_queue->tv.tv_usec - tv.tv_usec;
		if (tv.tv_sec < 0) {
			tv.tv_sec = 0;
			tv.tv_usec = 0;
		}
	}

	if (!sys->sig_active) sys->do_jmp = !sigsetjmp(sys->env,1);
	if (sys->sig_active) {
		/* Still perform select(), but don't block. */
		ptv = &tv;
		tv.tv_sec = 0;
		tv.tv_usec = 0;
	}

	/* select() fails on FreeBSD with EINVAL if tv_sec > 1000000000.
           The manual specifies the error code but not the limit.  We limit
	   the select() timeout to one hour for portability. */
	if (NULL != ptv && ptv->tv_sec >= 3600) ptv->tv_sec = 3599;
	assert(NULL == ptv 
	   || (ptv->tv_sec >= 0 && ptv->tv_sec < 3600
           &&  ptv->tv_usec >= 0 && ptv->tv_usec < 1000000));

	FD_ZERO(&rfd);
	FD_ZERO(&wfd);
	FD_ZERO(&xfd);
	for (i = 0; i < sys->num_files; ++i) {
		if (NULL != sys->files[i][OOP_READ].f) FD_SET(i,&rfd);
		if (NULL != sys->files[i][OOP_WRITE].f) FD_SET(i,&wfd);
		if (NULL != sys->files[i][OOP_EXCEPTION].f) FD_SET(i,&xfd);
	}

	do
		rv = select(sys->num_files,&rfd,&wfd,&xfd,ptv);
	while (0 > rv && EINTR == errno);

	sys->do_jmp = 0;

	if (0 > rv) { /* Error in select(). */
		ret = OOP_ERROR;
		goto done; 
	}

	if (sys->sig_active) {
		sys->sig_active = 0;
		for (i = 0; OOP_CONTINUE == ret && i < OOP_NUM_SIGNALS; ++i) {
			if (sys->sig[i].active) {
				sys->sig[i].active = 0;
				sys->sig[i].ptr = sys->sig[i].list;
			}
			while (OOP_CONTINUE == ret && NULL != sys->sig[i].ptr) {
				struct sys_signal_handler *h;
				h = sys->sig[i].ptr;
				sys->sig[i].ptr = h->next;
				ret = h->f(&sys->oop,i,h->v);
			}
		}
		if (OOP_CONTINUE != ret) {
			sys->sig_active = 1; /* come back */
			goto done;
		}
	}

	if (0 < rv) {
		for (i = 0; OOP_CONTINUE == ret && i < sys->num_files; ++i)
			if (FD_ISSET(i,&xfd) 
			&&  NULL != sys->files[i][OOP_EXCEPTION].f)
				ret = sys->files[i][OOP_EXCEPTION].f(
					&sys->oop,i,OOP_EXCEPTION,
					 sys->files[i][OOP_EXCEPTION].v);
		for (i = 0; OOP_CONTINUE == ret && i < sys->num_files; ++i)
			if (FD_ISSET(i,&wfd) 
			&&  NULL != sys->files[i][OOP_WRITE].f)
				ret = sys->files[i][OOP_WRITE].f(
					&sys->oop,i,OOP_WRITE,
					 sys->files[i][OOP_WRITE].v);
		for (i = 0; OOP_CONTINUE == ret && i < sys->num_files; ++i)
			if (FD_ISSET(i,&rfd) 
			&&  NULL != sys->files[i][OOP_READ].f)
				ret = sys->files[i][OOP_READ].f(
					&sys->oop,i,OOP_READ,
					 sys->files[i][OOP_READ].v);
		if (OOP_CONTINUE != ret) goto done;
	}

	/* Catch any leftover timeout events. */
	ret = sys_time_run(sys);
	if (OOP_CONTINUE != ret) goto done;

	if (NULL != sys->time_queue) {
		struct sys_time *p,**pp = &sys->time_queue;
		gettimeofday(&tv,NULL);
		while (NULL != *pp 
		   && (tv.tv_sec > (*pp)->tv.tv_sec
		   || (tv.tv_sec == (*pp)->tv.tv_sec
		   &&  tv.tv_usec >= (*pp)->tv.tv_usec)))
			pp = &(*pp)->next;
		p = *pp;
		*pp = NULL;
		sys->time_run = sys->time_queue;
		sys->time_queue = p;
	}

	ret = sys_time_run(sys);

done:
	sys->in_run = 0;
	return ret;
}

void oop_sys_delete(oop_source_sys *sys) {
	int i,j;

	assert(!sys->in_run && "cannot delete while in oop_sys_run");
	assert(NULL == sys->time_queue 
	&&     NULL == sys->time_run
	&&     "cannot delete with timeout");

	for (i = 0; i < OOP_NUM_SIGNALS; ++i)
		assert(NULL == sys->sig[i].list && "cannot delete with signal handler");

	for (i = 0; i < sys->num_files; ++i)
		for (j = 0; j < OOP_NUM_EVENTS; ++j)
			assert(NULL == sys->files[i][j].f && "cannot delete with file handler");

	assert(0 == sys->num_events);
	if (NULL != sys->files) oop_free(sys->files);
	oop_free(sys);
}

oop_source *oop_sys_source(oop_source_sys *sys) {
	assert(&sys->oop == (oop_source *) sys);
	return &sys->oop;
}
