#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "gale/all.h"

struct gale_client *client;
struct gale_text category;
struct auth_id *domain;

void *gale_malloc(size_t size) { return malloc(size); }
void gale_free(void *ptr) { free(ptr); }

void usage() {
	fprintf(stderr,"%s\nusage: gdomain\n",GALE_BANNER);
	exit(1);
}

void success(struct auth_id *id,struct gale_message *msg) {
	struct gale_data data;
	export_auth_id(id,&data,0);
	msg->data.p = gale_malloc(256 + strlen(auth_id_name(id)) + data.l);
	sprintf(msg->data.p,
		"Content-Type: application/x-gale-key\r\n"
		"From: Domain Server\r\n"
		"Time: %lu\r\n"
		"Subject: success %s\r\n\r\n",
		time(NULL),auth_id_name(id));
	msg->data.l = strlen(msg->data.p);
	memcpy(msg->data.p + msg->data.l,data.p,data.l);
	msg->data.l += data.l;
	gale_free(data.p);
}

void failure(struct auth_id *id,struct gale_message *msg) {
	msg->data.p = gale_malloc(256 + strlen(auth_id_name(id)));
	sprintf(msg->data.p,
		"Content-Type: application/x-gale-key\r\n"
		"From: Domain Server\r\n"
		"Time: %lu\r\n"
		"Subject: failure %s\r\n",
		time(NULL),auth_id_name(id));
	msg->data.l = strlen(msg->data.p);
}

void incoming(struct gale_message *_msg) {
	struct gale_text cat = { NULL,0 },rcpt = { NULL,0 },trailer;
	struct gale_message *new,*msg = NULL,*reply = NULL;
	struct auth_id *key = NULL,*encrypted = NULL,*signature = NULL;
	char *user = NULL,*next,*header,*data,*end;

	encrypted = decrypt_message(_msg,&msg);
	if (!msg) goto done;
	signature = verify_message(msg);

	trailer = gale_text_from_latin1("/key",-1);

	while (gale_text_token(_msg->cat,':',&cat)) {
		if (gale_text_compare(category,gale_text_left(cat,category.l))
		||  gale_text_compare(trailer,gale_text_right(cat,trailer.l)))
			continue;
		user = gale_text_to_latin1(gale_text_left(cat,-trailer.l));
	}

	free_gale_text(trailer);
	if (!user) goto done;
	key = lookup_id(user);
	if (!key) goto done;

	next = msg->data.p; end = next + msg->data.l;
	while (parse_header(&next,&header,&data,end))
		if (!strcasecmp(header,"Receipt-To")) 
			rcpt = gale_text_from_latin1(data,-1);

	if (!rcpt.p) {
		gale_alert(GALE_WARNING,"no Receipt-To header, cannot reply",0);
		goto done;
	}

	reply = new_message();
	reply->cat = rcpt;

	if (!auth_id_public(key)) 
		failure(key,reply);
	else
		success(key,reply);

	if (!reply->data.p) goto done;

	new = sign_message(domain,reply);
	release_message(reply);
	reply = new;
	if (!reply) goto done;

	if (signature) {
		new = encrypt_message(1,&signature,reply);
		release_message(reply);
		reply = new;
		if (!reply) goto done;
	}

	link_put(client->link,reply);

done:
	if (key) free_auth_id(key);
	if (encrypted) free_auth_id(encrypted);
	if (signature) free_auth_id(signature);
	if (user) gale_free(user);
	if (msg) release_message(msg);
	if (reply) release_message(msg);
}

int main(int argc,char *argv[]) {
	int arg;

	gale_init("gdomain",argc,argv);
	while ((arg = getopt(argc,argv,"dDh")) != EOF)
	switch (arg) {
	case 'd': ++gale_debug; break;
	case 'D': gale_debug += 5; break;
	case 'h':
	case '?': usage();
	}
	if (optind != argc) usage();

	init_auth_id(&domain,getenv("GALE_DOMAIN"));
	if (!domain || !auth_id_private(domain))
		gale_alert(GALE_ERROR,"no access to domain private key",0);

	category = dom_category(NULL,"dom");
	client = gale_open(category);

	gale_daemon(0);

	for (;;) {
		struct gale_message *msg;
		while (gale_send(client)) gale_retry(client);
		while (gale_next(client)) gale_retry(client);
		while ((msg = link_get(client->link))) {
			incoming(msg);
			release_message(msg);
		}
	}

	return 0;
}
