#ifndef CONNECT_H
#define CONNECT_H

#include "attach.h"

#include "gale/core.h"

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

struct connect {
	int rfd,wfd;
	struct gale_link *link;
	struct gale_text subscr;
	struct connect *next;
	struct attach *retry;

	int flag,priority,stamp;
	struct connect *sub_next;
};

struct connect *new_connect(int rfd,int wfd,int old);
void free_connect(struct connect *);

void pre_select(struct connect *,fd_set *r,fd_set *w);
int post_select(struct connect *,fd_set *r,fd_set *w);

void subscribe_connect(struct connect *,struct gale_text);

#endif
