#include "gale/all.h"

#include <stdlib.h>

/* HACK */
struct gale_text key_i_swizzle(struct gale_text);

static oop_source *source;
static struct gale_link *line;
static struct gale_text subscriptions;
static struct gale_location *domain_location = NULL;

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

	if (0 != subscriptions.l) link_subscribe(line,subscriptions);
	return OOP_CONTINUE;
}

static void *on_response(struct gale_packet *pk,void *x) {
	link_put(line,pk);
	return OOP_CONTINUE;
}

static struct gale_group response(struct gale_key *key) {
	const struct gale_key_assertion * const pub = 
		gale_key_public(key,gale_time_now());
	struct gale_group data = gale_group_empty();
	struct gale_fragment frag;

	if (NULL != pub) {
		frag.name = G_("answer/key");
		frag.type = frag_data;
		frag.value.data = gale_key_raw(pub);
	} else if (NULL == domain_location) 
		return data;
	else {
		frag.name = G_("answer/key/error");
		frag.type = frag_text;
		frag.value.text = gale_text_concat(4,
			gale_location_name(domain_location),
			G_(" cannot find \""),
			gale_key_name(key),
			G_("\""));
	}

	gale_group_add(&data,frag);
	return data;
}

static void *on_location(struct gale_text n,struct gale_location *loc,void *x) {
	struct gale_key *key = (struct gale_key *) x;
	struct gale_message *msg;

	gale_create(msg);
	gale_create_array(msg->to,2);
	msg->to[0] = loc;
	msg->to[1] = NULL;

	gale_create_array(msg->from,2);
	msg->from[0] = domain_location;
	msg->from[1] = NULL;
	msg->data = response(key);

	if (gale_group_null(msg->data)) return OOP_CONTINUE;
	gale_pack_message(source,msg,on_response,NULL);
	return OOP_CONTINUE;
}

static void *on_key(oop_source *oop,struct gale_key *key,void *x) {
	gale_find_exact_location(source,
		gale_text_concat(2,G_("_gale.key."),gale_key_name(key)),
		on_location,key);
	return OOP_CONTINUE;
}

static void *on_old_key(oop_source *oop,struct gale_key *key,void *x) {
	struct gale_packet *pk;
	const struct gale_group data = response(key);
	const struct gale_text name = key_i_swizzle(gale_key_name(key));
	struct gale_text local = null_text,domain = null_text;

	if (!gale_text_token(name,'@',&local)) return OOP_CONTINUE;

	domain = local;
	if (!gale_text_token(name,'@',&domain)) return OOP_CONTINUE;

	gale_create(pk);
	pk->routing = gale_text_concat(5,G_("@"),
		gale_text_replace(domain,G_(":"),G_("..")),
		G_("/auth/key/"),
		gale_text_replace(local,G_(":"),G_("..")),G_("/"));

	pk->content.p = gale_malloc(gale_group_size(data));
	pk->content.l = 0;
	gale_pack_group(&pk->content,data);
	return on_response(pk,x);
}

static void *on_message(struct gale_message *msg,void *x) {
	struct gale_fragment frag;
	if (NULL == msg) return OOP_CONTINUE;
	if (gale_group_lookup(msg->data,G_("question.key"),frag_text,&frag))
		gale_key_search(source,
			gale_key_handle(frag.value.text),
			search_all & ~search_private & ~search_slow,
			on_key,x);
	else
	if (gale_group_lookup(msg->data,G_("question/key"),frag_text,&frag))
		gale_key_search(source,
			gale_key_handle(key_i_swizzle(frag.value.text)),
			search_all & ~search_private & ~search_slow,
			on_old_key,x);
	return OOP_CONTINUE;
}

static void *on_request(struct gale_link *link,struct gale_packet *pk,void *x) {
	/* HACK */
	pk->routing = gale_text_concat(4,
		G_("@"),gale_var(G_("GALE_DOMAIN")),
		G_("/user/_gale/auth/:"),
		pk->routing);
	gale_unpack_message(source,pk,on_message,x);
	return OOP_CONTINUE;
}

static void *on_domain_location(
	struct gale_text name,
	struct gale_location *loc,void *x)
{
	const struct gale_text domain = gale_var(G_("GALE_DOMAIN"));

	if (NULL == loc
	|| !gale_key_private(gale_location_key(loc)))
		gale_alert(GALE_ERROR,gale_text_concat(3,
			G_("no private key for \""),
			domain,G_("\"")),0);

	gale_alert(GALE_NOTICE,gale_text_concat(3,
		G_("serving keys for \""),
		domain,G_("\"")),0);

	gale_daemon(source);
	gale_kill(domain,1);
	gale_detach(source);

	domain_location = loc;
	return OOP_CONTINUE;
}

static void *on_query_location(
	struct gale_text name,
	struct gale_location *loc,void *x) 
{
	struct gale_location *list[2] = { loc, NULL };
	subscriptions = gale_text_concat(4,
		gale_pack_subscriptions(list,NULL),
		G_(":@"),gale_var(G_("GALE_DOMAIN")),G_("/auth/query/"));
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

	while ((arg = getopt(argc,argv,"dDKh")) != EOF)
	switch (arg) {
	case 'd': ++gale_global->debug_level; break;
	case 'D': gale_global->debug_level += 5; break;
	case 'K': gale_kill(gale_var(G_("GALE_DOMAIN")),1); return 0;
	case 'h':
	case '?': usage();
	}

	if (optind != argc) {
		struct gale_text arg = gale_text_from(
			gale_global->enc_cmdline,
			argv[optind++],-1);
		gale_set(G_("GALE_DOMAIN"),arg);
	}

	if (optind != argc) usage();

	line = new_link(source);
	link_on_message(line,on_request,NULL);
	server = gale_make_server(source,line,null_text,0);
	gale_on_connect(server,on_connected,NULL);
	gale_find_exact_location(source,
		gale_var(G_("GALE_DOMAIN")),
		on_domain_location,NULL);
	gale_find_location(source,
		G_("_gale.query"),
		on_query_location,NULL);

	oop_sys_run(sys);
	return 0;
}
