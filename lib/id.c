#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

#include "gale/all.h"

#define TIMEOUT 20 /* seconds */

static int old_style(void) {
	if (getenv("GALE_NEW_STYLE")) return 0;
	if (getenv("GALE_OLD_STYLE")) return 0;
	return time(NULL) <= 883641600;
}

struct auth_id *lookup_id(const char *spec) {
	char *at = strchr(spec,'@');
	struct auth_id *id;

	if (at) init_auth_id(&id,spec);
	else {
		const char *domain = getenv("GALE_DOMAIN");
		char *tmp = gale_malloc(strlen(domain) + strlen(spec) + 2);
		sprintf(tmp,"%s@%s",spec,domain);
		init_auth_id(&id,tmp);
		gale_free(tmp);
	}

	return id;
}

struct gale_text id_category(struct gale_id *id,const char *pfx,const char *sfx)
{
	const char *name = auth_id_name(id);
	char *tmp = gale_malloc(strlen(name) + strlen(pfx) + strlen(sfx) + 4);
	const char *at = strchr(name,'@');
	struct gale_text text;

	if (old_style())
		sprintf(tmp,"%s/%s/%.*s/%s",pfx,at+1,at - name,name,sfx);
	else
		sprintf(tmp,"@%s/%s/%.*s/%s",at+1,pfx,at - name,name,sfx);

	text = gale_text_from_local(tmp,-1);
	gale_free(tmp);
	return text;
}

struct gale_text dom_category(const char *dom,const char *pfx) {
	char *tmp;
	struct gale_text text;

	if (!dom) dom = getenv("GALE_DOMAIN");
	tmp = gale_malloc(strlen(pfx) + strlen(dom) + 4);
	if (old_style())
		sprintf(tmp,"%s/%s/",pfx,dom);
	else
		sprintf(tmp,"@%s/%s/",dom,pfx);

	text = gale_text_from_local(tmp,-1);
	gale_free(tmp);
	return text;
}

/*
   1: got a key
   0: no success
  -1: no key exists
*/
static int process(struct auth_id *id,struct auth_id *domain,struct gale_message *_msg) {
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
		if (domain && signature == domain) {
			char *next,*key,*data,*end;
			next = msg->data.p;
			end = next + msg->data.l;
			while (parse_header(&next,&key,&data,end)) {
				if (!strcasecmp(key,"subject")
				&&  !strncasecmp(data,"failure",7)) {
					status = -1;
					break;
				}
			}
			if (status == 0) {
				struct gale_data blob;
				struct auth_id *found;
				blob.l = end - next;
				blob.p = next;
				import_auth_id(&found,blob,0);
				status = (found == id) ? 1 : -1;
				if (found) free_auth_id(found);
			}
		}
	}

	if (signature) free_auth_id(signature);
	if (encrypted) free_auth_id(encrypted);
	release_message(msg);
	return status;
}

int find_id(struct auth_id *id) {
	struct gale_client *client;
	struct gale_message *msg,*_msg;
	char *tmp;
	struct gale_text category,cat1,cat2,colon;
	const char *name = auth_id_name(id);
	struct auth_id *domain = NULL;
	time_t timeout;
	int status = 0;

	if (auth_id_public(id)) return 1;
	tmp = strchr(name,'@');
	if (!tmp) return 0;
	init_auth_id(&domain,tmp + 1);
	if (!domain) return 0;
	
	tmp = gale_malloc(80 + strlen(name));
	sprintf(tmp,"requesting key \"%s\" from domain server",name);
	gale_alert(GALE_NOTICE,tmp,0);
	gale_free(tmp);

	timeout = time(NULL);
	tmp = gale_malloc(strlen(getenv("HOST")) + 30);
	sprintf(tmp,"%s.%d",getenv("HOST"),(int) getpid());
	category = id_category(user_id,"receipt",tmp);
	gale_free(tmp);
	client = gale_open(category);

	msg = new_message();

	cat1 = id_category(id,"dom","key");
	cat2 = id_category(id,"user","ping");
	colon = gale_text_from_latin1(":",1);
	msg->cat = new_gale_text(cat1.l + cat2.l + colon.l);
	gale_text_append(&msg->cat,cat1);
	gale_text_append(&msg->cat,colon);
	gale_text_append(&msg->cat,cat2);
	free_gale_text(cat1); free_gale_text(cat2); free_gale_text(colon);

	msg->data.p = gale_malloc(category.l + 256);
	tmp = gale_text_to_latin1(category);
	sprintf(msg->data.p,
	        "Receipt-To: %s\r\n"
	        "Time: %lu\r\n",
		tmp,timeout);
	gale_free(tmp);
	msg->data.l = strlen(msg->data.p);

	_msg = sign_message(user_id,msg);
	if (_msg) {
		release_message(msg);
		msg = _msg;
	}

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

		while (!status && (reply = link_get(client->link))) {
			status = process(id,domain,reply);
			release_message(reply);
		}
	}

	if (domain) free_auth_id(domain);
	release_message(msg);
	free_gale_text(category);
	gale_close(client);
	return status > 0;
}
