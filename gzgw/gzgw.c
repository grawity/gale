#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>

#include <zephyr/zephyr.h>
#include <com_err.h>

#include "gale/all.h"

void *gale_malloc(size_t size) { return malloc(size); }
void gale_free(void *ptr) { return free(ptr); }

ZSubscription_t sub;

u_short port;
struct gale_client *client;
const char *myname,*category;

char *buf = NULL;
int buf_len = 0,buf_alloc = 0;

void cleanup(void) {
	ZCancelSubscriptions(port);
}

void reset(void) {
	buf_len = 0;
}

void append(int len,const char *s) {
	if (len < 0) len = strlen(s);
	if (buf_len + len >= buf_alloc) {
		char *tmp = buf;
		buf_alloc = (buf_len + len) * 2;
		buf = gale_malloc(buf_alloc);
		memcpy(buf,tmp,buf_len);
		gale_free(tmp);
	}
	memcpy(buf + buf_len,s,len);
	buf_len += len;
}

void to_g(ZNotice_t *notice) {
	struct gale_message *msg;
	int len;
	char *ptr,*end,num[15];

	if (notice->z_opcode && !strcmp(notice->z_opcode,"gale")) {
		gale_dprintf(2,"message has opcode gale; dropping.\n");
		return;
	}

	if (notice->z_recipient && notice->z_recipient[0]) {
		gale_dprintf(2,"message has recipient \"%s\"; dropping.\n",
		             notice->z_recipient);
		return;
	}

	for (ptr = notice->z_class_inst; *ptr; ++ptr)
		*ptr = tolower(*ptr);

	msg = new_message();
	msg->category = gale_malloc(10 + 
		strlen(category) +
		strlen(notice->z_class_inst));
	sprintf(msg->category,"%s%s",category,notice->z_class_inst);

	reset();
	append(-1,"Content-type: text/x-zwgc\r\nTime: ");
	sprintf(num,"%u",(unsigned int) notice->z_time.tv_sec);
	append(-1,num);

	append(8,"\r\nFrom: ");

	ptr = memchr(notice->z_message,'\0',notice->z_message_len);
	if (ptr) {
		if (notice->z_message[0]) {
			append(ptr - notice->z_message,notice->z_message);
			append(1," ");
		}
	}

	append(1,"<");
	append(-1,notice->z_sender);
	append(5,">\r\n\r\n");

	if (ptr) {
		char *zero;
		++ptr;
		len = notice->z_message_len - (ptr - notice->z_message);
		zero = memchr(ptr,'\0',len);
		if (zero) len = zero - ptr;
		while (len > 0 && *ptr) {
			end = memchr(ptr,'\n',len);
			if (!end) end = ptr + len;
			append(end - ptr,ptr);
			append(2,"\r\n");
			len -= (end - ptr) + 1;
			ptr = end + 1;
		}
	}

	msg->data_size = buf_len;
	msg->data = gale_malloc(msg->data_size);
	memcpy(msg->data,buf,msg->data_size);
	link_put(client->link,msg);
	release_message(msg);
}

void z_to_g(void) {
	ZNotice_t notice;
	int retval;

	while (ZPending() > 0) {
		if ((retval = ZReceiveNotice(&notice,NULL)) != ZERR_NONE) {
			com_err(myname,retval,"while receiving notice");
			return;
		}
		to_g(&notice);
		ZFreeNotice(&notice);
	}
}

