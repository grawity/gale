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
	struct gale_group group;

	encrypted = decrypt_message(msg,&msg);
	if (!msg) return 0;
	signature = verify_message(msg,&msg);

	group = gale_group_find(msg->data,G_("answer/key"));
	if (!gale_group_null(group)) {
		struct gale_fragment frag = gale_group_first(group);
		if (frag_data == frag.type) {
			struct auth_id *found;
			import_auth_id(&found,frag.value.data,0);
			if (found == id) return 1;
		}
	}

	group = gale_group_find(msg->data,G_("answer/key/error"));
	if (!gale_group_null(group)) {
		struct gale_fragment frag = gale_group_first(group);
		if (domain && signature == domain) return -1;
	}

	return 0;
}

int _gale_find_id(struct auth_id *id) {
	struct gale_client *client;
	struct gale_message *msg;
	struct gale_text tok,name,category;
	struct gale_fragment frag;
	struct auth_id *domain = NULL;
	char *tmp;
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
	sprintf(tmp,"requesting key \"%s\"",
	        gale_text_to_local(name));
	gale_alert(GALE_NOTICE,tmp,0);
	gale_free(tmp);

	timeout = time(NULL);
	category = id_category(id,G_("auth/key"),G_(""));
	client = gale_open(category);

	msg = new_message();

	msg->cat = id_category(id,G_("auth/query"),G_(""));
	gale_add_id(&msg->data,G_("AKD"));
	frag.name = G_("question/key");
	frag.type = frag_text;
	frag.value.text = name;
	gale_group_add(&msg->data,frag);

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
		if (retval < 0 && EINTR == errno) continue;
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
