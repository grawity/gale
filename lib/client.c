#include "gale/all.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#define MAX_RETRY 60

struct gale_server {
	oop_source *source;
	struct gale_link *link;
	int retry_time;
	struct timeval retry_when;
	struct gale_text host,sub;
	struct gale_connect *connect;

	void *(*on_connect)(struct gale_server *,void *);
	void *on_connect_data;

	void *(*on_disconnect)(struct gale_server *,void *);
	void *on_disconnect_data;
};

static void *on_connect(int fd,void *user);

static void *on_retry(oop_source *source,struct timeval tv,void *user) {
	struct gale_server *s = (struct gale_server *) user;
	s->connect = gale_make_connect(s->source,s->host,on_connect,s);
	return OOP_CONTINUE;
}

static void do_retry(struct gale_server *s) {
	if (0 != s->retry_time)
		gale_alert(GALE_WARNING,"server connection failed "
			"again, waiting before retry",0);
	else
		gale_alert(GALE_WARNING,"server connection failed",0);

	gettimeofday(&s->retry_when,NULL);
	s->retry_when.tv_sec += s->retry_time;

	if (0 != s->retry_time)
		s->retry_time = s->retry_time + lrand48() % s->retry_time + 1;
	else
		s->retry_time = 2;
	if (s->retry_time > MAX_RETRY) s->retry_time /= 2;

	s->source->on_time(s->source,s->retry_when,on_retry,s);
}

static void *on_connect(int fd,void *user) {
	struct gale_server *s = (struct gale_server *) user;
	if (fd < 0)
		do_retry(s);
	else {
		if (0 != s->retry_time) {
			s->retry_time = 0;
			gale_alert(GALE_WARNING,"server connection ok",0);
		}
		s->connect = NULL;
		link_set_fd(s->link,fd);
		link_subscribe(s->link,s->sub);
	}

	if (NULL != s->on_connect) return s->on_connect(s,s->on_connect_data);
	return OOP_CONTINUE;
}

static void *on_error(struct gale_link *l,int err,void *user) {
	struct gale_server *s = (struct gale_server *) user;
	do_retry(s);
	if (NULL != s->on_disconnect) 
		return s->on_disconnect(s,s->on_disconnect_data);
	return OOP_CONTINUE;
}

struct gale_server *gale_open(
	oop_source *source,struct gale_link *l,
        struct gale_text sub,struct gale_text server) {
	struct gale_server *s;

	if (!gale_text_compare(G_("-:"),gale_text_left(sub,2)))
		sub = gale_text_right(sub,-2);

	gale_create(s);
	s->source = source;
	s->link = l;
	s->retry_time = 0;
	s->retry_when = OOP_TIME_NOW;
	s->host = server;
	if (0 == s->host.l) s->host = gale_var(G_("GALE_SERVER"));
	s->connect = NULL;
	s->sub = sub;

	gale_close(s);
	link_on_error(l,on_error,s);
	s->connect = gale_make_connect(s->source,s->host,on_connect,s);
	return s;
}

void gale_close(struct gale_server *s) {
	link_on_error(s->link,NULL,NULL);
	link_set_fd(s->link,-1);
	s->source->cancel_time(s->source,s->retry_when,on_retry,s);
	if (NULL != s->connect) gale_abort_connect(s->connect);
}

void gale_on_connect(struct gale_server *s,
     void *(*call)(struct gale_server *,void *),
     void *call_data) {
	s->on_connect = call;
	s->on_connect_data = call_data;
}

void gale_on_disconnect(struct gale_server *s,
     void *(*call)(struct gale_server *,void *),
     void *call_data) {
	s->on_disconnect = call;
	s->on_disconnect_data = call_data;
}

struct auth_id *gale_user(void) {
	if (!auth_id_public(gale_global->user_id) && !auth_id_private(gale_global->user_id))
		auth_id_gen(gale_global->user_id,gale_var(G_("GALE_FROM")));

	return gale_global->user_id;
}
