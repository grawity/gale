#include <assert.h>
#include <syslog.h>
#include <fcntl.h>
#include <errno.h>

#include "gale/all.h"
#include "connect.h"
#include "subscr.h"

struct connect {
	oop_source *source;
	struct gale_link *link;
	struct gale_text subscr;
	struct gale_message *will;
};

static void *on_will(struct gale_link *l,struct gale_message *will,void *d) {
	struct connect *conn = (struct connect *) d;
	assert(l == conn->link);
	conn->will = will;
	return OOP_CONTINUE;
}

static void *on_message(struct gale_link *l,struct gale_message *msg,void *d) {
	struct connect *conn = (struct connect *) d;
	assert(l == conn->link);
	subscr_transmit(msg,conn->link);
	return OOP_CONTINUE;
}

static void *on_subscribe(struct gale_link *l,struct gale_text sub,void *d) {
	struct connect *conn = (struct connect *) d;
	assert(l == conn->link);
	remove_subscr(conn->subscr,conn->link);
	conn->subscr = sub;
	add_subscr(conn->subscr,conn->link);
	return OOP_CONTINUE;
}

static void *on_error(struct gale_link *l,int err,void *d) {
	struct connect *conn = (struct connect *) d;
	assert(l == conn->link);
	if (0 != err && ECONNRESET != err && EPIPE != err)
		gale_alert(GALE_WARNING,"I/O error",err);
	close_connect(conn);
	return OOP_CONTINUE;
}

struct connect *new_connect(
	oop_source *source,
	struct gale_link *link,
	struct gale_text subscr)
{
	struct connect *conn;
	gale_create(conn);
	conn->source = source;
	conn->link = link;
	conn->subscr = subscr;
	conn->will = NULL;
	add_subscr(conn->subscr,conn->link);

	link_on_will(conn->link,on_will,conn);
	link_on_message(conn->link,on_message,conn);
	link_on_subscribe(conn->link,on_subscribe,conn);
	link_on_error(conn->link,on_error,conn);
	return conn;
}

void close_connect(struct connect *conn) {
	remove_subscr(conn->subscr,conn->link);
	conn->subscr = G_("-");
	link_set_fd(conn->link,-1);
	if (NULL != conn->will) subscr_transmit(conn->will,conn->link);
}
