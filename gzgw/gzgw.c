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
struct gale_link *conn;
const char *myname;
struct gale_text cat;

char *buf = NULL;
int buf_len = 0,buf_alloc = 0;

void cleanup(void *d) {
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
	struct old_gale_message *msg;
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
	link_put(conn,msg);
}

void *z_to_g(oop_source *source,int sock,oop_event event,void *d) {
	ZNotice_t notice;
	int retval;

	while (ZPending() > 0) {
		if ((retval = ZReceiveNotice(&notice,NULL)) != ZERR_NONE) {
			com_err(myname,retval,"while receiving notice");
			return OOP_CONTINUE;
		}
		to_g(&notice);
		ZFreeNotice(&notice);
	}

	return OOP_CONTINUE;
}

void *to_z(struct gale_link *conn,struct old_gale_message *msg,void *data) {
	ZNotice_t notice;
	char instance[256] = "(invalid)";
	char *sig = NULL,*body = NULL;
	struct auth_id *signature;
	struct gale_group group;
	struct gale_text token = null_text;
	int retval;

	(void) auth_decrypt(&msg->data);
	if (!msg) return OOP_CONTINUE;
	signature = auth_verify(&msg->data);

	memset(&notice,0,sizeof(notice));
	notice.z_kind = UNACKED;
	notice.z_port = 0;
	notice.z_class = sub.zsub_class;
	notice.z_class_inst = instance;
	notice.z_opcode = "gale";
	notice.z_default_format = "Class $class, Instance $instance:\nTo: @bold($recipient) at $time $date\nFrom: @bold($1) <$sender>\n\n$2";
	notice.z_recipient = "";
	if (signature)
		notice.z_sender = gale_text_to_latin1(auth_id_name(signature));
	else
		notice.z_sender = gale_text_to_latin1(G_("gale"));

	while (gale_text_token(msg->cat,':',&token)) {
		if (!gale_text_compare(cat,gale_text_left(token,cat.l))) {
			strncpy(instance,
				gale_text_to_local(gale_text_right(token,-cat.l)),
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
			sig = gale_text_to_latin1(G_("Gale User"));
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

	return OOP_CONTINUE;
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
	oop_source_sys *sys;
	oop_source *source;
	int retval,opt;

	gale_init("gzgw",argc,argv);
	gale_init_signals(source = oop_sys_source(sys = oop_sys_new()));

	openlog(argv[0],LOG_PID,LOG_DAEMON);

	myname = argv[0];

	while (EOF != (opt = getopt(argc,argv,"hdD"))) switch (opt) {
	case 'd': ++gale_global->debug_level; break;
	case 'D': gale_global->debug_level += 5; break;
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
	conn = new_link(source);
	(void) gale_open(source,conn,cat,null_text,0);

	gale_dprintf(2,"subscribing to Zephyr\n");
	gale_dprintf(3,"... triple %s,%s,%s\n",
		        sub.zsub_class,sub.zsub_classinst,sub.zsub_recipient);

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
	gale_daemon(source);
	gale_kill(gale_text_from_local(sub.zsub_class,-1),1);
	gale_detach();

	gale_cleanup(cleanup,NULL);
	link_on_message(conn,to_z,NULL);
	source->on_fd(source,ZGetFD(),OOP_READ,z_to_g,NULL);

	oop_sys_run(sys);
	return 0;
}
