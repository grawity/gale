/* server.h -- useful things for servers */

#ifndef GALE_SERVER_H
#define GALE_SERVER_H

#include "gale/types.h"

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

struct gale_connect;

/* Create a connect object, which represents a connection in progress.  This
   accepts a server specification (like GALE_SERVER), which contains a list
   of host[:port] entries delimited by commas.  It will attempt a connection
   to all such hosts simultaneously.  (The first one to connect "wins".)
   This call will not block; it just starts the process rolling and creates
   the object. */
struct gale_connect *make_connect(struct gale_text serv);

/* Call this before calling select().  Pass in a pointer to an fd_set,
   initialized with any other file descriptors you want to check for writing
   (possibly none -- FD_ZERO it).  You could use several connect objects
   with the same select loop. */
void connect_select(struct gale_connect *,fd_set *wfd);

/* Then call select(), passing that fd_set as the fourth argument. */

/* Then (if it succeeds) call select_connect(), giving the same fd_set and
   the connect object.  This call will return 0 if nothing has connected, but
   you should keep trying (go through the loop again, back to connect_select),
   -1 if no connection could be made (the object has been destroyed; do 
   whatever you do to handle errors), or the file descriptor if a connection 
   has succeeded (the object has been destroyed; you don't need now). */
int select_connect(fd_set *wfd,struct gale_connect *);

/* This destroys the connect object with any pending connections. */
void abort_connect(struct gale_connect *);

#endif
