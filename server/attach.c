#include "attach.h"
#include "server.h"

#include "gale/all.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

struct attach {
	struct gale_server *server;
	struct connect *connect;
	struct gale_link *link;
	struct gale_text name,in_subs,out_subs;

	attach_empty_call *func;
	void *data;
};

static struct gale_text attach_report(void *d) {
	struct attach *att = (struct attach *) d;
	return gale_text_concat(9,
		G_("["),
		gale_text_from_number((unsigned int) att->link,16,8),
		G_("] attach: name="),
		att->name,
		G_(", pull ["),
		att->in_subs,
		G_("], push ["),
		att->out_subs,
		G_("]\n"));
}

void *on_connect(struct gale_server *server,
                 struct gale_text name,struct sockaddr_in addr,
                 void *data) 
{
	struct attach *att = (struct attach *) data;
	gale_alert(GALE_NOTICE,gale_text_to(gale_global->enc_console,
		gale_text_concat(2,
			G_("connected to "),
			gale_connect_text(name,addr))),0);
	att->name = name;
	link_will(att->link,gale_error_message(
		gale_text_concat(3,
			G_("galed will: disconnected from "),
			gale_connect_text(name,addr),G_("\n"))));
	return OOP_CONTINUE;
}

void *on_disconnect(struct gale_server *server,void *data) {
	struct attach *att = (struct attach *) data;
	gale_alert(GALE_WARNING,gale_text_to(gale_global->enc_console,
		gale_text_concat(3,
			G_("disconnected from \""),att->name,G_("\""))),0);
	return OOP_CONTINUE;
}

static void *on_empty(struct gale_link *link,void *data) {
	struct attach *att = (struct attach *) data;
	return att->func(att,att->data);
}

struct attach *new_attach(
	oop_source *source,
	struct gale_text server,
	filter *func,void *data,
	struct gale_text in,struct gale_text out) 
{
	struct gale_link *link = new_link(source);
	struct attach *att;
	gale_create(att);
	att->name = server;
	att->link = link;
	att->connect = new_connect(source,link,out);
	att->in_subs = in;
	att->out_subs = out;
	/* This overrides the default on_error ... */
	att->server = gale_open(source,link,in,server,server_port);
	gale_report_add(gale_global->report,attach_report,att);
	gale_on_connect(att->server,on_connect,att);
	gale_on_disconnect(att->server,on_disconnect,att);
	if (NULL != func) connect_filter(att->connect,func,data);
	return att;
}

void close_attach(struct attach *att) {
	gale_report_remove(gale_global->report,attach_report,att);
	gale_close(att->server);
	close_connect(att->connect);
}

void on_empty_attach(struct attach *att,attach_empty_call *f,void *d) {
	link_on_empty(att->link,f ? on_empty : NULL,att);
	att->func = f;
	att->data = d;
}
