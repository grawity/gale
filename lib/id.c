#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

#include "gale/all.h"

#define TIMEOUT 20 /* seconds */

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

char *id_category(struct gale_id *id,const char *pfx,const char *sfx) {
	const char *name = auth_id_name(id);
	char *tmp = gale_malloc(strlen(name) + strlen(pfx) + strlen(sfx) + 3);
	const char *at = strchr(name,'@');

	if (at)
		sprintf(tmp,"%s/%s/%.*s/%s",pfx,at+1,at - name,name,sfx);
	else
		sprintf(tmp,"%s/%s/%s",pfx,name,sfx);

	return tmp;
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
			next = msg->data;
			end = next + msg->data_size;
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
	char *tmp,*tmp2,*category;
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

	tmp = id_category(id,"dom","key");
	tmp2 = id_category(id,"user","ping");
	msg->category = gale_malloc(strlen(tmp) + strlen(tmp2) + 2);
	sprintf(msg->category,"%s:%s",tmp,tmp2);
	gale_free(tmp); gale_free(tmp2);

	msg->data = gale_malloc(strlen(category) + 256);
	sprintf(msg->data,
	        "Receipt-To: %s\r\n"
	        "Time: %lu\r\n",
	        category,timeout);
	msg->data_size = strlen(msg->data);

	_msg = sign_message(user_id,msg);
	if (_msg) {
		release_message(msg);
		msg = _msg;
	}

	timeout += TIMEOUT;
	link_put(client->link,msg);
	while (gale_send(client) && time(NULL) < timeout) {
		gale_retry(client);
		if (!link_queue(client->link)) link_put(client->link,msg);
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
	gale_free(category);
	gale_close(client);
	return status > 0;
}
