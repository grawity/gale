#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "gale/all.h"

struct gale_client *client;

struct gale_text *subs = NULL;
struct gale_message **pings = NULL;
int count_subs = 0,count_pings = 0;

const char *tty,*gwatchrc = "gwatchrc";
struct gale_text receipt;

int max_num = 0;
int so_far = 0;

enum { m_login, m_logout, m_ping };

void *gale_malloc(size_t size) { return malloc(size); }
void gale_free(void *ptr) { free(ptr); }

void bye(int x) {
	(void) x;
	exit(0);
}

void watch_cat(struct gale_text cat) {
	subs = gale_realloc(subs,sizeof(*subs) * (count_subs + 1));
	subs[count_subs++] = cat;
}

void watch_ping(struct gale_text cat,struct gale_id *id) {
	struct gale_message *msg;
	char *tmp;

	if (!receipt.p) {
		const char *host = getenv("HOST");
		char *tmp = gale_malloc(strlen(host) + 20);
		sprintf(tmp,"%s.%d",host,(int) getpid());
		receipt = id_category(user_id,
			_G("receipt"),
			gale_text_from_latin1(tmp,-1));
		gale_free(tmp);
		watch_cat(receipt);
	}

	msg = new_message();
	msg->cat = gale_text_dup(cat);
	msg->data.p = gale_malloc(30 + receipt.l);
	tmp = gale_text_to_latin1(receipt);
	sprintf(msg->data.p,"Receipt-To: %s\r\n\r\n",tmp);
	gale_free(tmp);
	msg->data.l = strlen(msg->data.p);

	if (id) {
		struct gale_message *new = encrypt_message(1,&id,msg);
		release_message(msg);
		msg = new;
	}

	if (msg) {
		pings = gale_realloc(pings,sizeof(*pings) * (count_pings + 1));
		pings[count_pings++] = msg;
	}
}

void watch_id(struct gale_id *id) {
	watch_ping(id_category(id,_G("user"),_G(":/ping")),id);
	watch_cat(id_category(id,_G("notice"),_G("")));
}

void watch_domain(struct gale_text id) {
	watch_cat(dom_category(id,_G("notice")));
}

void read_file(const char *fn) {
	FILE *fp;
	int num;
	const char *file;

	file = dir_search(fn,1,dot_gale,sys_dir,NULL);
	if (!file) {
		gale_alert(GALE_WARNING,fn,ENOENT);
		return;
	}

	fp = fopen(file,"r");
	if (!fp) {
		gale_alert(GALE_WARNING,file,errno);
		return;
	}

	do {
		char ch,var[40],value[256];
		while (fscanf(fp," #%*[^\n]%c",&ch) == 1) ;
		num = fscanf(fp,"%39s %255[^\n]",var,value);
		if (num != 2) continue;
		if (!strcmp(var,"category"))
			watch_cat(gale_text_from_local(value,-1));
		else if (!strcmp(var,"ping"))
			watch_ping(gale_text_from_local(value,-1),NULL);
		else if (!strcmp(var,"id"))
			watch_id(lookup_id(gale_text_from_local(value,-1)));
		else if (!strcmp(var,"domain"))
			watch_domain(gale_text_from_local(value,-1));
		else
			gale_alert(GALE_WARNING,var,EINVAL);
	} while (num == 2);

	fclose(fp);
}

void open_client(void) {
	struct gale_text spec,colon;
	int i,len = 0;

	colon = gale_text_from_latin1(":",-1);

	for (i = 0; i < count_subs; ++i) len += 1 + subs[i].l;
	spec = new_gale_text(len);

	gale_text_append(&spec,subs[0]);
	for (i = 1; i < count_subs; ++i) {
		gale_text_append(&spec,colon);
		gale_text_append(&spec,subs[i]);
	}

	client = gale_open(spec);
	free_gale_text(spec);
	free_gale_text(colon);
}

void send_pings(void) {
	int i;

	for (i = 0; i < count_pings; ++i) {
		link_put(client->link,pings[i]);
		release_message(pings[i]);
	}
}

void incoming(
	int type,
	struct gale_id *id,
	const char *agent,
	const char *from,
	int seq
)
{
	(void) seq; (void) agent;
	switch (type) {
	case m_login: printf("<login>"); break;
	case m_logout: printf("<logout>"); break;
	case m_ping: printf("<ping>"); break;
	}
	if (id) printf(" <%s>",gale_text_hack(auth_id_name(id)));
	if (from) printf(" (%s)",from);
	printf("\r\n");
	fflush(stdout);
}

