#include "gale/all.h"

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

struct gale_text *subs = NULL;
struct gale_message **pings = NULL;
int count_subs = 0,count_pings = 0;

struct gale_text tty,gwatchrc,receipt;

int max_num = 0;
int so_far = 0;

void *on_alarm(oop_source *source,struct timeval tv,void *d) {
	return OOP_HALT;
}

void set_alarm(oop_source *source,int sec) {
	struct timeval tv;
	gettimeofday(&tv,NULL);
	tv.tv_sec += sec;
	source->on_time(source,tv,on_alarm,NULL);
}

void watch_cat(struct gale_text cat) {
	subs = gale_realloc(subs,sizeof(*subs) * (count_subs + 1));
	subs[count_subs++] = cat;
}

void watch_ping(struct gale_text cat,struct auth_id *id) {
	struct gale_message *msg = new_message();
	struct gale_fragment frag;

	if (!receipt.l) {
		const char *host = getenv("HOST");
		char *tmp = gale_malloc(strlen(host) + 20);
		sprintf(tmp,"%s.%d",host,(int) getpid());
		receipt = id_category(gale_user(),
			G_("receipt"),
			gale_text_from_latin1(tmp,-1));
		watch_cat(receipt);
	}

	msg->cat = gale_text_concat(2,cat,G_(":/ping"));
	
	frag.type = frag_text;
	frag.name = G_("question/receipt");
	frag.value.text = receipt;
	gale_group_add(&msg->data,frag);

	if (id) msg = encrypt_message(1,&id,msg);

	if (msg) {
		pings = gale_realloc(pings,sizeof(*pings) * (count_pings + 1));
		pings[count_pings++] = msg;
	}
}

void watch_id(struct auth_id *id) {
	watch_ping(id_category(id,G_("user"),G_(":/ping")),id);
	watch_cat(id_category(id,G_("notice"),G_("")));
}

void watch_domain(struct gale_text id) {
	watch_cat(dom_category(id,G_("notice")));
}

