#include <errno.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#include "gale/util.h"
#include "server.h"
#include "attach.h"

struct attach *new_attach(void) {
	struct attach *att = gale_malloc(sizeof(struct attach));
	att->server = NULL;
	att->subs = NULL;
	att->next = NULL;
	att->connect = NULL;
	att->time.tv_sec = 0;
	att->time.tv_usec = 0;
	att->wait = 0;
	return att;
}

void free_attach(struct attach *att) {
	if (att->server) gale_free(att->server);
	if (att->subs) gale_free(att->subs);
	if (att->connect) abort_connect(att->connect);
}

static int tv_less(const struct timeval *a,const struct timeval *b) {
	if (a->tv_sec < b->tv_sec) return 1;
	if (a->tv_sec > b->tv_sec) return 0;
	return (a->tv_usec < b->tv_usec);
}

static void tv_sub(struct timeval *a,const struct timeval *b) {
	if (a->tv_usec < b->tv_usec) {
		a->tv_usec += 1000000;
		a->tv_sec -= 1;
	}
	a->tv_usec -= b->tv_usec;
	a->tv_sec -= b->tv_sec;
}

static void delay_attach(struct attach *att,struct timeval *now) {
	dprintf(3,"... \"%s\": connection failed, waiting %d seconds\n",
	        att->server,att->wait);
	att->time.tv_sec = now->tv_sec + att->wait;
	att->time.tv_usec = now->tv_usec;
	if (att->wait)
		att->wait += lrand48() % att->wait + 1;
	else
		att->wait = 2;
}

void attach_select(struct attach *att,fd_set *wfd,
                   struct timeval *now,struct timeval *timeo)
{
	do {
		if (tv_less(now,&att->time)) {
			struct timeval diff = att->time;
			tv_sub(&diff,now);
			dprintf(4,"... \"%s\": setting timeout to %d.%06d\n",
			        att->server,diff.tv_sec,diff.tv_usec);
			if (tv_less(&diff,timeo)) *timeo = diff;
			return;
		}
		if (!att->connect) {
			dprintf(3,"... \"%s\": attempting connection\n",
			        att->server);
			att->connect = make_connect(att->server);
			if (!att->connect) delay_attach(att,now);
		}
	} while (!att->connect);
	connect_select(att->connect,wfd);
}

int select_attach(struct attach *att,fd_set *wfd,struct timeval *now) {
	int fd;
	if (!att->connect) return -1;
	fd = select_connect(wfd,att->connect);
	if (!fd) return -1;
	att->connect = NULL;
	if (fd > 0) {
		dprintf(3,"[%d] \"%s\": successful connection\n",
		        fd,att->server);
		att->wait = 0;
		return fd;
	}
	delay_attach(att,now);
	return -1;
}
