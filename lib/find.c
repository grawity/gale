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

struct akd_request {
	oop_source *source;
	enum { FOUND, NONE, DUNNO } status;
	struct auth_id *id,*domain;
};

static void *on_timeout(oop_source *source,struct timeval tv,void *user) {
	/* stop processing */
	return OOP_HALT;
}

static void *on_message(struct gale_link *l,struct gale_message *msg,void *d) {
	struct akd_request *req = (struct akd_request *) d;
	struct auth_id *encrypted,*signature;
	struct gale_group group;

	encrypted = decrypt_message(msg,&msg);
	if (!msg) return OOP_CONTINUE;
	signature = verify_message(msg,&msg);

	group = gale_group_find(msg->data,G_("answer/key"));
	if (!gale_group_null(group)) {
		struct gale_fragment frag = gale_group_first(group);
		if (frag_data == frag.type) {
			struct auth_id *found;
			import_auth_id(&found,frag.value.data,0);
			if (found == req->id) {
				req->status = FOUND;
				return OOP_HALT;
			}
		}
	}

	group = gale_group_find(msg->data,G_("answer/key/error"));
	if (!gale_group_null(group)) {
		if (NULL != req->domain && signature == req->domain) {
			req->status = NONE;
			return OOP_HALT;
		}
	}

	return OOP_CONTINUE;
}

int _gale_find_id(struct auth_id *id) {
	oop_source_sys *sys;
	oop_source *source;
	struct akd_request req;
	struct gale_server *server;
	struct gale_message *msg;
	struct gale_link *link;
	struct gale_text tok,name,category;
	struct gale_fragment frag;
	struct auth_id *domain = NULL;
	char *tmp;
	struct timeval timeout;

	name = auth_id_name(id);
	tok = null_text;
	if (gale_text_token(name,'@',&tok) && gale_text_token(name,0,&tok))
		init_auth_id(&domain,tok);
	else
		domain = id;

	/* prevent re-entrancy */
	if (inhibit) return 0;
	disable_gale_akd();

	/* notify the user */
	tmp = gale_malloc(80 + name.l);
	sprintf(tmp,"requesting key \"%s\"",gale_text_to_local(name));
	gale_alert(GALE_NOTICE,tmp,0);

	/* create the connection */
	category = id_category(id,G_("auth/key"),G_(""));
	sys = oop_sys_new();
	source = oop_sys_source(sys);
	link = new_link(source);
	server = gale_open(source,link,category,null_text,0);

	/* enqueue the request */
	msg = new_message();
	msg->cat = id_category(id,G_("auth/query"),G_(""));
	gale_add_id(&msg->data,G_("AKD"));
	frag.name = G_("question/key");
	frag.type = frag_text;
	frag.value.text = name;
	gale_group_add(&msg->data,frag);
	link_put(link,msg);

	/* set up the timeout handler */
	gettimeofday(&timeout,NULL);
	timeout.tv_sec += TIMEOUT;
	source->on_time(source,timeout,on_timeout,NULL);

	/* set up the message handler */
	req.source = source;
	req.status = NONE;
	req.id = id;
	req.domain = domain;
	link_on_message(link,on_message,&req);

	/* do it! */
	oop_sys_run(sys);

	/* one way or another, we're done... */
	source->cancel_time(source,timeout,on_timeout,NULL);
	gale_close(server);
	oop_sys_delete(sys);

	enable_gale_akd();
	return req.status == FOUND;
}