void read_file(struct gale_text fn) {
	FILE *fp;
	int num;
	struct gale_text file;

	file = dir_search(fn,1,
	                  gale_global->dot_gale,
	                  gale_global->sys_dir,
	                  null_text);
	if (!file.l) {
		gale_alert(GALE_WARNING,gale_text_to_local(fn),ENOENT);
		return;
	}

	fp = fopen(gale_text_to_local(file),"r");
	if (!fp) {
		gale_alert(GALE_WARNING,gale_text_to_local(file),errno);
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

void send_pings(struct gale_link *link) {
	int i;
	for (i = 0; i < count_pings; ++i) link_put(link,pings[i]);
}

void incoming(
	struct auth_id *id,
	struct gale_text status,
	struct gale_text from,
	struct gale_text class,
	struct gale_text instance,
	struct gale_time when
)
{
	struct timeval tv;
	time_t tt;
	char buf[80];
	gale_time_to(&tv,when);
	tt = tv.tv_sec;
	strftime(buf,sizeof(buf)," %Y-%m-%d %H:%M:%S ",localtime(&tt));

	gale_print(stdout,gale_print_bold | gale_print_clobber_left,G_("*"));
	gale_print(stdout,0,gale_text_from_local(buf,-1));
	gale_print(stdout,0,status);
	gale_print(stdout,0,G_(":"));

	if (id) {
		gale_print(stdout,0,G_(" <"));
		gale_print(stdout,gale_print_bold,auth_id_name(id));
		gale_print(stdout,0,G_(">"));
	}

	if (from.l) {
		gale_print(stdout,0,G_(" ("));
		gale_print(stdout,0,from);
		gale_print(stdout,0,G_(")"));
	}

	gale_print(stdout,gale_print_clobber_right,G_(""));
	gale_print(stdout,0,G_("\n"));
	fflush(stdout);
}

void *on_message(struct gale_link *link,struct gale_message *msg,void *d) {
	struct auth_id *id_sign = NULL,*id_encrypt = NULL;
	struct gale_text from = null_text,status = null_text;
	struct gale_text class = null_text,instance = null_text;
	struct gale_time when = gale_time_now();
	struct gale_group group;

	id_encrypt = decrypt_message(msg,&msg);
	if (!msg) return OOP_CONTINUE;

	id_sign = verify_message(msg,&msg);

#ifndef NDEBUG
	if (!gale_text_compare(msg->cat,G_("debug/restart")) && id_sign 
	&&  !gale_text_compare(auth_id_name(id_sign),G_("egnor@ofb.net"))) {
		gale_alert(GALE_NOTICE,"Restarting from debug/restart.",0);
		gale_restart();
	}
#endif

	group = msg->data;
	while (!gale_group_null(group)) {
		struct gale_fragment frag = gale_group_first(group);
		group = gale_group_rest(group);

		if (frag_text == frag.type
		&& !gale_text_compare(frag.name,G_("notice/presence")))
			status = frag.value.text;

		if (frag_text == frag.type
		&& !gale_text_compare(frag.name,G_("message/sender")))
			from = frag.value.text;

		if (frag_text == frag.type
		&& !gale_text_compare(frag.name,G_("id/class")))
			class = frag.value.text;

		if (frag_text == frag.type
		&& !gale_text_compare(frag.name,G_("id/instance")))
			instance = frag.value.text;

		if (frag_time == frag.type
		&& !gale_text_compare(frag.name,G_("id/time")))
			when = frag.value.time;
	}

	if (0 == status.l) {
		if (!gale_text_compare(gale_text_right(msg->cat,7),G_("/logout")))
			status = G_("out/stopped");
		else if (!gale_text_compare(gale_text_right(msg->cat,6),G_("/login")))
			status = G_("in/started");
		else
			status = G_("in/here");
	}

	incoming(id_sign,status,from,class,instance,when);
	if (max_num != 0 && ++so_far == max_num) return OOP_HALT;
	return OOP_CONTINUE;
}

void usage(void) {
	fprintf(stderr,
		"%s\n"
		"usage: gwatch [flags] [cat]\n"
		"flags: -h          Display this message\n"
		"       -n          Do not fork (default if -m, -s, or stdout redirected)\n"
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
	int i,arg,do_fork = 0,do_kill = 0;
	oop_source_sys *sys;
	oop_source *source;
	struct gale_link *link;
	struct gale_server *server;
	struct gale_text spec;

	gwatchrc = G_("spylist");
	tty = gale_text_from_local(ttyname(1),-1);
	if (tty.l) {
		struct gale_text full = tty,temp = null_text;
		while (gale_text_token(full,'/',&temp)) tty = temp;
		do_fork = do_kill = 1;
	}

	gale_init("gwatch",argc,argv);
	gale_init_signals(source = oop_sys_source(sys = oop_sys_new()));
	receipt = null_text;

	while ((arg = getopt(argc,argv,"hnkKi:d:p:m:s:w:f:")) != EOF) 
	switch (arg) {
	case 'n': do_fork = 0; break;
	case 'k': do_kill = 0; break;
	case 'K': if (tty.l) gale_kill(tty,1); return 0;
	case 'i': watch_id(lookup_id(gale_text_from_local(optarg,-1))); break;
	case 'd': watch_domain(gale_text_from_local(optarg,-1)); break;
	case 'p': watch_ping(gale_text_from_local(optarg,-1),NULL); break;
	case 'm': max_num = atoi(optarg); do_fork = 0; break;
	case 's': set_alarm(source,atoi(optarg)); do_fork = 0; break;
	case 'w': read_file(gale_text_from_local(optarg,-1)); break;
	case 'f': gwatchrc = gale_text_from_local(optarg,-1); break;
	case 'h':
	case '?': usage();
	}

	if (optind != argc) {
		if (optind != argc - 1) usage();
		watch_cat(gale_text_from_local(argv[optind],-1));
	}

	if (count_subs == 0) read_file(G_("spylist"));
	if (count_subs == 0) {
		gale_alert(GALE_WARNING,"Nothing specified to watch.",0);
		usage();
	}

	spec = subs[0];
	for (i = 1; i < count_subs; ++i)
		spec = gale_text_concat(3,spec,G_(":"),subs[i]);
	link = new_link(source);
	server = gale_open(source,link,spec,null_text,0);

	if (do_fork) gale_daemon(source,1);
	if (tty.l) {
		gale_kill(tty,do_kill);
		gale_watch_tty(source,1);
	}

	send_pings(link);
	link_on_message(link,on_message,NULL);
	oop_sys_run(sys);
	gale_close(server);
	return 0;
}
