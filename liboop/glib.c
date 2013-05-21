/* glib.c, liboop, copyright 1999 Dan Egnor

   This is free software; you can redistribute it and/or modify it under the
   terms of the GNU Lesser General Public License, version 2.1 or later.
   See the file COPYING for details. */

#ifdef HAVE_GLIB

#include "glib.h"
#include "oop-glib.h"
#include "oop.h"

#include <assert.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#ifdef HAVE_POLL_H
#include <poll.h>
#endif

static int use_count = 0;
static oop_source_sys *sys;
static oop_adapter_select *sel;

static fd_set read_set,write_set,except_set;
static int count;
static void *ret = NULL;

#if GLIB_MAJOR_VERSION >= 2
static GPollFunc save_poll_func = NULL;
#endif

static void *on_select(
	oop_adapter_select *s,int num,fd_set *r,fd_set *w,fd_set *x,
	struct timeval now,void *unused) 
{
	read_set = *r;
	write_set = *w;
	except_set = *x;
	count = num;
	return &use_count;
}

static gint on_poll(GPollFD *array,guint num,gint timeout) {
	struct timeval tv;
	int i;

	FD_ZERO(&read_set);
	FD_ZERO(&write_set);
	FD_ZERO(&except_set);
	count = 0;
	for (i = 0; i < num; ++i) {
		if (array[i].events & G_IO_IN)
			FD_SET(array[i].fd,&read_set);
		if (array[i].events & G_IO_OUT)
			FD_SET(array[i].fd,&write_set);
		if (array[i].events & G_IO_PRI)
			FD_SET(array[i].fd,&except_set);
		/* {G_IO_,POLL}{ERR,HUP,INVAL} don't correspond to anything
		   in select(2), and aren't `normal' events anyway. */
		if (array[i].fd >= count)
			count = 1 + array[i].fd;
	}

	tv.tv_sec = timeout / 1000;
	tv.tv_usec = timeout % 1000;

	oop_select_set(sel,count,&read_set,&write_set,&except_set,
		       timeout < 0 ? NULL : &tv);
	ret = oop_sys_run(sys);

	if (&use_count != ret) {
		/* I really wish I could: g_main_quit(glib_loop); */
		return -1;
		/* but they even ignore the error return... sigh. */
	}

	for (i = 0; i < num; ++i) {
		if (FD_ISSET(array[i].fd,&read_set))
			array[i].revents |= G_IO_IN;
		if (FD_ISSET(array[i].fd,&write_set))
			array[i].revents |= G_IO_OUT;
		if (FD_ISSET(array[i].fd,&except_set))
			array[i].revents |= G_IO_PRI;
	}

	return count;
}

oop_source *oop_glib_new(void) {
	if (use_count++) return oop_sys_source(sys);

	sys = oop_sys_new();
	sel = oop_select_new(oop_sys_source(sys),on_select,NULL);

#if GLIB_MAJOR_VERSION >= 2
	save_poll_func = g_main_context_get_poll_func(g_main_context_default());
        g_main_context_set_poll_func(g_main_context_default(), on_poll);
#else
        g_main_set_poll_func(on_poll);
#endif

	return oop_sys_source(sys);
}

void *oop_glib_return(void) {
	if (&use_count == ret) return NULL;
	return ret;
}

#if defined(HAVE_POLL_H) && GLIB_MAJOR_VERSION < 2
static gint real_poll(GPollFD *array,guint num,gint timeout) {
	assert(sizeof(GPollFD) == sizeof(struct pollfd));
	return poll((struct pollfd *) array,num,timeout);
}
#endif

void oop_glib_delete(void) {
	assert(use_count > 0 && "oop_glib_delete() called too much");
	if (0 != --use_count) return;

#if GLIB_MAJOR_VERSION >= 2
	g_main_context_set_poll_func(g_main_context_default(), save_poll_func);
#elif defined(HAVE_POLL_H)
	g_main_set_poll_func(real_poll);
#else
	return;
#endif

	oop_select_delete(sel);
	oop_sys_delete(sys);
}

#endif  /* HAVE_GLIB */
