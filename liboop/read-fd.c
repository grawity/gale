/* read-fd.c, liboop, copyright 2000 Ian jackson
   
   This is free software; you can redistribute it and/or modify it under the
   terms of the GNU Lesser General Public License, version 2.1 or later.
   See the file COPYING for details. */

#include "oop.h"
#include "oop-read.h"

#include <assert.h>
#include <limits.h>
#include <errno.h>

#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

typedef struct {
  oop_readable ra;
  oop_source *oop;
  int fd;
  oop_readable_call *call;
  void *opaque;
} rafd_intern;

static void *process(oop_source *oop, int fd, oop_event event, void *rafd_void) {
  rafd_intern *rafd= rafd_void;

  assert(event == OOP_READ);
  assert(fd == rafd->fd);
  assert(oop == rafd->oop);

  return
    rafd->call(oop,&rafd->ra,rafd->opaque);
}

static void on_cancel(struct oop_readable *ra) {
  rafd_intern *rafd= (void*)ra;

  rafd->oop->cancel_fd(rafd->oop,rafd->fd,OOP_READ);
}
  
static int on_read(oop_readable *ra, oop_readable_call *call, void *opaque) {
  rafd_intern *rafd= (void*)ra;

  rafd->call= call;
  rafd->opaque= opaque;

  return
    rafd->oop->on_fd(rafd->oop,rafd->fd,OOP_READ,process,rafd), 0; /* fixme */
}

static ssize_t try_read(oop_readable *ra, void *buffer, size_t length) {
  rafd_intern *rafd= (void*)ra;
  ssize_t nread;

  for (;;) {
    nread= read(rafd->fd,buffer,length);
    if (nread != -1) break;
    if (errno != EINTR) return nread;
  }

  assert(nread >= 0);
  return nread;
}

static void delete_kill(struct oop_readable *ra) {
  oop_free(ra);
}

static int delete_tidy(struct oop_readable *ra) {
  rafd_intern *rafd= (void*)ra;
  int err;

  err= oop_fd_nonblock(rafd->fd,0);
  delete_kill(ra);
  return err;
}

static const oop_readable functions= {
  on_read, on_cancel, try_read, delete_tidy, delete_kill
};

oop_readable *oop_readable_fd(oop_source *oop, int fd) {
  rafd_intern *rafd;

  rafd= oop_malloc(sizeof(*rafd));  if (!rafd) return 0;

  rafd->ra= functions;
  rafd->oop= oop;
  rafd->fd= fd;

  if (oop_fd_nonblock(fd,1)) { oop_free(rafd); return 0; }
  return (oop_readable*)rafd;
}

int oop_fd_nonblock(int fd, int nonblock) {
  int flags;
  
  flags= fcntl(fd, F_GETFL);  if (flags == -1) return errno;
  if (nonblock) flags |= O_NONBLOCK; else flags &= ~O_NONBLOCK;
  return fcntl(fd, F_SETFL, flags) ? errno : 0;
}
