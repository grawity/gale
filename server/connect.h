#ifndef CONNECT_H
#define CONNECT_H

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include "gale/link.h"
#include "attach.h"

struct connect {
	int rfd,wfd;
	struct gale_link *link;
	char *subscr;
	struct connect *next;
	int stamp;
	struct attach *retry;
};

struct connect *new_connect(int rfd,int wfd,int num,int mem);
void free_connect(struct connect *);

void pre_select(struct connect *,fd_set *r,fd_set *w);
int post_select(struct connect *,fd_set *r,fd_set *w);

void subscribe_connect(struct connect *,char *);

#endif
