/* gsend.c -- simple client for sending messages */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pwd.h>

#include "gale/all.h"

struct gale_message *msg;               /* The message we're building. */
struct gale_location *user = NULL;	/* The local user. */
oop_source *oop;			/* Event source. */
int lookup_count = 1;			/* Outstanding lookup requests. */
int do_rrcpt = 0,do_identify = 1;       /* Various flags. */
int from_count = 0,to_count = 0;

/* Print a list of names. */
static void print_list(struct gale_location **list) {
	int i;
	for (i = 0; NULL != list && NULL != list[i]; ++i) {
		int at;
		struct gale_text name = gale_location_name(list[i]);
		gale_print(stdout,0,G_(" "));
		for (at = 0; at < name.l && '@' != name.p[at]; ++at) ;
		gale_print(stdout,gale_print_bold,gale_text_left(name,at));
		gale_print(stdout,0,gale_text_right(name,-at));
	}
}

/* Add default fragments to the message, if not already specified. */
void headers(void) {
	struct gale_fragment frag;
	char *tty;

	frag.name = G_("message/sender");
	frag.type = frag_text;
	frag.value.text = gale_var(G_("GALE_NAME"));
	if (0 != frag.value.text.l) gale_group_add(&msg->data,frag);

	tty = ttyname(0);
	if (tty && strchr(tty,'/')) tty = strrchr(tty,'/') + 1;
	gale_add_id(&msg->data,gale_text_from(gale_global->enc_filesys,tty,-1));

	if (do_rrcpt) {
		frag.name = G_("question.receipt");
		frag.type = frag_text;
		frag.value.text = gale_location_name(user);
		gale_group_add(&msg->data,frag);
	}
}

static void collapse(struct gale_location **from,int count) {
	struct gale_location **to = from;
	if (NULL == from) return;
	while (0 != count--)
		if (NULL == *from)
			++from;
		else
			*to++ = *from++;

	*to = NULL;
}

/* Terminate the event loop when the message is sent. */
static void *on_empty(struct gale_link *link,void *user) {
	if (isatty(0)) gale_print(stdout,0,G_("Message transmitted.\n"));
	return OOP_HALT;
}

static void *on_pack(struct gale_packet *pack,void *user) {
	struct gale_link *link;			/* Physical connection */
	struct gale_server *server;		/* Logical link */

	/* Open a connection to the server; don't subscribe to anything. */
	link = new_link(oop);
	server = gale_make_server(oop,link,null_text,0);

	link_put(link,pack);
	link_on_empty(link,on_empty,NULL);
	link_shutdown(link);
	return OOP_CONTINUE;
}

/* Get ready to send the message */
static void prepare_message() {
	struct gale_text_accumulator body;
	struct gale_text line;
	struct gale_fragment frag;
	const int ttyin = isatty(0);	  		/* Input options */
	const int from_specified = (0 != from_count);

	if (!from_specified && do_identify) {
		/* Sign with our key by default. */
		if (NULL == user) gale_alert(GALE_ERROR,G_("Who are you?"),0);
		msg->from[from_count++] = user;
		collapse(msg->from,from_count);
	}

	collapse(msg->from,from_count);
	collapse(msg->to,to_count);

	if (NULL == msg->to[0])
		gale_alert(GALE_ERROR,G_("No valid recipients."),0);

	/* If stdin is a TTY, prompt the user. */
	if (ttyin) {
		struct gale_group tail;

		if (from_specified || !do_identify) {
			gale_print(stdout,0,G_("From:"));
			if (NULL == msg->from || NULL == msg->from[0]) {
				gale_print(stdout,0,G_(" *"));
				gale_print(stdout,gale_print_bold,G_("anonymous"));
				gale_print(stdout,0,G_("*"));
			} else
				print_list(msg->from);
			gale_print(stdout,0,G_("\n"));
		}

		gale_print(stdout,0,G_("To:"));
		print_list(msg->to);
		gale_print(stdout,0,G_("\n"));

		tail = gale_group_find(
			msg->data,
			G_("message.keyword"),frag_text);

		if (!gale_group_null(tail)) {
			gale_print(stdout,0,G_("Keywords:"));
			do {
				const struct gale_fragment frag = 
					gale_group_first(tail);
				gale_print(stdout,0,G_(" /"));
				gale_print(stdout,gale_print_bold,frag.value.text);
				tail = gale_group_find(
					gale_group_rest(tail),
					G_("message.keyword"),frag_text);
			} while (!gale_group_null(tail));
			gale_print(stdout,0,G_("\n"));
		}

		gale_print(stdout,0,G_("(End your message with EOF or a solitary dot.)\n"));
	}

	/* Get the message. */
	body = null_accumulator;
	while ((line = gale_read_line(stdin)).l > 0) {
		if (!gale_text_compare(gale_text_right(line,1),G_("\n")))
			line = gale_text_left(line,-1);
		if (!gale_text_compare(gale_text_right(line,1),G_("\r")))
			line = gale_text_left(line,-1);

		/* Check for a solitary dot if input comes from a TTY. */
		if (ttyin && !gale_text_compare(line,G_("."))) break;

		gale_text_accumulate(&body,line);
		gale_text_accumulate(&body,G_("\r\n"));
	}

	frag.name = G_("message/body");
	frag.type = frag_text;
	frag.value.text = gale_text_collect(&body);
	gale_group_add(&msg->data,frag);

	if (do_identify) {
		/* Add the default fragments to the message. */
		headers();
	}

	/* Add the message to the outgoing queue. */
	gale_pack_message(oop,msg,on_pack,NULL);
}

