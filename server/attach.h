#ifndef ATTACH_H
#define ATTACH_H

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "gale/connect.h"

struct attach {
	char *server;
	char *subs;
	struct attach *next;

	struct gale_connect *connect;
	struct timeval time;
	int wait;
};

struct attach *new_attach(void);
void free_attach(struct attach *);

void attach_select(struct attach *,fd_set *wfd,
                   struct timeval *now,struct timeval *timeo);
int select_attach(struct attach *,fd_set *,struct timeval *now);

#endif
