/* select.c, liboop, copyright 1999 Dan Egnor

   This is free software; you can redistribute it and/or modify it under the
   terms of the GNU Lesser General Public License, version 2.1 or later.
   See the file COPYING for details. */

#include "oop.h"

#include <assert.h>

struct select_set {
	fd_set rfd,wfd,xfd;
};

struct oop_adapter_select {
	oop_source *source;
	struct select_set watch,active;
	struct timeval timeout;
	int num_fd,do_timeout,is_active,num_fd_active;
	oop_call_select *call;
	void *data;
};

static oop_call_fd on_fd;
static oop_call_time on_timeout,on_collect;

oop_adapter_select *oop_select_new(
	oop_source *source, 
	oop_call_select *call,void *data)
{
	oop_adapter_select *s = oop_malloc(sizeof(*s));
	if (NULL == s) return s;
	s->source = source;
	FD_ZERO(&s->watch.rfd);
	FD_ZERO(&s->watch.wfd);
	FD_ZERO(&s->watch.xfd);
	FD_ZERO(&s->active.rfd);
	FD_ZERO(&s->active.wfd);
	FD_ZERO(&s->active.xfd);
	s->num_fd = 0;
	s->num_fd_active = 0;
	s->do_timeout = 0;
	s->is_active = 0;
	s->call = call;
	s->data = data;
	return s;
}

static void *activate(oop_adapter_select *s) {
	if (!s->is_active) {
		s->is_active = 1;
		s->source->on_time(s->source,OOP_TIME_NOW,on_collect,s);
	}
	return OOP_CONTINUE;
}

static void deactivate(oop_adapter_select *s) {
	if (s->is_active) {
		s->source->cancel_time(s->source,OOP_TIME_NOW,on_collect,s);
		s->is_active = 0;
		s->num_fd_active = 0;
		FD_ZERO(&s->active.rfd);
		FD_ZERO(&s->active.wfd);
		FD_ZERO(&s->active.xfd);
	}
}

void oop_select_set(
	oop_adapter_select *s,int num_fd,
	fd_set *rfd,fd_set *wfd,fd_set *xfd,struct timeval *timeout)
{
	int fd;
	for (fd = 0; fd < num_fd || fd < s->num_fd; ++fd) {
		int rfd_set = fd < num_fd && FD_ISSET(fd,rfd);
		int wfd_set = fd < num_fd && FD_ISSET(fd,wfd);
		int xfd_set = fd < num_fd && FD_ISSET(fd,xfd);
		int w_rfd_set = fd < s->num_fd && FD_ISSET(fd,&s->watch.rfd);
		int w_wfd_set = fd < s->num_fd && FD_ISSET(fd,&s->watch.wfd);
		int w_xfd_set = fd < s->num_fd && FD_ISSET(fd,&s->watch.xfd);

		if (rfd_set && !w_rfd_set) {
			s->source->on_fd(s->source,fd,OOP_READ,on_fd,s);
			FD_SET(fd,&s->watch.rfd);
		}

		if (!rfd_set && w_rfd_set) {
			s->source->cancel_fd(s->source,fd,OOP_READ);
			FD_CLR(fd,&s->watch.rfd);
		}

		if (wfd_set && !w_wfd_set) {
			s->source->on_fd(s->source,fd,OOP_WRITE,on_fd,s);
			FD_SET(fd,&s->watch.wfd);
		}

		if (!wfd_set && w_wfd_set) {
			s->source->cancel_fd(s->source,fd,OOP_WRITE);
			FD_CLR(fd,&s->watch.wfd);
		}

		if (xfd_set && !w_xfd_set) {
			s->source->on_fd(s->source,fd,OOP_EXCEPTION,on_fd,s);
			FD_SET(fd,&s->watch.xfd);
		}

		if (!xfd_set && w_xfd_set) {
			s->source->cancel_fd(s->source,fd,OOP_EXCEPTION);
			FD_CLR(fd,&s->watch.xfd);
		}
	}

	s->num_fd = num_fd;

	if (s->do_timeout) {
		s->source->cancel_time(s->source,s->timeout,on_timeout,s);
		s->do_timeout = 0;
	}

	if (NULL != timeout) {
		gettimeofday(&s->timeout,NULL);
		s->timeout.tv_sec += timeout->tv_sec;
		s->timeout.tv_usec += timeout->tv_usec;
		while (s->timeout.tv_usec >= 1000000) {
			++s->timeout.tv_sec;
			s->timeout.tv_usec -= 1000000;
		}
		s->do_timeout = 1;
		s->source->on_time(s->source,s->timeout,on_timeout,s);
	}

	deactivate(s);
}

void oop_select_delete(oop_adapter_select *s) {
	fd_set fd;
	FD_ZERO(&fd);
	oop_select_set(s,0,&fd,&fd,&fd,NULL);
	oop_free(s);
}

static void set_fd(int fd,fd_set *fds,int *num) {
	if (!FD_ISSET(fd,fds)) {
		FD_SET(fd,fds);
		if (fd >= *num) *num = fd + 1;
	}
}

static void *on_fd(oop_source *source,int fd,oop_event event,void *data) {
	oop_adapter_select *s = (oop_adapter_select *) data;
	switch (event) {
	case OOP_READ:
		assert(FD_ISSET(fd,&s->watch.rfd));
		set_fd(fd,&s->active.rfd,&s->num_fd_active);
		break;
	case OOP_WRITE:
		assert(FD_ISSET(fd,&s->watch.wfd));
		set_fd(fd,&s->active.wfd,&s->num_fd_active);
		break;
	case OOP_EXCEPTION:
		assert(FD_ISSET(fd,&s->watch.xfd));
		set_fd(fd,&s->active.xfd,&s->num_fd_active);
		break;
	default:
		assert(0);
		break;
	}
	return activate(s);
}

static void *on_timeout(oop_source *source,struct timeval when,void *data) {
	oop_adapter_select *s = (oop_adapter_select *) data;
	assert(s->do_timeout);
	return activate(s);
}

static void *on_collect(oop_source *source,struct timeval when,void *data) {
	oop_adapter_select *s = (oop_adapter_select *) data;
	struct select_set set = s->active;
	int num = s->num_fd_active;
	struct timeval now;
	gettimeofday(&now,NULL);
	deactivate(s);
	return s->call(s,num,&set.rfd,&set.wfd,&set.xfd,now,s->data);
}
