#include "gale/all.h"
#include "connect.h"
#include "subscr.h"
#include "server.h"

#include <assert.h>
#include <syslog.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

struct connect {
	oop_source *source;
	struct gale_link *link;
	struct gale_text subscr;
	struct gale_message *will;
	struct sockaddr_in peer;
	struct timeval expire;
	filter *func;
	void *data;
};

static struct gale_message *null_filter(struct gale_message *msg,void *d) {
	return msg;
}

static struct gale_text connect_report(void *d) {
	struct connect *conn = (struct connect *) d;
	struct sockaddr_in peer;
	int len = sizeof(peer),fd = link_get_fd(conn->link);
	if (getpeername(fd,&peer,&len) || AF_INET != peer.sin_family) 
		return null_text;

	return gale_text_concat(7,
		G_("["),
		gale_text_from_number((unsigned int) conn->link,16,8),
		G_("] connect: peer="),
		gale_text_from(NULL,inet_ntoa(peer.sin_addr),-1),
		G_(", push ["),
		conn->subscr,
		G_("]\n"));
}

static void *on_will(struct gale_link *l,struct gale_message *will,void *d) {
	struct connect *conn = (struct connect *) d;
	assert(l == conn->link);
	conn->will = will;
	return OOP_CONTINUE;
}

static void *on_message(struct gale_link *l,struct gale_message *msg,void *d) {
	struct connect *conn = (struct connect *) d;
	assert(l == conn->link);
	msg = conn->func(msg,conn->data);
	if (NULL != msg) subscr_transmit(conn->source,msg,conn);
	return OOP_CONTINUE;
}

static void *on_subscribe(struct gale_link *l,struct gale_text sub,void *d) {
	struct connect *conn = (struct connect *) d;
	assert(l == conn->link);
	remove_subscr(conn->source,conn->subscr,conn);
	conn->subscr = sub;
	add_subscr(conn->source,conn->subscr,conn);
	return OOP_CONTINUE;
}

static void *on_error(struct gale_link *l,int err,void *d) {
	struct connect *conn = (struct connect *) d;
	assert(l == conn->link);
	if (0 != err && ECONNRESET != err && EPIPE != err) {
		if (AF_INET != conn->peer.sin_family)
			gale_alert(GALE_WARNING,"I/O error",err);
		else
			gale_alert(GALE_WARNING,
			           inet_ntoa(conn->peer.sin_addr),err);
	}
	close_connect(conn);
	return OOP_CONTINUE;
}

struct connect *new_connect(
	oop_source *source,
	struct gale_link *link,
	struct gale_text subscr)
{
	int fd = link_get_fd(link);
	struct connect *conn;
	int len = sizeof(conn->peer);
	gale_create(conn);
	conn->source = source;
	conn->link = link;
	conn->subscr = subscr;
	conn->will = NULL;
	conn->func = null_filter;
	conn->expire = OOP_TIME_NOW;
	add_subscr(conn->source,conn->subscr,conn);

	if (getpeername(fd,&conn->peer,&len) 
	|| AF_INET != conn->peer.sin_family)
		memset(&conn->peer,0,sizeof(conn->peer));

	gale_report_add(gale_global->report,connect_report,conn);
	link_on_will(conn->link,on_will,conn);
	link_on_message(conn->link,on_message,conn);
	link_on_subscribe(conn->link,on_subscribe,conn);
	link_on_error(conn->link,on_error,conn);
	return conn;
}

void connect_filter(struct connect *conn,filter *func,void *data) {
	conn->func = func;
	conn->data = data;
}

static void *on_expire(oop_source *source,struct timeval when,void *v) {
	struct connect *conn = (struct connect *) v;
	struct gale_time now = gale_time_now();
	struct gale_time cut = gale_time_diff(now,gale_time_seconds(QUEUE_AGE));
	while ((QUEUE_NUM > 0 && link_queue_num(conn->link) > QUEUE_NUM)
	   ||  (QUEUE_MEM > 0 && link_queue_mem(conn->link) > QUEUE_MEM))
		link_queue_drop(conn->link);
	while (QUEUE_AGE > 0 && link_queue_num(conn->link) > 0
	   &&  gale_time_compare(link_queue_time(conn->link),cut) < 0)
		link_queue_drop(conn->link);
	source->cancel_time(source,conn->expire,on_expire,conn);
	if (QUEUE_AGE > 0 && link_queue_num(conn->link) > 0) {
		struct gale_time expire = link_queue_time(conn->link);
		expire = gale_time_add(expire,gale_time_seconds(QUEUE_AGE));
		gale_time_to(&conn->expire,expire);
		source->on_time(source,conn->expire,on_expire,conn);
	}

	return OOP_CONTINUE;
}

void send_connect(struct connect *conn,struct gale_message *msg) {
	msg = conn->func(msg,conn->data);
	if (NULL == msg) return;
	link_put(conn->link,msg);
	on_expire(conn->source,OOP_TIME_NOW,conn);
}

void close_connect(struct connect *conn) {
	gale_report_remove(gale_global->report,connect_report,conn);
	remove_subscr(conn->source,conn->subscr,conn);
	conn->subscr = G_("-");
	delete_link(conn->link);
	conn->source->cancel_time(conn->source,conn->expire,on_expire,conn);
	if (NULL != conn->will) subscr_transmit(conn->source,conn->will,conn);
}
