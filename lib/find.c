#include "gale/client.h"
#include "gale/auth.h"
#include "gale/misc.h"

#include <errno.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>

#define TIMEOUT 20 /* seconds */

auth_hook _gale_find_id;
static int inhibit = 0;

void disable_gale_akd(void) {
	++inhibit;
}

void enable_gale_akd(void) {
	if (inhibit) --inhibit;
}

/*
   1: got a key
   0: no success
  -1: no key exists
*/
static int process(struct auth_id *id,struct auth_id *domain,
                   struct gale_message *msg) 
{
	struct auth_id *encrypted,*signature;
	struct gale_fragment **frags;
	struct gale_text error = null_text;
	int status = 0;

	encrypted = decrypt_message(msg,&msg);
	if (!msg) return 0;
	signature = verify_message(msg,&msg);

	for (frags = unpack_message(msg->data); *frags && !status; ++frags) {
		struct gale_fragment *frag = *frags;
		if (frag_data == frag->type
		&& !gale_text_compare(frag->name,G_("answer/key"))) {
			struct auth_id *found;
			import_auth_id(&found,frag->value.data,0);
			if (found == id) status = 1;
		}
		if (domain && signature == domain && frag_text == frag->type
		&& !gale_text_compare(frag->name,G_("answer/key/error"))) {
			error = frag->value.text;
			status = -1;
		}
	}

	return status;
}

int _gale_find_id(struct auth_id *id) {
	struct gale_client *client;
	struct gale_message *msg;
	struct gale_text tok,name,category;
	struct auth_id *domain = NULL;
	char *tmp,*tmp2;
	time_t timeout;
	int status = 0;

	name = auth_id_name(id);
	tok = null_text;
	if (gale_text_token(name,'@',&tok) && gale_text_token(name,0,&tok))
		init_auth_id(&domain,tok);
	else
		domain = id;

	/* prevent re-entrancy */
	if (inhibit) return 0;
	disable_gale_akd();

	tmp = gale_malloc(80 + name.l);
	sprintf(tmp,"requesting key \"%s\" from domain server",
	        gale_text_to_local(name));
	gale_alert(GALE_NOTICE,tmp,0);
	gale_free(tmp);

	timeout = time(NULL);
	category = id_category(id,G_("auth/key"),G_(""));
	client = gale_open(category);

	msg = new_message();

	msg->cat = id_category(id,G_("auth/query"),G_(""));
	msg->data.p = gale_malloc(name.l + 256);
	tmp2 = gale_text_to_latin1(name);
	sprintf(msg->data.p,
		"Request-Key: %s\r\n"
	        "Time: %lu\r\n",
		tmp2,timeout);
	msg->data.l = strlen(msg->data.p);

	timeout += TIMEOUT;
	link_put(client->link,msg);
	while (gale_send(client) && time(NULL) < timeout) {
		gale_retry(client);
		if (link_queue_num(client->link) < 1) 
			link_put(client->link,msg);
	}

	while (!status && time(NULL) < timeout) {
		struct gale_message *reply;
		struct timeval tv;
		fd_set fds;
		int retval;

		/* eh */
		tv.tv_sec = 3;
		tv.tv_usec = 0;

		FD_ZERO(&fds);
		FD_SET(client->socket,&fds);
		retval = select(FD_SETSIZE,(SELECT_ARG_2_T) &fds,NULL,NULL,&tv);
		if (retval < 0) {
			gale_alert(GALE_WARNING,"select",errno);
			break;
		}
		if (retval == 0) continue;
		if (gale_next(client)) {
			gale_retry(client);
			continue;
		}

		while (!status && (reply = link_get(client->link)))
			status = process(id,domain,reply);
	}

	gale_close(client);
	enable_gale_akd();
	return status > 0;
}
