/* www.c, liboop, copyright 1999 Dan Egnor

   This is free software; you can redistribute it and/or modify it under the
   terms of the GNU Lesser General Public License, version 2.1 or later.
   See the file COPYING for details. */

/* Yecch: the libwww header files need this. */
#define HAVE_CONFIG_H
#undef PACKAGE
#undef VERSION

#include <stdarg.h> /* Needed for our cut-down libwww headers. */

#include "oop.h"
#include "HTEvent.h"
#include "HTMemory.h"
#include "oop-www.h"

#include <assert.h>

struct event {
	HTEvent *event;
	struct timeval time;
};

typedef struct event descriptor[HTEvent_TYPES];

static int num = 0,size = 0;
static descriptor *array = NULL;
static oop_source *oop = NULL;

static struct event *get_event(SOCKET sock,HTEventType type) {
	assert(sock < size && "invalid file descriptor");
	return &array[sock][HTEvent_INDEX(type)];
}

static void *on_time(oop_source *s,struct timeval tv,void *x) {
	struct event *event;
	SOCKET sock = (int) x;
	int j;

	for (j = 0; j < HTEvent_TYPES; ++j) {
		event = &array[sock][j];
		if (NULL != event->event && 0 <= event->event->millis
		&&  tv.tv_sec == event->time.tv_sec
		&&  tv.tv_usec == event->time.tv_usec) 
			break;
	}

	assert(j < HTEvent_TYPES);
	event->event->cbf(sock,event->event->param,HTEvent_TIMEOUT);
	return OOP_CONTINUE;
}

static void set_timer(struct event *event) {
	if (0 <= event->event->millis) {
		gettimeofday(&event->time,NULL);
		event->time.tv_sec += event->event->millis / 1000;
		event->time.tv_usec += event->event->millis % 1000;
		if (event->time.tv_usec > 1000000) {
			event->time.tv_usec -= 1000000;
			++event->time.tv_sec;
		}
		oop->on_time(oop,event->time,on_time,event);
	}
}

static void *on_fd(oop_source *s,int fd,oop_event type,void *x) {
	HTEventType www_type;
	struct event *event;

	switch (type) {
	case OOP_READ:
		www_type = HTEvent_READ;
		break;
	case OOP_WRITE:
		www_type = HTEvent_WRITE;
		break;
	default:
		assert(0);
	}

	event = get_event(fd,www_type);
	if (0 <= event->event->millis) {
		oop->cancel_time(oop,event->time,on_time,event);
		set_timer(event);
	}
	event->event->cbf(fd,event->event->param,www_type);
	return OOP_CONTINUE;
}

static void dereg(SOCKET sock,HTEventType www_type,oop_event oop_type) {
	struct event *event = get_event(sock,www_type);
	assert(sock < size && "invalid file descriptor");
	if (NULL != event->event) {
		--num;
		oop->cancel_fd(oop,sock,oop_type);
		if (0 <= event->event->millis)
			oop->cancel_time(oop,event->time,on_time,event);
		event->event = NULL;
	}
}

static int reg(SOCKET sock,HTEventType type,HTEvent *www_event) {
	oop_event oop_type;
	struct event *event;

	switch (HTEvent_INDEX(type)) {
	case HTEvent_INDEX(HTEvent_READ):
		oop_type = OOP_READ;
		break;
	case HTEvent_INDEX(HTEvent_WRITE):
		oop_type = OOP_WRITE;
		break;
	case HTEvent_INDEX(HTEvent_OOB):
		// XXX: we don't handle this; does anything use it?
		return HT_ERROR;
	default:
		assert(0 && "invalid HTEvent type specified");
	}

	if (sock >= size) {
		int newsize = size ? (2*size) : 16;
		descriptor *newarray = oop_malloc(sizeof(*newarray) * newsize);
		int i,j;

		if (NULL == newarray) return HT_ERROR;

		memcpy(newarray,array,sizeof(*newarray) * size);
		for (i = size; i < newsize; ++i)
		for (j = 0; j < HTEvent_TYPES; ++j) {
			newarray[i][j].event = NULL;
		}

		array = newarray;
		size = newsize;
	}

	dereg(sock,type,oop_type);
	event = get_event(sock,type);
	event->event = www_event;
	oop->on_fd(oop,sock,oop_type,on_fd,NULL);
	set_timer(event);
	++num;

	return HT_OK;
}

static int unreg(SOCKET sock,HTEventType type) {
	oop_event oop_type;
	switch (HTEvent_INDEX(type)) {
	case HTEvent_INDEX(HTEvent_READ):
		oop_type = OOP_READ;
		break;
	case HTEvent_INDEX(HTEvent_WRITE):
		oop_type = OOP_WRITE;
		break;
	case HTEvent_INDEX(HTEvent_OOB):
		// XXX: we don't handle this; does anything use it?
		return HT_ERROR;
	default:
		assert(0 && "invalid HTEvent type specified");
	}

	dereg(sock,type,oop_type);
	return HT_OK;
}

void oop_www_register(oop_source *source) {
	oop = source;
	HTEvent_setRegisterCallback(reg);
	HTEvent_setUnregisterCallback(unreg);
}

void oop_www_cancel() {
	assert(0 == num && "cannot unregister with pending libwww events");
	HTEvent_setRegisterCallback(NULL);
	HTEvent_setUnregisterCallback(NULL);
	oop = NULL;
}

void oop_www_memory() {
	oop_malloc = HTMemory_malloc;
	oop_free = HTMemory_free;
}
