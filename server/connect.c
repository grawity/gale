#include <syslog.h>
#include <fcntl.h>

#include "gale/all.h"
#include "connect.h"
#include "subscr.h"
#include "server.h"

struct connect *new_connect(int rfd,int wfd) {
	struct connect *conn;
	gale_create(conn);
	fcntl(wfd,F_SETFL,O_NONBLOCK);
	fcntl(rfd,F_SETFD,1);
	fcntl(wfd,F_SETFD,1);
	conn->rfd = rfd;
	conn->wfd = wfd;
	conn->link = new_link();
	conn->subscr.p = NULL;
	conn->subscr.l = 0;
	conn->next = NULL;
	conn->retry = NULL;
	return conn;
}

void free_connect(struct connect *conn) {
	close(conn->rfd);
	if (conn->wfd != conn->rfd) close(conn->wfd);
	if (conn->subscr.p) remove_subscr(conn);
	if (conn->retry) free_attach(conn->retry);
}

void pre_select(struct connect *conn,fd_set *r,fd_set *w) {
	if (link_receive_q(conn->link))
		FD_SET(conn->rfd,r);
	if (link_transmit_q(conn->link))
		FD_SET(conn->wfd,w);
}

void process_will(struct connect *conn) {
	struct gale_message *msg;
	if ((msg = link_willed(conn->link)))
		subscr_transmit(msg,conn);
}

int post_select(struct connect *conn,fd_set *r,fd_set *w) {
	struct gale_message *msg;
	struct gale_text sub;
	if (FD_ISSET(conn->wfd,w)) {
		gale_dprintf(3,"[%d] sending data\n",conn->wfd);
		if (link_transmit(conn->link,conn->wfd)) {
			process_will(conn);
			return -1;
		}
	}
	if (FD_ISSET(conn->rfd,r)) {
		gale_dprintf(3,"[%d] receiving data\n",conn->rfd);
		if (link_receive(conn->link,conn->rfd)) {
			process_will(conn);
			return -1;
		}
	}
	sub = link_subscribed(conn->link);
	if (sub.p) subscribe_connect(conn,sub);
	while ((msg = link_get(conn->link))) subscr_transmit(msg,conn);
	return 0;
}

void subscribe_connect(struct connect *conn,struct gale_text sub) {
	if (conn->subscr.p) remove_subscr(conn);
	conn->subscr = sub;
	add_subscr(conn);
}
