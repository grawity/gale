/* signal.c, liboop, copyright 1999 Dan Egnor

   This is free software; you can redistribute it and/or modify it under the
   terms of the GNU Lesser General Public License, version 2.1 or later.
   See the file COPYING for details. */

#include "oop.h"

#include <assert.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define MAGIC 5131

struct sig_handler {
	struct sig_handler *next;
	oop_call_signal *f;
	void *v;
};

struct sig_signal {
	struct sig_handler *list,*ptr;
	struct sigaction old;
	volatile sig_atomic_t active;
};

struct oop_adapter_signal {
	oop_source oop;
	int magic,pipefd[2];
	oop_source *source; /* backing source */
	struct sig_signal sig[OOP_NUM_SIGNALS];
	int num_events;
};

static struct oop_adapter_signal *sig_owner[OOP_NUM_SIGNALS];

static oop_adapter_signal *verify_source(oop_source *source) {
	oop_adapter_signal * const s = (oop_adapter_signal *) source;
	assert(MAGIC == s->magic);
	return s;
}

static void do_pipe(struct oop_adapter_signal *s) {
	const char ch = '\0';
	while (write(s->pipefd[1],&ch,1) < 0 && EINTR == errno) ;
}

static void on_signal(int sig) {
	oop_adapter_signal * const s = sig_owner[sig];
	struct sigaction act;
	assert(NULL != s);

	/* Reset the handler, in case this is needed. */
	sigaction(sig,NULL,&act);
	act.sa_handler = on_signal;
	sigaction(sig,&act,NULL);

	assert(NULL != s->sig[sig].list);
	s->sig[sig].active = 1;
	do_pipe(s);
}

static void *on_pipe(oop_source *source,int fd,oop_event event,void *user) {
	oop_adapter_signal * const s = verify_source((oop_source *) user);
	int i;
	char buf[4096];

	assert(fd == s->pipefd[0]);
	assert(OOP_READ == event);
	while (read(s->pipefd[0],buf,sizeof buf) < 0 && EINTR == errno) ;

	for (i = 0; i < OOP_NUM_SIGNALS; ++i) {
		if (s->sig[i].active) {
			s->sig[i].active = 0;
			s->sig[i].ptr = s->sig[i].list;
		}
		if (NULL != s->sig[i].ptr) {
			struct sig_handler * const h = s->sig[i].ptr;
			s->sig[i].ptr = h->next;
			do_pipe(s); /* come back */
			return h->f(&s->oop,i,h->v);
		}
	}

	return OOP_CONTINUE;
}

static void sig_on_fd(oop_source *source,int fd,oop_event ev,
                      oop_call_fd *call,void *data) {
	oop_adapter_signal * const s = verify_source(source);
	s->source->on_fd(s->source,fd,ev,call,data);
}

static void sig_cancel_fd(oop_source *source,int fd,oop_event ev) {
	oop_adapter_signal * const s = verify_source(source);
	s->source->cancel_fd(s->source,fd,ev);
}

static void sig_on_time(oop_source *source,struct timeval when,
                        oop_call_time *call,void *data) {
	oop_adapter_signal * const s = verify_source(source);
	s->source->on_time(s->source,when,call,data);
}

static void sig_cancel_time(oop_source *source,struct timeval when,
                            oop_call_time *call,void *data) {
	oop_adapter_signal * const s = verify_source(source);
	s->source->cancel_time(s->source,when,call,data);
}

static void sig_on_signal(oop_source *source,int sig,
                          oop_call_signal *f,void *v) {
	oop_adapter_signal * const s = verify_source(source);
	struct sig_handler * const handler = oop_malloc(sizeof(*handler));
	if (NULL == handler) return; /* ugh */

	assert(sig > 0 && sig < OOP_NUM_SIGNALS && "invalid signal number");

	handler->f = f;
	handler->v = v;
	handler->next = s->sig[sig].list;
	s->sig[sig].list = handler;
	++s->num_events;

	if (NULL == handler->next) {
		struct sigaction act;

		assert(NULL == sig_owner[sig]);
		sig_owner[sig] = s;

		assert(0 == s->sig[sig].active);
		sigaction(sig,NULL,&act);
		s->sig[sig].old = act;
		act.sa_handler = on_signal;
#ifdef SA_NODEFER
		act.sa_flags &= ~SA_NODEFER;
#endif
		sigaction(sig,&act,NULL);
	}
}

static void sig_cancel_signal(oop_source *source,int sig,
                              oop_call_signal *f,void *v) {
	oop_adapter_signal * const s = verify_source(source);
	struct sig_handler **pp = &s->sig[sig].list;

	assert(sig > 0 && sig < OOP_NUM_SIGNALS && "invalid signal number");

	while (NULL != *pp && ((*pp)->f != f || (*pp)->v != v))
		pp = &(*pp)->next;

	if (NULL != *pp) {
		struct sig_handler * const p = *pp;

		if (NULL == p->next && &s->sig[sig].list == pp) {
			sigaction(sig,&s->sig[sig].old,NULL);
			s->sig[sig].active = 0;
			sig_owner[sig] = NULL;
		}

		*pp = p->next;
		if (s->sig[sig].ptr == p) s->sig[sig].ptr = *pp;
		--s->num_events;
		oop_free(p);
	}
}

static int fcntl_flag(int fd, int get_op, int set_op, int val) {
	const int flags = fcntl(fd,get_op,0);
	if (flags < 0) return -1;
	return fcntl(fd,set_op,flags|val);
}

oop_adapter_signal *oop_signal_new(oop_source *source) {
	int i;
	oop_adapter_signal * const s = oop_malloc(sizeof(*s));
	if (NULL == s) return NULL;
	assert(NULL != source);
	if (pipe(s->pipefd)
	||  fcntl_flag(s->pipefd[0],F_GETFD,F_SETFD,FD_CLOEXEC)
	||  fcntl_flag(s->pipefd[1],F_GETFD,F_SETFD,FD_CLOEXEC)
	||  fcntl_flag(s->pipefd[0],F_GETFL,F_SETFL,O_NONBLOCK)
	||  fcntl_flag(s->pipefd[1],F_GETFL,F_SETFL,O_NONBLOCK)) {
		oop_free(s);
		return NULL;
	}

	s->oop.on_fd = sig_on_fd;
	s->oop.cancel_fd = sig_cancel_fd;
	s->oop.on_time = sig_on_time;
	s->oop.cancel_time = sig_cancel_time;
	s->oop.on_signal = sig_on_signal;
	s->oop.cancel_signal = sig_cancel_signal;

	s->source = source;
	s->source->on_fd(s->source,s->pipefd[0],OOP_READ,on_pipe,s);
	s->num_events = 0;

	for (i = 0; i < OOP_NUM_SIGNALS; ++i) {
		s->sig[i].list = NULL;
		s->sig[i].ptr = NULL;
		s->sig[i].active = 0;
	}

	s->magic = MAGIC;
	return s;
}

void oop_signal_delete(oop_adapter_signal *s) {
	assert(0 == s->num_events && "cannot delete with signal handler");
	s->magic = 0;
	close(s->pipefd[0]);
	close(s->pipefd[1]);
	s->source->cancel_fd(s->source,s->pipefd[0],OOP_READ);
	oop_free(s);
}

oop_source *oop_signal_source(oop_adapter_signal *s) {
	return &s->oop;
}
