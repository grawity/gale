#include "gale/client.h"
#include "gale/core.h"
#include "gale/server.h"
#include "gale/compat.h"
#include "gale/misc.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>

static void do_connect(struct gale_client *client) {
	struct gale_connect *conn = make_connect(client->server);
	if (client->socket != -1) close(client->socket);
	client->socket = -1;
	if (!conn) return;
	do {
		fd_set fds;
		FD_ZERO(&fds);
		connect_select(conn,&fds);
		select(FD_SETSIZE,NULL,HPINT &fds,NULL,NULL);
		client->socket = select_connect(&fds,conn);
	} while (!client->socket);
	if (client->socket > 0 && client->subscr)
		link_subscribe(client->link,client->subscr);
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

struct gale_client *gale_open(const char *spec) {
	struct gale_client *client;

	client = gale_malloc(sizeof(*client));

	client->server = gale_strdup(getenv("GALE_SERVER"));
	client->subscr = spec ? gale_strdup(spec) : NULL;
	client->socket = -1;
	client->link = new_link();

	if (!client->server) gale_alert(GALE_ERROR,"$GALE_SERVER not set\n",0);

	do_connect(client);

	return client;
}

void gale_close(struct gale_client *client) {
	if (client->socket != -1) close(client->socket);
	free_link(client->link);
	if (client->subscr) gale_free(client->subscr);
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
