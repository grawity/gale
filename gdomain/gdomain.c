#include "gale/all.h"

#include <stdlib.h>

static oop_source *source;
static struct gale_link *line;
static struct gale_text subscriptions;

static void *on_error_packet(struct gale_packet *pkt,void *x) {
	link_put(line,pkt);
	return OOP_CONTINUE;
}

static void *on_error_message(struct gale_message *msg,void *x) {
	gale_pack_message(source,msg,on_error_packet,x);
	return OOP_CONTINUE;
}

static void *on_connected(struct gale_server *server,
	struct gale_text host,
	struct sockaddr_in addr,void *x)
{
	struct gale_error_queue *queue = gale_make_queue(source);
	gale_on_queue(queue,on_error_message,line);

	gale_daemon(source);
	gale_kill(gale_var(G_("GALE_DOMAIN")),1);
	gale_detach(source);

	if (0 != subscriptions.l) link_subscribe(line,subscriptions);
	return OOP_CONTINUE;
}

static void *on_packet(struct gale_link *link,struct gale_packet *pkt,void *x) {
	/* ... process (and respond to) message ... */
	return OOP_CONTINUE;
}

static void *on_query_location(struct gale_text name,
	struct gale_location *loc,void *x) 
{
	struct gale_location *list[2] = { loc, NULL };
	subscriptions = gale_pack_subscriptions(list,NULL);
	link_subscribe(line,subscriptions);
	return OOP_CONTINUE;
}

static void usage() {
	fprintf(stderr,"%s\n"
	"usage: gdomain [-h] [domain]\n"
	"flags: -h          Display this message\n",GALE_BANNER);
	exit(1);
}

int main(int argc,char *argv[]) {
	oop_source_sys *sys;
	struct gale_server *server;
	int arg;

	gale_init("gdomain",argc,argv);
	gale_init_signals(source = oop_sys_source(sys = oop_sys_new()));
	subscriptions = null_text;

	while ((arg = getopt(argc,argv,"dDh")) != EOF)
	switch (arg) {
	case 'd': ++gale_global->debug_level; break;
	case 'D': gale_global->debug_level += 5; break;
	case 'h':
	case '?': usage();
	}

	if (optind != argc) usage();

	line = new_link(source);
	link_on_message(line,on_packet,NULL);
	server = gale_make_server(source,line,null_text,0);
	gale_on_connect(server,on_connected,NULL);
	gale_find_location(source,G_("_gale.query"),on_query_location,NULL);

	oop_sys_run(sys);
	return 0;
}
