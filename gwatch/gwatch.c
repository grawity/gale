#include "gale/all.h"

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

struct gale_client *client;

struct gale_text *subs = NULL;
struct gale_message **pings = NULL;
int count_subs = 0,count_pings = 0;

const char *tty,*gwatchrc = "gwatchrc";
struct gale_text receipt;

int max_num = 0;
int so_far = 0;

void bye(int x) {
	(void) x;
	exit(0);
}

void watch_cat(struct gale_text cat) {
	subs = gale_realloc(subs,sizeof(*subs) * (count_subs + 1));
	subs[count_subs++] = cat;
}

void watch_ping(struct gale_text cat,struct auth_id *id) {
	struct gale_message *msg = new_message();
	struct gale_fragment *frags[2];

	if (!receipt.l) {
		const char *host = getenv("HOST");
		char *tmp = gale_malloc(strlen(host) + 20);
		sprintf(tmp,"%s.%d",host,(int) getpid());
		receipt = id_category(gale_user(),
			G_("receipt"),
			gale_text_from_latin1(tmp,-1));
		watch_cat(receipt);
	}

	gale_create(frags[0]);
	frags[0]->type = frag_text;
	frags[0]->name = G_("question/receipt");
	frags[0]->value.text = receipt;

	frags[1] = NULL;

	msg->cat = cat;
	msg->data = pack_message(frags);

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
	struct gale_text spec = subs[0];
	int i;
	for (i = 1; i < count_subs; ++i)
		spec = gale_text_concat(3,spec,G_(":"),subs[i]);
	client = gale_open(spec);
}

void send_pings(void) {
	int i;
	for (i = 0; i < count_pings; ++i) link_put(client->link,pings[i]);
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
	printf("* %s:",gale_text_to_local(status));

	if (id) {
		fputs(" <",stdout);
		gale_tmode("md");
		fputs(gale_text_to_local(auth_id_name(id)),stdout);
		gale_tmode("me");
		fputs(">",stdout);
	}

	if (from.l) 
		printf(" (%s)",gale_text_to_local(from));

	{
		struct timeval tv;
		time_t tt;
		char buf[80];
		gale_time_to(&tv,when);
		tt = tv.tv_sec;
		strftime(buf,sizeof(buf)," %m/%d %H:%M",localtime(&tt));
		fputs(buf,stdout);
	}

	printf("\r\n");
	fflush(stdout);
}

void process_message(struct gale_message *msg) {
	struct auth_id *id_sign = NULL,*id_encrypt = NULL;
	struct gale_text from = null_text,status = null_text;
	struct gale_text class = null_text,instance = null_text;
	struct gale_time when = gale_time_now();
	struct gale_fragment **frags;

	if (max_num != 0 && ++so_far == max_num) bye(0);

	id_encrypt = decrypt_message(msg,&msg);
	if (!msg) return;

	id_sign = verify_message(msg,&msg);

#ifndef NDEBUG
	if (!gale_text_compare(msg->cat,G_("debug/restart")) && id_sign 
	&&  !gale_text_compare(auth_id_name(id_sign),G_("egnor@ofb.net"))) {
		gale_alert(GALE_NOTICE,"Restarting from debug/restart.",0);
		gale_restart();
	}
#endif

	for (frags = unpack_message(msg->data); *frags; ++frags) {
		struct gale_fragment *frag = *frags;

		if (frag_text == frag->type
		&& !gale_text_compare(frag->name,G_("notice/presence")))
			status = frag->value.text;

		if (frag_text == frag->type
		&& !gale_text_compare(frag->name,G_("message/sender")))
			from = frag->value.text;

		if (frag_text == frag->type
		&& !gale_text_compare(frag->name,G_("id/class")))
			class = frag->value.text;

		if (frag_text == frag->type
		&& !gale_text_compare(frag->name,G_("id/instance")))
			instance = frag->value.text;

		if (frag_time == frag->type
		&& !gale_text_compare(frag->name,G_("id/time")))
			when = frag->value.time;
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
			while ((msg = link_get(client->link)))
				process_message(msg);
		}
		gale_retry(client);
	}

	gale_close(client);
	return 0;
}