void to_z(struct gale_message *_msg) {
	ZNotice_t notice;
	char instance[256] = "(invalid)";
	char *key,*data,*next,*end;
	const char *sig;
	struct gale_message *msg;
	struct auth_id *signature,*encryption;
	int retval,len;

	encryption = decrypt_message(_msg,&msg);
	if (!msg) return;
	signature = verify_message(msg);

	memset(&notice,0,sizeof(notice));
	notice.z_kind = UNACKED;
	notice.z_port = 0;
	notice.z_class = sub.zsub_class;
	notice.z_class_inst = instance;
	notice.z_opcode = "gale";
	notice.z_default_format = "";
	notice.z_sender = signature ? (char*) auth_id_name(signature) : "gale";
	notice.z_recipient = "";

	next = msg->category;
	len = strlen(category);
	while ((next = strstr(next,category))) {
		if (next == msg->category || next[-1] == ':') {
			const char *colon = strchr(next + len,':');
			int size = sizeof(instance) - 1;
			if (colon != NULL && colon - next + len < size)
				size = colon - next + len;
			strncpy(instance,next + len,size);
			break;
		}
		next += len;
	}

	sig = NULL;
	end = msg->data + msg->data_size;
	next = msg->data;
	while (parse_header(&next,&key,&data,end)) {
		if (!strcasecmp(key,"From"))
			sig = data;
	}

	if (sig == NULL && signature) sig = auth_id_comment(signature);
	if (sig == NULL) sig = "Gale User";

	reset();
	append(strlen(sig) + 1,sig); 

	while (next != end) {
		key = memchr(next,'\r',end - next);
		if (!key) key = end;
		append(key - next,next);
		next = key;
		if (next != end) {
			++next;
			if (*next == '\n') ++next;
			append(1,"\n");
		}
	}
	append(1,"");

	notice.z_message = buf;
	notice.z_message_len = buf_len;

	if ((retval = ZSendNotice(&notice,ZAUTH)) != ZERR_NONE &&
	    (retval = ZSendNotice(&notice,ZNOAUTH)) != ZERR_NONE)
		com_err(myname,retval,"while sending notice");

	if (signature) free_auth_id(signature);
	if (encryption) free_auth_id(encryption);
	release_message(msg);
}

void g_to_z(void) {
	struct gale_message *msg;

	while ((msg = link_get(client->link))) {
		gale_dprintf(2,"received gale message\n");
		to_z(msg);
		release_message(msg);
	}
}

void usage(void) {
	fprintf(stderr,
	"%s\n"
	"usage: gzgw [class [cat]]\n"
	"gzgw defaults to class message and category 'zephyr/$GALE_DOMAIN/'.\n"
	,GALE_BANNER);
	exit(1);
}

int main(int argc,char *argv[]) {
	int retval,opt;
	fd_set fds;

	gale_init("gzgw",argc,argv);

	openlog(argv[0],LOG_PID,LOG_DAEMON);

	myname = argv[0];

	while (EOF != (opt = getopt(argc,argv,"hdD"))) switch (opt) {
	case 'd': ++gale_debug; break;
	case 'D': gale_debug += 5; break;
	case 'h':
	case '?': usage();
	}

	sub.zsub_classinst = "*";
	sub.zsub_recipient = "*";

	if (optind < argc) {
		sub.zsub_class = argv[optind];
		++optind;
	} else
		sub.zsub_class = "message";

	if (optind < argc) {
		category = argv[optind];
		++optind;
	} else {
		const char *domain = getenv("GALE_DOMAIN");
		char *tmp = gale_malloc(strlen(domain) + 30);
		sprintf(tmp,"zephyr/%s/",domain);
		category = tmp;
	}
	if (optind < argc) usage();

	gale_dprintf(2,"subscribing to gale: \"%s\"\n",category);
	client = gale_open(category);

	gale_dprintf(2,"subscribing to Zephyr\n");
	if (gale_debug > 3) {
		gale_dprintf(3,"... triple %s,%s,%s\n",
		        sub.zsub_class,sub.zsub_classinst,sub.zsub_recipient);
	}

	if ((retval = ZInitialize()) != ZERR_NONE) {
		com_err(myname,retval,"while initializing");
		exit(1);
	}

	port = 0;
	if ((retval = ZOpenPort(&port)) != ZERR_NONE) {
		com_err(myname,retval,"while opening port");
		exit(1);
	}

	retval = ZSubscribeToSansDefaults(&sub,1,port);
	if (retval != ZERR_NONE) {
		com_err(myname,retval,"while subscribing");
		exit(1);
	}

	gale_dprintf(1,"starting\n");
	gale_daemon(0);

	gale_cleanup(cleanup);

	for (;;) {
		while (gale_send(client)) gale_retry(client);

		FD_ZERO(&fds);
		FD_SET(client->socket,&fds);
		FD_SET(ZGetFD(),&fds);

		gale_dprintf(2,"waiting for incoming messages ...\n");
		retval = select(FD_SETSIZE,
		                (SELECT_ARG_2_T) &fds,NULL,NULL,NULL);
		if (retval < 0) {
			com_err(myname,retval,"in select()");
			exit(1);
		}

		if (FD_ISSET(client->socket,&fds))
			if (gale_next(client)) gale_retry(client);
		g_to_z();
		z_to_g();
	}
}
