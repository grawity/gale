#include "gale/all.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#define MAX_RETRY 60

struct gale_server {
	oop_source *source;
	struct gale_link *link;
	int retry_time,avoid_local_port;
	struct timeval retry_when;
	struct gale_text host;
	struct gale_connect *connect;

	gale_call_connect *on_connect;
	void *on_connect_data;
	int on_connect_called;
	struct gale_text current_host;
	struct sockaddr_in current_addr;

	gale_call_disconnect *on_disconnect;
	void *on_disconnect_data;
	int on_disconnect_called;
};

static gale_connect_call on_connect;

static struct gale_text server_report(void *user) {
	struct gale_server *s = (struct gale_server *) user;
	return gale_text_concat(5,
		G_("["),
		gale_text_from_number((unsigned int) s->link,16,8),
		G_("] server: name="),
		s->host,
		G_("\n"));
}

static void *on_retry(oop_source *source,struct timeval tv,void *user) {
	struct gale_server *s = (struct gale_server *) user;
	s->connect = gale_make_connect(
		s->source,s->host,s->avoid_local_port,
		on_connect,s);
	return OOP_CONTINUE;
}

static void do_retry(struct gale_server *s,int do_alert) {
	if (do_alert && 0 == s->retry_time)
		gale_alert(GALE_WARNING,gale_text_concat(3,
			G_("link to "),s->host,
			G_(" failed, will retry")),0);

	gettimeofday(&s->retry_when,NULL);
	s->retry_when.tv_sec += s->retry_time;

	if (0 != s->retry_time)
		s->retry_time = s->retry_time + lrand48() % s->retry_time + 1;
	else
		s->retry_time = 2;
	if (s->retry_time > MAX_RETRY) s->retry_time /= 2;

	s->source->on_time(s->source,s->retry_when,on_retry,s);
}

static void *on_event(oop_source *oop,struct timeval now,void *user) {
	struct gale_server *s = (struct gale_server *) user;

	if (-1 != link_get_fd(s->link)) {
		s->on_disconnect_called = 0;
		if (NULL != s->on_connect && !s->on_connect_called) {
			s->on_connect_called = 1;
			return s->on_connect(s,
				s->current_host,
				s->current_addr,s->on_connect_data);
		}
	} else {
		s->on_connect_called = 0;
		if (NULL != s->on_disconnect && !s->on_disconnect_called) {
			s->on_disconnect_called = 1;
			return s->on_disconnect(s,s->on_disconnect_data);
		}
	}

	return OOP_CONTINUE;
}

static void *on_connect(int fd,
	struct gale_text host,struct sockaddr_in addr,
	int found_local,void *user) 
{
	struct gale_server *s = (struct gale_server *) user;
	s->connect = NULL;

	if (fd < 0)
		do_retry(s,!found_local);
	else {
		if (0 != s->retry_time) {
			s->retry_time = 0;
			gale_alert(GALE_NOTICE,gale_text_concat(3,
				G_("link to "),s->host,
				G_(" ok")),0);
		}

		s->current_host = host;
		s->current_addr = addr;
		link_set_fd(s->link,fd);
		s->source->on_time(s->source,OOP_TIME_NOW,on_event,s);
	}

	return OOP_CONTINUE;
}

static void *on_error(struct gale_link *l,int err,void *user) {
	struct gale_server *s = (struct gale_server *) user;
	assert(l == s->link);
	link_set_fd(l,-1);
	do_retry(s,1);
	s->source->on_time(s->source,OOP_TIME_NOW,on_event,s);
	return OOP_CONTINUE;
}

struct gale_server *gale_make_server(
	oop_source *source,struct gale_link *l,
        struct gale_text server,int avoid_local_port) {
	struct gale_server *s;

	gale_create(s);
	s->source = source;
	s->link = l;
	s->retry_time = 0;
	s->avoid_local_port = avoid_local_port;
	s->retry_when = OOP_TIME_NOW;
	s->host = server;
	if (0 == s->host.l) s->host = gale_var(G_("GALE_PROXY"));
	if (0 == s->host.l) s->host = gale_var(G_("GALE_DOMAIN"));
        if (0 == s->host.l) gale_alert(GALE_ERROR,G_("$GALE_DOMAIN not set"),0);
	s->on_connect = NULL;
	s->on_disconnect = NULL;

	link_set_fd(l,-1);
	link_on_error(l,on_error,s);
	s->connect = gale_make_connect(
		s->source,s->host,s->avoid_local_port,
		on_connect,s);

	gale_report_add(gale_global->report,server_report,s);
	return s;
}

void gale_close(struct gale_server *s) {
	gale_report_remove(gale_global->report,server_report,s);
	link_on_error(s->link,NULL,NULL);
	delete_link(s->link);
	s->source->cancel_time(s->source,s->retry_when,on_retry,s);
	if (NULL != s->connect) gale_abort_connect(s->connect);
}

void gale_on_connect(struct gale_server *s,gale_call_connect *f,void *d) {
	s->on_connect = f;
	s->on_connect_data = d;
	s->on_connect_called = 0;
	s->source->on_time(s->source,OOP_TIME_NOW,on_event,s);
}

void gale_on_disconnect(struct gale_server *s,gale_call_disconnect *f,void *d) {
	s->on_disconnect = f;
	s->on_disconnect_data = d;
	s->on_disconnect_called = 0;
	s->source->on_time(s->source,OOP_TIME_NOW,on_event,s);
}

#if 0
struct auth_id *gale_user(void) {
	struct auth_id *user_id = lookup_id(gale_var(G_("GALE_ID")));
	if (!auth_id_public(user_id) 
	&&  !auth_id_private(user_id))
	{
		struct gale_fragment frag;
		struct gale_group group = gale_group_empty();

		frag.name = G_("key.owner");
		frag.type = frag_text;
		frag.value.text = gale_var(G_("GALE_NAME"));
		gale_group_add(&group,frag);
		auth_id_gen(user_id,group);
	}

	return user_id;
}
#endif
