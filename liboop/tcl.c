/* glib.c, liboop, copyright 1999 Dan Egnor

   This is free software; you can redistribute it and/or modify it under the
   terms of the GNU Lesser General Public License, version 2.1 or later.
   See the file COPYING for details. */

#ifdef HAVE_TCL

#include "oop-tcl.h"
#include <tcl.h>
#include <assert.h>

struct file_handler {
        oop_call_fd *f[OOP_NUM_EVENTS];
        void *d[OOP_NUM_EVENTS];
};

struct timer_handler {
        struct timeval t;
        oop_call_time *f;
        void *d;
        Tcl_TimerToken token;
        struct timer_handler *next;
};

static int use_count = 0;
static struct oop_source source;
static struct oop_adapter_signal *signal;

static int array_size;
static struct file_handler *array;
static struct timer_handler *list;

static void file_call(ClientData data,int mask) {
        const int fd = (int) data;
        oop_source * const oop = oop_tcl_new();
        const struct file_handler *h;

        if (fd >= array_size) {
                oop_tcl_done();
                return;
        }

        /* BUG: what if !OOP_CONTINUE? */

        h = &array[fd];
        if ((mask & TCL_READABLE) && NULL != h->f[OOP_READ])
                h->f[OOP_READ](oop,fd,OOP_READ,h->d[OOP_READ]);

        h = &array[fd];
        if ((mask & TCL_WRITABLE) && NULL != h->f[OOP_WRITE])
                h->f[OOP_WRITE](oop,fd,OOP_WRITE,h->d[OOP_WRITE]);

        h = &array[fd];
        if ((mask & TCL_EXCEPTION) && NULL != h->f[OOP_EXCEPTION])
                h->f[OOP_EXCEPTION](oop,fd,OOP_EXCEPTION,h->d[OOP_EXCEPTION]);

        oop_tcl_done();
}

static void set_mask(int fd) {
        int mask = 0;
        const struct file_handler * const h = &array[fd];
        if (NULL != h->f[OOP_READ]) mask |= TCL_READABLE;
        if (NULL != h->f[OOP_WRITE]) mask |= TCL_WRITABLE;
        if (NULL != h->f[OOP_EXCEPTION]) mask |= TCL_EXCEPTION;

        if (0 == mask)
                Tcl_DeleteFileHandler(fd);
        else
                Tcl_CreateFileHandler(fd,mask,file_call,(ClientData) fd);
}

static void on_fd(oop_source *x,int fd,oop_event event,oop_call_fd *f,void *d) {
        struct file_handler *h;

        if (fd >= array_size) {
                const int new_size = fd + 1;
                struct file_handler * const new_array = 
                        oop_realloc(array,new_size * sizeof(*new_array));
                if (NULL == new_array) return; /* YUCK */

                array = new_array;
                while (array_size != new_size) {
                        int i;
                        for (i = 0; i < OOP_NUM_EVENTS; ++i)
                                array[array_size].f[i] = NULL;
                        ++array_size;
                }
        }

        h = &array[fd];
        assert(NULL == h->f[event] && NULL != f);
        h->f[event] = f;
        h->d[event] = d;
        set_mask(fd);
}

static void cancel_fd(oop_source *x,int fd,oop_event event) {
        if (fd < array_size) {
                struct file_handler * const h = &array[fd];
                h->f[event] = NULL;
                set_mask(fd);
        }
}

static void timer_call(ClientData data) {
        struct timer_handler * const timer = (struct timer_handler *) data;
        struct timer_handler **ptr;

        Tcl_DeleteTimerHandler(timer->token);
        for (ptr = &list; timer != *ptr; ptr = &(*ptr)->next) ;
        *ptr = timer->next;

        /* BUG: What if !OOP_CONTINUE? */
        timer->f(oop_signal_source(signal),timer->t,timer->d);
        oop_free(timer);
}

static void on_time(oop_source *x,struct timeval t,oop_call_time *f,void *d) {
        struct timer_handler * const timer = oop_malloc(sizeof(*timer));
        struct timeval now;
        int msec;
        if (NULL == timer) return; /* YUCK */

        gettimeofday(&now,NULL);
        if (t.tv_sec < now.tv_sec
        || (t.tv_sec == now.tv_sec && t.tv_usec < now.tv_usec))
                msec = 0;
        else {
                msec = 1000 * (t.tv_sec - now.tv_sec);
                msec = msec + (t.tv_usec - now.tv_usec) / 1000;
        }

        assert(msec >= 0);
        timer->t = t;
        timer->f = f;
        timer->d = d;
        timer->next = list;
        timer->token = Tcl_CreateTimerHandler(msec,timer_call,timer);
        list = timer;
}

static void cancel_time(oop_source *x,struct timeval t,oop_call_time *f,void *d) {
        struct timer_handler **timer;
        for (timer = &list; NULL != *timer; timer = &(*timer)->next)
                if ((*timer)->d == d && (*timer)->f == f
                &&  (*timer)->t.tv_sec == t.tv_sec
                &&  (*timer)->t.tv_usec == t.tv_usec) {
                        struct timer_handler *dead = *timer;
                        *timer = dead->next;
                        Tcl_DeleteTimerHandler(dead->token);
                        oop_free(dead);
                        return;
                }
}

oop_source *oop_tcl_new(void) {
        if (0 == use_count) {
                source.on_fd = on_fd;
                source.cancel_fd = cancel_fd;
                source.on_time = on_time;
                source.cancel_time = cancel_time;
                source.on_signal = NULL;
                source.cancel_signal = NULL;
                array = NULL;
                array_size = 0;
                list = NULL;

                /* Do this last, after everything is set up. */
                signal = oop_signal_new(&source);
                if (NULL == signal) return NULL;
        }

        ++use_count;
        return oop_signal_source(signal);
}

void oop_tcl_done(void) {
        if (0 == --use_count) {
                int i,j;
                for (i = 0; i < array_size; ++i)
                        for (j = 0; j < OOP_NUM_EVENTS; ++j)
                                assert(NULL == array[i].f[j]);
                oop_free(array);
                assert(NULL == list);
                oop_signal_delete(signal);
        }
}

#endif
