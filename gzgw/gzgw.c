#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>

#include <zephyr/zephyr.h>
#include <com_err.h>

#include "gale/all.h"

ZSubscription_t sub;

u_short port;
struct gale_client *client;
const char *myname;
struct gale_text cat;

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
	char *ptr;
	struct gale_fragment frag;

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

	gale_add_id(&msg->data,null_text);

	frag.name = G_("message/sender");
	frag.type = frag_text;
	frag.value.text = gale_text_from_latin1(notice->z_sender,-1);
	gale_group_add(&msg->data,frag);

	frag.name = G_("message/body");
	frag.type = frag_text;
	frag.value.text = null_text;

	if ((ptr = memchr(notice->z_message,'\0',notice->z_message_len))) {
		int len = notice->z_message_len - (++ptr - notice->z_message);
		char *zero = memchr(ptr,'\0',len);
		if (zero) len = zero - ptr;
		while (len > 0 && *ptr) {
			char *end = memchr(ptr,'\n',len);
			if (!end) end = ptr + len;

			frag.value.text = gale_text_concat(3,
				frag.value.text,
				gale_text_from_latin1(ptr,end - ptr),
				G_("\r\n"));

			len -= (end - ptr) + 1;
			ptr = end + 1;
		}
	}

	gale_group_add(&msg->data,frag);

	msg->cat = gale_text_concat(2,cat,
		gale_text_from_latin1(notice->z_class_inst,-1));
	link_put(client->link,msg);
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

void to_z(struct gale_message *msg) {
	ZNotice_t notice;
	char instance[256] = "(invalid)";
	char *sig = NULL,*body = NULL;
	struct auth_id *signature,*encryption;
	struct gale_group group;
	struct gale_text token = null_text;
	int retval;

	encryption = decrypt_message(msg,&msg);
	if (!msg) return;
	signature = verify_message(msg,&msg);

	memset(&notice,0,sizeof(notice));
	notice.z_kind = UNACKED;
	notice.z_port = 0;
	notice.z_class = sub.zsub_class;
	notice.z_class_inst = instance;
	notice.z_opcode = "gale";
	notice.z_default_format = "";
	notice.z_recipient = "";
	if (signature)
		notice.z_sender = gale_text_to_latin1(auth_id_name(signature));
	else
		notice.z_sender = gale_strdup("gale");

	while (gale_text_token(msg->cat,':',&token)) {
		if (!gale_text_compare(cat,gale_text_left(token,cat.l))) {
			strncpy(instance,
				gale_text_to_local(gale_text_right(token,cat.l)),
				255);
			break;
		}
	}

	group = gale_group_find(msg->data,G_("message/sender"));
	if (!gale_group_null(group)) {
		struct gale_fragment frag = gale_group_first(group);
		if (frag_text == frag.type) sig = gale_text_to_latin1(frag.value.text);
	}

	group = gale_group_find(msg->data,G_("message/body"));
	if (!gale_group_null(group)) {
		struct gale_fragment frag = gale_group_first(group);
		if (frag_text == frag.type) body = gale_text_to_latin1(frag.value.text);
	}

	if (sig == NULL) {
		if (signature) 
			sig = gale_text_to_latin1(auth_id_comment(signature));
		else
			sig = gale_strdup("Gale User");
	}

	reset();
	append(strlen(sig) + 1,sig); 

	while (body && *body) {
		char *nl = strchr(body,'\r');
		if (!nl) nl = body + strlen(body);
		append(nl - body,body);
		body = nl;
		if (*body) {
			++body;
			if (*body == '\n') ++body;
			append(1,"\n");
		}
	}
	append(1,"");

	notice.z_message = buf;
	notice.z_message_len = buf_len;

	if ((retval = ZSendNotice(&notice,ZAUTH)) != ZERR_NONE &&
	    (retval = ZSendNotice(&notice,ZNOAUTH)) != ZERR_NONE)
		com_err(myname,retval,"while sending notice");
}

void g_to_z(void) {
	struct gale_message *msg;

	while ((msg = link_get(client->link))) {
		gale_dprintf(2,"received gale message\n");
		to_z(msg);
	}
}

void usage(void) {
	fprintf(stderr,
	"%s\n"
	"usage: gzgw [-h] [class [cat]]\n"
	"flags: -h          Display this message\n"
	"gzgw defaults to class message and category 'gate/zephyr/$GALE_DOMAIN/'.\n"
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
		cat = gale_text_from_local(argv[optind],-1);
		++optind;
	} else {
		cat = gale_text_concat(3,
			G_("gate/zephyr/"),gale_var(G_("GALE_DOMAIN")),G_("/"));
	}
	if (optind < argc) usage();

	gale_dprintf(2,"subscribing to gale: \"%s\"\n",gale_text_to_local(cat));
	client = gale_open(cat);

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
