#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include "gale/client.h"
#include "gale/link.h"
#include "gale/util.h"
#include "gale/connect.h"
#include "gale/compat.h"

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
		fprintf(stderr,"gale: server connection failed");
		if (retry_time)
			fprintf(stderr," again, waiting %d seconds",retry_time);
		fprintf(stderr,"\r\n");
		sleep(retry_time);
		if (retry_time)
			retry_time = retry_time + lrand48() % retry_time + 1;
		else
			retry_time = 2;
		do_connect(client);
	} while (client->socket < 0);
	fprintf(stderr,"gale: server connection ok\r\n");
}

struct gale_client *gale_open(const char *spec,int num,int mem) {
	struct gale_client *client;
	char *at;

	client = gale_malloc(sizeof(*client));

	if (spec && (at = strrchr(spec,'@'))) {
		client->server = at[1] ? gale_strdup(at+1) : NULL;
		client->subscr = gale_strndup(spec,at - spec);
	} else {
		client->server = NULL;
		client->subscr = NULL;
		if (spec) {
			if (spec[0] == '%')
				client->server = gale_strdup(spec + 1);
			else
				client->subscr = gale_strdup(spec);
		}
	}

	client->socket = -1;
	client->link = new_link();
	link_limits(client->link,num,mem);

	if (!client->server || !client->server[0]) {
		char *env = getenv("GALE_SERVER");
		if (client->server) gale_free(client->server);
		if (env) client->server = gale_strdup(env);
	}
	if (!client->server || !client->server[0]) {
		fprintf(stderr,
		"gale: no server given and $GALE_SERVER not set\n");
		gale_close(client);
		return NULL;
	}

	do_connect(client);

	return client;
}

void gale_close(struct gale_client *client) {
	if (client->socket != -1) close(client->socket);
	free_link(client->link);
	gale_free(client->subscr);
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
