#include "attach.h"

#include "gale/client.h"
#include "gale/misc.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

struct attach {
	struct gale_server *server;
	struct connect *connect;
};

struct attach *new_attach(
	oop_source *source,
	struct gale_text server,
	struct gale_text in,struct gale_text out) 
{
	struct gale_link *link = new_link(source);
	struct attach *att;
	gale_create(att);
	att->connect = new_connect(source,link,out);
	/* This overrides the default on_error ... */
	att->server = gale_open(source,link,in,server);
	return att;
}

void close_attach(struct attach *att) {
	gale_close(att->server);
	close_connect(att->connect);
}
