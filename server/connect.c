#include <syslog.h>

#include "gale/all.h"
#include "connect.h"
#include "subscr.h"
#include "server.h"

struct connect *new_connect(int rfd,int wfd,int num,int mem) {
	struct connect *conn = gale_malloc(sizeof(struct connect));
	conn->rfd = rfd;
	conn->wfd = wfd;
	conn->link = new_link();
	conn->subscr = NULL;
	conn->next = NULL;
	conn->retry = NULL;
	link_limits(conn->link,num,mem);
	return conn;
}

void free_connect(struct connect *conn) {
	free_link(conn->link);
	close(conn->rfd);
	if (conn->wfd != conn->rfd) close(conn->wfd);
	if (conn->subscr) {
		remove_subscr(conn);
		gale_free(conn->subscr);
	}
	if (conn->retry) free_attach(conn->retry);
	gale_free(conn);
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
	char *cp;
	int i;
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
	if ((cp = link_subscribed(conn->link))) {
		subscribe_connect(conn,cp);
		gale_free(cp);
	}
	if ((i = link_lossage(conn->link))) {
		gale_dprintf(0,"[%d] %d messages lost\n",conn->wfd,i);
		syslog(LOG_WARNING,"%d incoming messages lost",i);
	}
	if ((msg = link_get(conn->link))) {
		subscr_transmit(msg,conn);
		release_message(msg);
	}
	return 0;
}

void subscribe_connect(struct connect *conn,char *cp) {
	if (conn->subscr) {
		remove_subscr(conn);
		gale_free(conn->subscr);
	}
	conn->subscr = gale_strdup(cp);
	add_subscr(conn);
}
