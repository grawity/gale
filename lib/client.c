#include "gale/server.h"
#include "gale/client.h"
#include "gale/core.h"
#include "gale/compat.h"
#include "gale/misc.h"
#include "gale/auth.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>

struct auth_id *gale_user(void) {
	static struct auth_id *user_id = NULL;

	if (user_id) return user_id;

	user_id = lookup_id(gale_var(G_("GALE_ID")));
	if (!auth_id_public(user_id))
		auth_id_gen(user_id,gale_var(G_("GALE_FROM")));

	return user_id;
}

static void do_connect(struct gale_client *client) {
	struct gale_connect *conn = make_connect(client->server);
	if (client->socket != -1) close(client->socket);
	client->socket = -1;
	if (!conn) return;
	do {
		fd_set fds;
		FD_ZERO(&fds);
		connect_select(conn,&fds);
		select(FD_SETSIZE,NULL,(SELECT_ARG_2_T) &fds,NULL,NULL);
		client->socket = select_connect(&fds,conn);
	} while (!client->socket);
	if (client->socket > 0 && client->subscr.p)
		link_subscribe(client->link,client->subscr);
	while (client->socket > 0 && link_version(client->link) < 0) {
		fd_set rfd,wfd;
		FD_ZERO(&rfd);
		FD_ZERO(&wfd);
		if (link_receive_q(client->link)) FD_SET(client->socket,&rfd);
		if (link_transmit_q(client->link)) FD_SET(client->socket,&wfd);
		select(FD_SETSIZE,
			(SELECT_ARG_2_T) &rfd,
			(SELECT_ARG_2_T) &wfd,
			NULL,NULL);
		if ((FD_ISSET(client->socket,&rfd) 
		&&  link_receive(client->link,client->socket))
		||  (FD_ISSET(client->socket,&wfd)
		&&  link_transmit(client->link,client->socket))) {
			close(client->socket);
			client->socket = -1;
		}
	}
}

void gale_retry(struct gale_client *client) {
	int retry_time = 0;
	srand48(getpid() ^ time(NULL));
	reset_link(client->link);
	do {
		if (retry_time)
			gale_alert(GALE_WARNING,"server connection failed "
				"again, waiting before retry",0);
		else
			gale_alert(GALE_WARNING,"server connection failed",0);
		sleep(retry_time);
		if (retry_time)
			retry_time = retry_time + lrand48() % retry_time + 1;
		else
			retry_time = 2;
		do_connect(client);
	} while (client->socket < 0);
	gale_alert(GALE_NOTICE,"server connection ok",0);
}

struct gale_client *gale_open(struct gale_text spec) {
	struct gale_client *client;

	gale_create(client);
	client->server = gale_strdup(getenv("GALE_SERVER"));
	client->subscr = spec;
	client->socket = -1;
	client->link = new_link();

	if (!client->server) gale_alert(GALE_ERROR,"$GALE_SERVER not set\n",0);

	do_connect(client);

	return client;
}

void gale_close(struct gale_client *client) {
	if (client->socket != -1) close(client->socket);
	gale_free(client->server);
	gale_free(client);
}

int gale_error(struct gale_client *client) {
	return client->socket == -1;
}

int gale_send(struct gale_client *client) {
	if (client->socket == -1) return -1;
	while (link_transmit_q(client->link))
		if (link_transmit(client->link,client->socket)) return -1;
	return 0;
}

int gale_next(struct gale_client *client) {
	if (client->socket == -1) return -1;
	gale_send(client);
	if (link_receive_q(client->link))
		if (link_receive(client->link,client->socket)) return -1;
	return 0;
}
