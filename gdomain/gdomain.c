#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "gale/all.h"

struct gale_text category;
struct gale_client *client;
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

void request(struct auth_id *id) {
	struct gale_message *reply;
	reply = auth_id_public(id) ? success(id) : failure(id);
	reply = _sign_message(domain,reply);
	if (reply) link_put(client->link,reply);
}

int prefix(struct gale_text x,struct gale_text prefix) {
	return !gale_text_compare(gale_text_left(x,prefix.l),prefix);
}

int suffix(struct gale_text x,struct gale_text suffix) {
	return !gale_text_compare(gale_text_right(x,suffix.l),suffix);
}

void incoming(struct gale_message *msg) {
	struct auth_id *encrypted = NULL,*signature = NULL;
	struct gale_text user = null_text;
	struct gale_group group;

	encrypted = decrypt_message(msg,&msg);
	if (!msg) return;
	signature = verify_message(msg,&msg);

	/* Figure out what we can from the headers. */

	group = gale_group_find(msg->data,G_("question/key"));
	if (!gale_group_null(group)) {
		struct gale_fragment frag = gale_group_first(group);
		if (frag_text == frag.type) user = frag.value.text;
	}

	/* Now see what we can glean from the category */

	if (!user.p) {
		struct gale_text cat = null_text;
		while (!user.p && gale_text_token(msg->cat,':',&cat))
			if (prefix(cat,category))
				user = gale_text_right(cat,-category.l);
	}

	if (!user.p)
		gale_alert(GALE_WARNING,"cannot determine the key wanted",0);
	else {
		struct auth_id *key = lookup_id(user);
		if (key) request(key);
	}
}

int main(int argc,char *argv[]) {
	int arg;

	gale_init("gdomain",argc,argv);
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
	client = gale_open(category);

	gale_daemon(0);

	for (;;) {
		struct gale_message *msg;
		while (gale_send(client)) gale_retry(client);
		while (gale_next(client)) gale_retry(client);
		while ((msg = link_get(client->link))) incoming(msg);
	}

	return 0;
}
