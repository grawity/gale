#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "gale/all.h"

struct gale_text category;
struct auth_id *domain;

void usage() {
	fprintf(stderr,"%s\n"
	"usage: gdomain [-h]\n"
	"flags: -h          Display this message\n",GALE_BANNER);
	exit(1);
}

struct gale_message *slip(struct auth_id *id,struct gale_fragment frag) {
	struct gale_message *msg = new_message();
	gale_add_id(&msg->data,null_text);
	gale_group_add(&msg->data,frag);
	msg->cat = id_category(id,G_("auth/key"),G_(""));
	return msg;
}

struct gale_message *success(struct auth_id *id) {
	struct gale_fragment frag;
	frag.name = G_("answer/key");
	frag.type = frag_data;
	export_auth_id(id,&frag.value.data,0);
	return slip(id,frag);
}

struct gale_message *failure(struct auth_id *id) {
	struct gale_fragment frag;
	frag.name = G_("answer/key/error");
	frag.type = frag_text;
	frag.value.text = gale_text_concat(3,
		G_("key \""),auth_id_name(id),G_("\" not found"));
	return slip(id,frag);
}

void request(struct gale_link *link,struct auth_id *id) {
	struct gale_message *reply;
	gale_check_mem();
	reply = auth_id_public(id) ? success(id) : failure(id);
	auth_sign(&reply->data,domain,AUTH_SIGN_SELF);
	if (reply) link_put(link,reply);
}

void *on_message(struct gale_link *link,struct gale_message *msg,void *data) {
	struct auth_id *encrypted = NULL,*signature = NULL;
	struct gale_fragment frag;

	gale_check_mem();

	encrypted = auth_decrypt(&msg->data);
	if (!msg) return OOP_CONTINUE;
	signature = auth_verify(&msg->data);

	if (!gale_group_lookup(msg->data,G_("question/key"),frag_text,&frag))
		gale_alert(GALE_WARNING,"cannot determine the key wanted",0);
	else {
		struct auth_id *key;
		init_auth_id(&key,frag.value.text);
		request(link,key);
	}

	return OOP_CONTINUE;
}

int main(int argc,char *argv[]) {
	struct gale_link *link;
	struct gale_server *server;
	oop_source_sys *sys;
	oop_source *source;
	int arg;

	gale_init("gdomain",argc,argv);
	gale_init_signals(source = oop_sys_source(sys = oop_sys_new()));
	disable_gale_akd();

	while ((arg = getopt(argc,argv,"dDh")) != EOF)
	switch (arg) {
	case 'd': ++gale_global->debug_level; break;
	case 'D': gale_global->debug_level += 5; break;
	case 'h':
	case '?': usage();
	}
	if (optind != argc) usage();

	init_auth_id(&domain,gale_var(G_("GALE_DOMAIN")));
	if (!domain || !auth_id_private(domain))
		gale_alert(GALE_ERROR,"no access to domain private key",0);

	category = dom_category(auth_id_name(domain),G_("auth/query"));
	link = new_link(source);
	server = gale_open(source,link,category,null_text,0);
	gale_set_error_link(source,link);

	gale_daemon(source);
	gale_kill(auth_id_name(domain),1);
	gale_detach();
	link_on_message(link,on_message,NULL);
	oop_sys_run(sys);

	return 0;
}
