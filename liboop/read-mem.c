/* read-mem.c, liboop, copyright 2000 Ian jackson
   
   This is free software; you can redistribute it and/or modify it under the
   terms of the GNU Lesser General Public License, version 2.1 or later.
   See the file COPYING for details. */

#include "oop.h"
#include "oop-read.h"

#include <assert.h>
#include <string.h>

#include <unistd.h>
#include <limits.h>

typedef struct {
  oop_readable ra;
  oop_source *oop;
  int processing;
  enum { state_cancelled, state_active, state_dying } state;
  const char *data;
  size_t remaining;
  oop_readable_call *call;
  void *opaque;
} ram_intern;

static void *process(oop_source *oop, struct timeval when, void *ram_void);

static int set_time(ram_intern *ram) {
  int err;
  err=
    (ram->oop->on_time(ram->oop,OOP_TIME_NOW,process,ram), 0); /* fixme */
  if (err) return err;

  ram->processing= 1;
  return 0;
}

static void *process(oop_source *oop, struct timeval when, void *ram_void) {
  ram_intern *ram= ram_void;
  void *ret;
  int err;

  assert(oop == ram->oop);
  assert(ram->processing);

  ret= OOP_CONTINUE;

  while (ram->state == state_active && ret == OOP_CONTINUE) {
    ret= ram->call(oop,&ram->ra,ram->opaque);
  }

  switch (ram->state) {

  case state_active:
    err= set_time(ram);
    if (err)
      assert(!"must not lose flow of control");
         /* AAARGH! No way to avoid this I think.  Happens when:
	  *  - program calls on_read which works, setting immediate callback;
	  *  - process calls the application's function, which returns
	  *    OOP_HALT or some such, but without calling on_cancel;
	  *  Now we have to set another immediate callback.
	  *  If this fails and we were to ignore it then:
	  *  - program reenters event loop, expecting to deal with the rest
	  *    of the oop_readable_mem data.  But we've lost the flow
	  *    of control and the callback never happens, so
	  *    oop_sys_run or whatever would (lyingly) exit straight
	  *    away with OOP_CONTINUE.
	  *  Alternatively we could ignore the application's request
	  *  to abort the event loop, which seems just as bad.
	  */
    break;

  case state_cancelled:
    ram->processing= 0;
    break;

  case state_dying:
    oop_free(ram);
    break;
  }
  
  return ret;
}

static int on_read(oop_readable *ra, oop_readable_call *call, void *opaque) {
  ram_intern *ram= (void*)ra;

  assert(ram->state != state_dying);
  ram->state= state_active;
  ram->call= call;
  ram->opaque= opaque;

  if (ram->processing)
    return 0;

  return
    set_time(ram);
}

static void on_cancel(struct oop_readable *ra) {
  ram_intern *ram= (void*)ra;

  assert(ram->state != state_dying);
  ram->state= state_cancelled;
}

static ssize_t try_read(oop_readable *ra, void *buffer, size_t length) {
  ram_intern *ram= (void*)ra;

  if (length > SSIZE_MAX)
    length= SSIZE_MAX;

  if (length > ram->remaining)
    length= ram->remaining;

  memcpy(buffer,ram->data,length);
  ram->data += length;
  ram->remaining -= length;

  return length;
}

static void delete_kill(struct oop_readable *ra) {
  ram_intern *ram= (void*)ra;

  assert(ram->state != state_dying);
  ram->state= state_dying;
  if (!ram->processing)
    oop_free(ram);
}

static int delete_tidy(struct oop_readable *ra) {
  delete_kill(ra);
  return 0;
}

static const oop_readable functions= {
  on_read, on_cancel, try_read, delete_tidy, delete_kill
};

oop_readable *oop_readable_mem(oop_source *oop, const void *data, size_t length) {
  ram_intern *ram;

  ram= oop_malloc(sizeof(*ram));  if (!ram) return 0;

  ram->ra= functions;
  ram->oop= oop;
  ram->processing= 0;
  ram->state= state_cancelled;

  ram->data= data;
  ram->remaining= length;

  return (oop_readable*)ram;
}
