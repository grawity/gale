#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include <zephyr/zephyr.h>
#include <com_err.h>

#include "gale/all.h"

ZSubscription_t default_sub;
ZSubscription_t *subs = &default_sub;
int num_subs = 1,sub_alloc = 0;

void *gale_malloc(int size) { return malloc(size); }
void gale_free(void *ptr) { return free(ptr); }

u_short port;
struct gale_client *client;
const char *myname,*category = "zephyr";

char *buf = NULL;
int buf_len = 0,buf_alloc = 0;

void cleanup(void) {
	ZCancelSubscriptions(port);
}

void sig(int x) {
	cleanup();
	exit(1);
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

void do_retry(void) {
	do {
		gale_retry(client);
		link_subscribe(client->link,category);
	} while (gale_send(client));
}

void to_g(ZNotice_t *notice) {
	struct gale_message *msg;
	int len;
	char *ptr,*end,num[15];

	ptr = strrchr(notice->z_sender,'@');
	if (ptr && !strcasecmp(ptr,"@gale")) return;

	msg = new_message();
	if (notice->z_opcode && notice->z_opcode[0]) {
		msg->category = gale_malloc(10 + strlen(category) +
			strlen(notice->z_class) + 
			strlen(notice->z_class_inst) + 
			strlen(notice->z_opcode));
		sprintf(msg->category,"%s/%s/%s/%s",category,
			notice->z_class,notice->z_class_inst,notice->z_opcode);
	} else {
		msg->category = gale_malloc(10 + strlen(category) +
			strlen(notice->z_class) + 
			strlen(notice->z_class_inst));
		sprintf(msg->category,"%s/%s/%s",category,
			notice->z_class,notice->z_class_inst);
	}

	reset();
	append(-1,"Content-type: text/x-zwgc\r\nTime: ");
	sprintf(num,"%u",(unsigned int) notice->z_time.tv_sec);
	append(-1,num);
	if (notice->z_recipient && notice->z_recipient[0]) {
		append(6,"\r\nTo: ");
		append(-1,notice->z_recipient);
	}
	append(8,"\r\nFrom: ");
	if (ptr)
		append(ptr - notice->z_sender,notice->z_sender);
	else
		append(-1,notice->z_sender);
	append(7,"@zephyr");
	
	ptr = memchr(notice->z_message,'\0',notice->z_message_len);
	if (ptr) {
		if (notice->z_message[0]) {
			append(2," (");
			append(ptr - notice->z_message,notice->z_message);
			append(1,")");
		}
		append(4,"\r\n\r\n");
		++ptr;
		len = notice->z_message_len - (ptr - notice->z_message);
		while (len > 0 && *ptr) {
			end = memchr(ptr,'\n',len);
			append(end - ptr,ptr);
			append(2,"\r\n");
			len -= (end - ptr) + 1;
			ptr = end + 1;
		}
	} else
		append(4,"\r\n\r\n");

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

void to_z(struct gale_message *msg) {
	ZNotice_t notice;
	char class[32],instance[256] = "(invalid)",opcode[64] = "";
	char sender[128] = "unknown@gale",sig[256] = "";
	char *key,*data,*next,*end;
	int retval;

	strncpy(class,subs[0].zsub_class,sizeof(class));

	memset(&notice,0,sizeof(notice));
	notice.z_kind = UNACKED;
	notice.z_port = 0;
	notice.z_class = class;
	notice.z_class_inst = instance;
	notice.z_opcode = opcode;
	notice.z_default_format = "";
	notice.z_sender = sender;
	notice.z_recipient = "";

	sscanf(msg->category,"%*[^/]/%31[^/]/%255[^/]/%64[^/]",
	       class,instance,opcode);

	end = msg->data + msg->data_size;
	next = msg->data;
	while (parse_header(&next,&key,&data,end)) {
		if (!strcasecmp(key,"To")) 
			notice.z_recipient = data;
		else if (!strcasecmp(key,"From") && 0 <
			sscanf(data,"%100s (%255[^)])",sender,sig))
			strcat(sender,"@gale");
		else if (!strcasecmp(key,"Encryption")) 
			return;
	}

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
	"usage: gzgw [-n] [-c class[:class...]] [[cat]@[server]]\n"
	"flags: -c       Specify Zephyr class(es) to watch\n"
	"defaults: class MESSAGE and category 'zephyr'.\n");
	exit(1);
}

void add_class(char *s) {
	ZSubscription_t *tmp = subs;
	if (subs == &default_sub) num_subs = 0;
	if (num_subs == sub_alloc) sub_alloc = sub_alloc ? sub_alloc * 2 : 10;
	subs = gale_malloc(sizeof(*subs) * sub_alloc);
	memcpy(subs,tmp,num_subs * sizeof(*subs));
	if (tmp != &default_sub) gale_free(tmp);
	subs[num_subs].zsub_class = s;
	subs[num_subs].zsub_classinst = "*";
	subs[num_subs].zsub_recipient = "*";
	++num_subs;
}

void copt(char *s) {
	s = strtok(s,":");
	while (s) {
		add_class(s);
		s = strtok(NULL,":");
	}
}

int main(int argc,char *argv[]) {
	char *server = NULL;
	int retval,opt;
	fd_set fds;

	subs[0].zsub_class = "MESSAGE";
	subs[0].zsub_classinst = "*";
	subs[0].zsub_recipient = "*";

	myname = argv[0];

	while (EOF != (opt = getopt(argc,argv,"dDc:"))) switch (opt) {
	case 'c': copt(optarg); break;
	case 'd': ++gale_debug; break;
	case 'D': gale_debug += 5; break;
	case '?': usage();
	}

	if (optind < argc - 1) usage();

	if (optind == argc - 1) {
		if ((server = strrchr(argv[optind],'@'))) *server++ = '\0';
		if (argv[optind][0]) category = argv[optind];
	}

	gale_dprintf(2,"subscribing to gale: \"%s\"\n",category);
	if (!(client = gale_open(server,32,262144))) {
		fprintf(stderr,"could not contact gale server\n");
		exit(1);
	}

	link_subscribe(client->link,category);

	gale_dprintf(2,"subscribing to Zephyr\n");
	if (gale_debug > 3) {
		int i;
		for (i = 0; i < num_subs; ++i)
			gale_dprintf(3,"... triple %s,%s,%s\n",
			        subs[i].zsub_class,subs[i].zsub_classinst,
			        subs[i].zsub_recipient);
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

	retval = ZSubscribeToSansDefaults(subs,num_subs,port);
	if (retval != ZERR_NONE) {
		com_err(myname,retval,"while subscribing");
		exit(1);
	}

	gale_dprintf(1,"starting\n");
	gale_daemon();

	signal(SIGINT,sig);
	signal(SIGTERM,sig);
	signal(SIGHUP,sig);
	atexit(cleanup);

	for (;;) {
		while (gale_send(client)) do_retry();

		FD_ZERO(&fds);
		FD_SET(client->socket,&fds);
		FD_SET(ZGetFD(),&fds);

		gale_dprintf(2,"waiting for incoming messages ...\n");
		retval = select(FD_SETSIZE,HPINT &fds,NULL,NULL,NULL);
		if (retval < 0) {
			com_err(myname,retval,"in select()");
			exit(1);
		}

		if (FD_ISSET(client->socket,&fds))
			if (gale_next(client)) do_retry();
		g_to_z();
		z_to_g();
	}
}