/* Store a location address when lookup is complete. */
static void *on_location(struct gale_text n,struct gale_location *loc,void *x) {
	struct gale_location **ptr = (struct gale_location **) x;
	if (NULL != loc)
		*ptr = loc;
	else {
		*ptr = NULL;
		gale_alert(GALE_WARNING,gale_text_concat(3,
			G_("cannot find \""),n,G_("\"")),0);
	}

	if (0 == --lookup_count) prepare_message();
	return OOP_CONTINUE;
}

/* Start looking up a location. */
static void find_location(struct gale_text name,struct gale_location **ptr) {
	*ptr = NULL;
	++lookup_count;
	gale_find_location(oop,name,on_location,ptr);
}

/* Output usage information, exit. */
static void usage(void) {
	fprintf(stderr,
		"%s\n"
		"usage: gsend [-hap] [-f address] [-t nm=val] address [address ...] [/keyword]\n"
		"flags: -h          Display this message\n"
		"       -a          Send message anonymously\n"
		"       -p          Always request a return receipt\n"
		"       -f address  Send message from a specific <address> (multiple ok)\n"
		"       -t nm=val   Include text fragment 'nm' set to 'val' (multiple ok)\n"
		"       /keyword    Add a message keyword (multiple ok)\n"
		"You must specify at least one recipient address.\n"
		,GALE_BANNER);
	exit(1);
}

static void append(struct gale_fragment frag) {
	struct gale_group group = gale_group_empty();
	gale_group_add(&group,frag);
	gale_group_append(&msg->data,group);
}

int main(int argc,char *argv[]) {
	struct oop_source_sys *sys;		/* Event loop */
	int arg;				/* Command line flags */
	struct gale_fragment frag;

	if (argc <= 1) usage();

	/* Initialize the gale libraries. */
	gale_init("gsend",argc,argv);
	sys = oop_sys_new();
	oop = oop_sys_source(sys);
	/* gale_init_signals(oop); */

	/* Create a new message object to send. */
	gale_create(msg);
	msg->data = gale_group_empty();

	/* Conservatively allocate arrays. */
	gale_create_array(msg->to,argc);
	gale_create_array(msg->from,argc);
	for (arg = 0; arg != argc; ++arg)
		msg->to[arg] = msg->from[arg] = NULL;

	/* Parse command line options. */
	while ((arg = getopt(argc,argv,"Ddhat:pf:")) != EOF) {
		struct gale_text str = (NULL == optarg) ? null_text :
			gale_text_from(gale_global->enc_cmdline,optarg,-1);
	switch (arg) {
	case 'd': ++gale_global->debug_level; break;
	case 'D': gale_global->debug_level += 5; break;
	case 'a': do_identify = 0; break;	/* Anonymous */

	case 'f': 
		find_location(str,&msg->from[from_count++]);
		break;

	case 'p': do_rrcpt = 1; break;		/* Return receipt */

	case 't': 
		frag.type = frag_text;
		frag.name = null_text;
		gale_text_token(str,'=',&frag.name);
		frag.value.text = frag.name;
		if (!gale_text_token(str,'=',&frag.value.text))
			frag.value.text = null_text;
		append(frag);
		break;

	case 'h':
	case '?': usage();
	} }

	if (do_identify) {
		++lookup_count;
		gale_find_default_location(oop,on_location,&user);
	}

	for (arg = optind; argc != arg; ++arg) {
		struct gale_text a = gale_text_from(
			gale_global->enc_cmdline,
			argv[arg],-1);
		if ('/' != argv[arg][0])
			find_location(a,&msg->to[to_count++]);
		else {
			frag.name = G_("message.keyword");
			frag.type = frag_text;
			frag.value.text = gale_text_right(a,-1);
			append(frag);
		}
	}

	if (0 == --lookup_count) prepare_message();

	/* Everything else is asynchronous. */
	oop_sys_run(sys);
	return 0;
}
