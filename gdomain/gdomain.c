#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "gale/all.h"

struct gale_client *client;
struct gale_text old_cat,new_cat;
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

void request(struct auth_id *id,struct gale_text category) {
	struct gale_message *reply = new_message();
	reply->cat = gale_text_dup(category);

	if (!auth_id_public(id)) 
		failure(id,reply);
	else
		success(id,reply);

	if (reply->data.p) {
		struct gale_message *new = _sign_message(domain,reply);
		if (new) {
			link_put(client->link,new);
			release_message(new);
		}
	}

	release_message(reply);
}

int prefix(struct gale_text x,struct gale_text prefix) {
	return !gale_text_compare(gale_text_left(x,prefix.l),prefix);
}

int suffix(struct gale_text x,struct gale_text suffix) {
	return !gale_text_compare(gale_text_right(x,suffix.l),suffix);
}

void incoming(struct gale_message *_msg) {
	struct gale_text rcpt = { NULL,0 };
	struct gale_message *msg = NULL;
	struct auth_id *encrypted = NULL,*signature = NULL;
	char *user = NULL;

	encrypted = decrypt_message(_msg,&msg);
	if (!msg) return;
	signature = verify_message(msg);

	/* Figure out what we can from the headers. */

	{
		char *next = msg->data.p,*end = next + msg->data.l;
		char *header,*data;
		while (parse_header(&next,&header,&data,end)) {
			if (!strcasecmp(header,"Request-Key"))
				user = gale_strdup(data);
			else if (!strcasecmp(header,"Receipt-To")) {
				if (rcpt.p) free_gale_text(rcpt);
				rcpt = gale_text_from_latin1(data,-1);
			}
		}
	}

	/* Now see what we can glean from the category */

	if (!user) {
		struct gale_text trailer = gale_text_from_latin1("/key",-1);
		struct gale_text cat = { NULL, 0 };

		while (gale_text_token(_msg->cat,':',&cat)) {
			if (prefix(cat,old_cat) && suffix(cat,trailer))
				user = gale_text_to_latin1(
					gale_text_right(
						gale_text_left(cat,-trailer.l),
						-old_cat.l));
			else if (prefix(cat,new_cat))
				user = gale_text_to_latin1(
					gale_text_right(cat,-new_cat.l));
		}

		free_gale_text(trailer);
	}

	if (!user) 
		gale_alert(GALE_WARNING,"cannot determine the key wanted",0);
	else {
		struct auth_id *key = lookup_id(user);
		if (!rcpt.p) rcpt = id_category(key,"auth/key","");
		gale_dprintf(3,"--- looking up key for %s\n",user);
		if (key) {
			request(key,rcpt);
			free_auth_id(key);
		}
	}

	if (rcpt.p) free_gale_text(rcpt);

	if (encrypted) free_auth_id(encrypted);
	if (signature) free_auth_id(signature);
	if (user) gale_free(user);
	if (msg) release_message(msg);
}

int main(int argc,char *argv[]) {
	int arg;
	struct gale_text category,colon;

	gale_init("gdomain",argc,argv);
	disable_gale_akd();

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

	old_cat = dom_category(NULL,"dom");
	new_cat = dom_category(NULL,"auth/query");
	colon = gale_text_from_latin1(":",1);
	category = new_gale_text(old_cat.l + new_cat.l + colon.l);
	gale_text_append(&category,old_cat);
	gale_text_append(&category,colon);
	gale_text_append(&category,new_cat);
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
