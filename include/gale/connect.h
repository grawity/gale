#ifndef GALE_CONNECT_H
#define GALE_CONNECT_H

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

struct gale_connect;

struct gale_connect *make_connect(const char *serv);
void connect_select(struct gale_connect *,fd_set *wfd);
int select_connect(fd_set *wfd,struct gale_connect *);
void abort_connect(struct gale_connect *);

#endif
