/* glib.c, liboop, copyright 1999 Dan Egnor

   This is free software; you can redistribute it and/or modify it under the
   terms of the GNU Lesser General Public License, version 2.1 or later.
   See the file COPYING for details. */

#include "glib.h"
#include "oop-glib.h"
#include "oop.h"

#include <assert.h>
#include <poll.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

static int use_count = 0;
static oop_source_sys *sys;
static oop_adapter_select *sel;

static fd_set read_set,write_set;
static int count;
static void *ret = NULL;

static void *on_select(
	oop_adapter_select *s,int num,fd_set *r,fd_set *w,
	struct timeval now,void *x) 
{
	read_set = *r;
	write_set = *w;
	count = num;
	return &use_count;
}

static gint on_poll(GPollFD *array,guint num,gint timeout) {
	struct timeval tv;
	int i;

	FD_ZERO(&read_set);
	FD_ZERO(&write_set);
	count = 0;
	for (i = 0; i < num; ++i) {
		if (array[i].events & G_IO_IN)
			FD_SET(array[i].fd,&read_set);
		if (array[i].events & G_IO_OUT)
			FD_SET(array[i].fd,&write_set);
		if (array[i].fd >= count)
			count = 1 + array[i].fd;
	}

	tv.tv_sec = timeout / 1000;
	tv.tv_usec = timeout % 1000;

	oop_select_set(sel,count,&read_set,&write_set,timeout < 0 ? NULL : &tv);
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
	}

	return count;
}

static gint real_poll(GPollFD *array,guint num,gint timeout) {
	assert(sizeof(GPollFD) == sizeof(struct pollfd));
	return poll((struct pollfd *) array,num,timeout);
}

oop_source *oop_glib_new() {
	if (use_count++) return oop_sys_source(sys);

	sys = oop_sys_new();
	sel = oop_select_new(oop_sys_source(sys),on_select,NULL);
	g_main_set_poll_func(on_poll);
	return oop_sys_source(sys);
}

void *oop_glib_return() {
	if (&use_count == ret) return NULL;
	return ret;
}

void oop_glib_delete() {
	assert(use_count > 0 && "oop_glib_delete() called too much");
	if (0 != --use_count) return;

	oop_select_delete(sel);
	oop_sys_delete(sys);
	g_main_set_poll_func(real_poll);
}
