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
	struct gale_link *link;
	struct gale_text name;
};

void *on_connect(struct gale_server *server,struct gale_text name,void *data) {
	struct attach *att = (struct attach *) data;
	gale_alert(GALE_NOTICE,gale_text_to_local(gale_text_concat(3,
		G_("connected to \""),name,G_("\""))),0);
	att->name = name;
	link_will(att->link,gale_error_message(
		gale_text_concat(3,
			G_("galed will: disconnected from \""),
			name,G_("\"\n"))));
	return OOP_CONTINUE;
}

void *on_disconnect(struct gale_server *server,void *data) {
	struct attach *att = (struct attach *) data;
	gale_alert(GALE_WARNING,gale_text_to_local(gale_text_concat(3,
		G_("disconnected from \""),att->name,G_("\""))),0);
	return OOP_CONTINUE;
}

struct attach *new_attach(
	oop_source *source,
	struct gale_text server,
	struct gale_text in,struct gale_text out) 
{
	struct gale_link *link = new_link(source);
	struct attach *att;
	gale_create(att);
	att->name = server;
	att->link = link;
	att->connect = new_connect(source,link,out);
	/* This overrides the default on_error ... */
	att->server = gale_open(source,link,in,server);
	gale_on_connect(att->server,on_connect,att);
	gale_on_disconnect(att->server,on_disconnect,att);
	return att;
}

void close_attach(struct attach *att) {
	gale_close(att->server);
	close_connect(att->connect);
}
