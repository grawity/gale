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
                   struct gale_message *_msg) 
{
	struct gale_message *msg;
	struct auth_id *encrypted,*signature;
	int status = 0;

	encrypted = decrypt_message(_msg,&msg);
	if (!msg) return 0;
	signature = verify_message(msg);

	if (signature == id) {
		assert(auth_id_public(id));
		status = 1;
	} else {
		char *next,*key,*data,*end;
		next = msg->data.p;
		end = next + msg->data.l;
		while (parse_header(&next,&key,&data,end)) {
			if (domain && signature == domain
			&&  !strcasecmp(key,"subject")
			&&  !strncasecmp(data,"failure",7)) {
				status = -1;
				break;
			}
		}
		if (status == 0) {
			struct gale_data blob;
			blob.l = end - next;
			blob.p = next;
			if (blob.l != 0) {
				struct auth_id *found;
				import_auth_id(&found,blob,0);
				if (found == id) status = 1;
				if (found) free_auth_id(found);
			}
		}
	}

	if (signature) free_auth_id(signature);
	if (encrypted) free_auth_id(encrypted);
	release_message(msg);
	return status;
}

void _gale_find_id(struct auth_id *id) {
	struct gale_client *client;
	struct gale_message *msg;
	char *tmp;
	struct gale_text category,cat1,cat2,cat3,colon;
	const char *name;
	struct auth_id *domain = NULL;
	time_t timeout;
	int status = 0;

	name = auth_id_name(id);
	tmp = strchr(name,'@');
	if (!tmp)
		domain = id;
	else {
		init_auth_id(&domain,tmp + 1);
		auth_id_public(domain);
	}

	/* prevent re-entrancy */
	if (inhibit) return;
	disable_gale_akd();

	tmp = gale_malloc(80 + strlen(name));
	sprintf(tmp,"requesting key \"%s\" from domain server",name);
	gale_alert(GALE_NOTICE,tmp,0);
	gale_free(tmp);

	timeout = time(NULL);
	category = id_category(id,"auth/key","");
	client = gale_open(category);

	msg = new_message();

	if (id == domain)
		msg->cat = id_category(id,"auth/query","");
	else
	{
		cat1 = id_category(id,"dom","key");
		cat2 = id_category(id,"user",":/ping");
		cat3 = id_category(id,"auth/query","");
		colon = gale_text_from_latin1(":",1);
		msg->cat = new_gale_text(cat1.l + cat2.l + cat3.l + 2*colon.l);
		gale_text_append(&msg->cat,cat1);
		gale_text_append(&msg->cat,colon);
		gale_text_append(&msg->cat,cat2);
		gale_text_append(&msg->cat,colon);
		gale_text_append(&msg->cat,cat3);
		free_gale_text(cat1); 
		free_gale_text(cat2); 
		free_gale_text(cat3);
		free_gale_text(colon);
	}

	msg->data.p = gale_malloc(strlen(name) + category.l + 256);
	tmp = gale_text_to_latin1(category);
	sprintf(msg->data.p,
	        "Receipt-To: %s\r\n"
		"Request-Key: %s\r\n"
	        "Time: %lu\r\n",
		tmp,name,timeout);
	gale_free(tmp);
	msg->data.l = strlen(msg->data.p);
	free_gale_text(category);

	timeout += TIMEOUT;
	link_put(client->link,msg);
	while (gale_send(client) && time(NULL) < timeout) {
		gale_retry(client);
		if (link_queue_num(client->link) < 1) 
			link_put(client->link,msg);
	}

	release_message(msg);

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

		while (!status && (reply = link_get(client->link))) {
			status = process(id,domain,reply);
			release_message(reply);
		}
	}

	if (domain && domain != id) free_auth_id(domain);
	gale_close(client);
	enable_gale_akd();
}
