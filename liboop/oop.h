/* oop.h, liboop, copyright 1999 Dan Egnor

   This is free software; you can redistribute it and/or modify it under the
   terms of the GNU Lesser General Public License, version 2.1 or later.
   See the file COPYING for details. */

#ifndef OOP_H
#define OOP_H

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

/* ------------------------------------------------------------------------- */

typedef struct oop_source oop_source;

/* File descriptor action types */
typedef enum {
	OOP_READ,
	OOP_WRITE,
	OOP_EXCEPTION,

	OOP_NUM_EVENTS
} oop_event;

/* Pass this to on_time to schedule an event immediately */
static const struct timeval OOP_TIME_NOW = { 0, 0 };

/* Maximum signal number.  (The OS may have a stricter limit!) */
#define OOP_NUM_SIGNALS 256

/* Callbacks may return one of these */
extern int _oop_continue,_oop_error; /* internal only */
#define OOP_CONTINUE ((void *) &_oop_continue)
#define OOP_ERROR ((void *) &_oop_error)
#define OOP_HALT ((void *) NULL) /* (or any other value besides OOP_CONTINUE) */

/* Callback function prototypes */
typedef void *oop_call_fd(oop_source *,int fd,oop_event,void *);
typedef void *oop_call_time(oop_source *,struct timeval,void *);
typedef void *oop_call_signal(oop_source *,int sig,void *);

struct oop_source {
	void (*on_fd)(oop_source *,int fd,oop_event,oop_call_fd *,void *);
	void (*cancel_fd)(oop_source *,int fd,oop_event);

	void (*on_time)(oop_source *,struct timeval,oop_call_time *,void *);
	void (*cancel_time)(oop_source *,struct timeval,oop_call_time *,void *);

	void (*on_signal)(oop_source *,int sig,oop_call_signal *,void *);
	void (*cancel_signal)(oop_source *,int sig,oop_call_signal *,void *);
};

/* ------------------------------------------------------------------------- */

/* For recommended use by oop components. */

extern void *(*oop_malloc)(size_t);
extern void *(*oop_realloc)(void *,size_t);
extern void (*oop_free)(void *);

/* ------------------------------------------------------------------------- */

/* System event source. */
typedef struct oop_source_sys oop_source_sys;

/* Create a system event source.  Returns NULL on failure. */
oop_source_sys *oop_sys_new(void);   

/* Process events until either of the following two conditions:
   1 -- some callback returns anything but OOP_CONTINUE; 
        will return the value in question.
   2 -- no callbacks are registered; will return OOP_CONTINUE. 
   3 -- an error occurs; will return OOP_ERROR (check errno). */
void *oop_sys_run(oop_source_sys *); 

/* Process all pending events and return immediately.
   Return values are the same as for oop_sys_run(). */
void *oop_sys_run_once(oop_source_sys *);

/* Delete a system event source.  No callbacks may be registered. */
void oop_sys_delete(oop_source_sys *);

/* Get the event registration interface for a system event source. */
oop_source *oop_sys_source(oop_source_sys *);

/* ------------------------------------------------------------------------- */

/* Helper for select-style asynchronous interfaces. */
typedef struct oop_adapter_select oop_adapter_select;
typedef void *oop_call_select(
	oop_adapter_select *,
	int num,fd_set *r,fd_set *w,fd_set *x,
	struct timeval now,void *);

oop_adapter_select *oop_select_new(
	oop_source *,
	oop_call_select *,
	void *);

void oop_select_set(
	oop_adapter_select *,int num_fd,
	fd_set *rfd,fd_set *wfd,fd_set *xfd,struct timeval *timeout);

void oop_select_delete(oop_adapter_select *);

/* ------------------------------------------------------------------------- */

/* Helper for event sources without signal handling. */
typedef struct oop_adapter_signal oop_adapter_signal;

oop_adapter_signal *oop_signal_new(oop_source *);
void oop_signal_delete(oop_adapter_signal *);
oop_source *oop_signal_source(oop_adapter_signal *);

#endif