void process_message(struct gale_message *msg) {
	int type;
	struct gale_text login,logout,debug;
	char *next,*key,*data,*end;
	struct gale_id *id_sign = NULL,*id_encrypt = NULL;
	char *agent = NULL,*from = NULL;
	int sequence = -1;

	login = gale_text_from_latin1("/login",-1);
	logout = gale_text_from_latin1("/logout",-1);
	debug = gale_text_from_latin1("debug/restart",-1);

	if (!gale_text_compare(gale_text_right(msg->cat,7),logout))
		type = m_logout;
	else if (!gale_text_compare(gale_text_right(msg->cat,6),login))
		type = m_login;
	else
		type = m_ping;

	if (type != m_logout && max_num != 0 && ++so_far == max_num) bye(0);

	id_encrypt = decrypt_message(msg,&msg);
	if (!msg) goto error;

	id_sign = verify_message(msg);

	next = msg->data.p;
	end = next + msg->data.l;
	while (parse_header(&next,&key,&data,end)) {
		if (!strcasecmp(key,"Agent")) agent = data;
		if (!strcasecmp(key,"From")) from = data;
		if (!strcasecmp(key,"Sequence")) sequence = atoi(data);
	}

#ifndef NDEBUG
	if (!gale_text_compare(msg->cat,debug) && id_sign 
	&&  !gale_text_compare(auth_id_name(id_sign),_G("egnor@ofb.net"))) {
		gale_alert(GALE_NOTICE,"Restarting from debug/restart.",0);
		gale_restart();
	}
#endif

	incoming(type,id_sign,agent,from,sequence);

error:
	free_gale_text(login);
	free_gale_text(logout);
	free_gale_text(debug);
	if (id_sign) free_id(id_sign);
	if (id_encrypt) free_id(id_encrypt);
	if (msg) release_message(msg);
}

void usage(void) {
	fprintf(stderr,
		"%s\n"
		"usage: gwatch [flags] cat\n"
		"flags: -n          Do not fork (default if -m, -t, or stdout redirected)\n"
		"       -k          Do not kill other gwatch processes\n"
		"       -K          Kill other gwatch processes and terminate\n"
		"       -i id       Watch user \"id\"\n"
		"       -d domain   Watch domain \"domain\"\n"
		"       -p cat      Send a \"ping\" to the given category\n"
		"       -m num      Exit after num responses received\n"
		"       -s count    Exit after count seconds pass\n"
		"       -w file     Use config file (default \"spylist\")\n"
		"       -f rc       Use script file (default \"gwatchrc\")\n"
		,GALE_BANNER);
	exit(1);
}

int main(int argc,char *argv[]) {
	int arg,do_fork = 0,do_kill = 0;
	struct sigaction act;

	if ((tty = ttyname(1))) {
		char *tmp = strrchr(tty,'/');
		if (tmp) tty = tmp + 1;
		do_fork = do_kill = 1;
	}

	gale_init("gwatch",argc,argv);
	receipt = null_text;

	sigaction(SIGALRM,NULL,&act);
	act.sa_handler = bye;
	sigaction(SIGALRM,&act,NULL);

	while ((arg = getopt(argc,argv,"hnkKi:d:p:m:s:w:f:")) != EOF) 
	switch (arg) {
	case 'n': do_fork = 0; break;
	case 'k': do_kill = 0; break;
	case 'K': if (tty) gale_kill(tty,1); return 0;
	case 'i': watch_id(lookup_id(gale_text_from_local(optarg,-1))); break;
	case 'd': watch_domain(gale_text_from_local(optarg,-1)); break;
	case 'p': watch_ping(gale_text_from_local(optarg,-1),NULL); break;
	case 'm': max_num = atoi(optarg); do_fork = 0; break;
	case 's': alarm(atoi(optarg)); do_fork = 0; break;
	case 'w': read_file(optarg);
	case 'f': gwatchrc = optarg;
	case 'h':
	case '?': usage();
	}

	if (optind != argc) {
		if (optind != argc - 1) usage();
		watch_cat(gale_text_from_local(argv[optind],-1));
	}

	if (count_subs == 0) read_file("spylist");
	if (count_subs == 0) {
		gale_alert(GALE_WARNING,"Nothing specified to watch.",0);
		usage();
	}

	open_client();

	if (do_fork) gale_daemon(1);
	if (tty) gale_kill(tty,do_kill);

	send_pings();
	for (;;) {
		while (!gale_send(client) && !gale_next(client)) {
			struct gale_message *msg;
			if (tty && !isatty(1)) return 0;
			if ((msg = link_get(client->link))) {
				process_message(msg);
				release_message(msg);
			}
		}
		gale_retry(client);
	}

	gale_close(client);
	return 0;
}
