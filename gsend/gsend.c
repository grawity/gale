/* gsend.c -- simple client for sending messages */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pwd.h>

#include "gale/all.h"

#if defined(HAVE_READLINE_READLINE_H) && defined(HAVE_LIBREADLINE)
#define HAVE_READLINE 1
#include "readline/readline.h"
#endif

struct gale_message *msg;               /* The message we're building. */
struct gale_location *user = NULL;	/* The local user. */
struct gale_location *bad_location = (struct gale_location *) 0xbaadbaad;
oop_source *oop;			/* Event source. */
int lookup_count = 0;			/* Outstanding lookup requests. */
int do_rrcpt = 1,do_identify = 1;       /* Various flags. */

/* Construct a comma-separated list of names. */
static struct gale_text comma_list(struct gale_location **list) {
	struct gale_text *array;
	int i,num;

	if (NULL == list || NULL == list[0]) return null_text;
	for (num = 0; NULL != list[num]; ++num) ;

	gale_create_array(array,2*num - 1);
	for (i = 0; num != i; ++i) {
		array[2*i] = gale_location_name(list[i]);
		if (0 == i) continue;
		if (i < num)
			array[2*i - 1] = G_(", ");
		else
			array[2*i - 1] = G_(" and ");
	}

	return gale_text_concat_array(2*num - 1,array);
}

/* Add default fragments to the message, if not already specified. */
void headers(void) {
	struct gale_fragment frag;
	char *tty;

	frag.name = G_("message/sender");
	frag.type = frag_text;
	frag.value.text = gale_var(G_("GALE_FROM"));
	if (0 != frag.value.text.l) gale_group_add(&msg->data,frag);

	tty = ttyname(0);
	if (tty && strchr(tty,'/')) tty = strrchr(tty,'/') + 1;
	gale_add_id(&msg->data,gale_text_from(gale_global->enc_filesys,tty,-1));

	if (do_rrcpt) {
		frag.name = G_("question/receipt");
		frag.type = frag_text;
		frag.value.text = gale_location_name(user);
		gale_group_add(&msg->data,frag);
	}
}

/* Read a string, and return a pointer to it.  Returns NULL on EOF. */
char *get_line(int tty)
{
	int alloc,len,num;
	char *line;

	(void) tty;

#ifdef HAVE_READLINE
	if (tty) {
		static int init = 1;
		if (init) {
			rl_initialize();
			rl_bind_key('\t',rl_insert);
			rl_bind_key('R' - '@',
			            rl_named_function("redraw-current-line"));
/*
			rl_parse_and_bind("set meta-flag On\n");
			rl_parse_and_bind("set convert-meta Off\n");
			rl_parse_and_bind("set output-meta On\n");
*/
			init = 0;
		}
		return readline("");
	}
#endif

	gale_create_array(line,alloc = 80);
	len = 0;

	do {
		if (len + 40 > alloc) line = gale_realloc(line,alloc *= 2);
		line[alloc - 2] = '\0';
		if (!fgets(line + len,alloc - len,stdin)) break;
		num = strlen(line + len);
		len += num;
	} while (num && line[len - 1] != '\n');

	if (!len) {
		gale_free(line);
		line = NULL;
	} else if (line[len - 1] == '\n') line[len - 1] = '\0';

	return line;
}

static void collapse(struct gale_location **from) {
	struct gale_location **to = from;
	if (NULL == from) return;
	while (NULL != *from) {
		if (bad_location == *from)
			++from;
		else
			*to++ = *from++;
	}
	*to = *from;
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
	server = gale_open(oop,link,G_("-"),null_text,0);

	link_put(link,pack);
	link_on_empty(link,on_empty,NULL);
	link_shutdown(link);
	return OOP_CONTINUE;
}

/* Get ready to send the message */
static void prepare_message() {
	struct gale_text body = null_text;	/* Message body */
	struct gale_fragment frag;
	char *line = NULL;			/* The current input line */
	int ttyin = isatty(0);	  		/* Input options */

	if (bad_location == user)
		gale_alert(GALE_ERROR,G_("Who are you?"),0);

	/* Sign with our key by default. */
	if (do_identify && NULL == msg->from) {
		gale_create_array(msg->from,2);
		msg->from[0] = user;
		msg->from[1] = NULL;
	}

	collapse(msg->from);
	collapse(msg->to);

	if (NULL == msg->to || NULL == msg->to[0])
		gale_alert(GALE_ERROR,G_("No valid recipients."),0);

	/* If stdin is a TTY, prompt the user. */
	if (ttyin) {
		gale_print(stdout,0,G_("Message for "));
		gale_print(stdout,gale_print_bold,comma_list(msg->to));
		gale_print(stdout,0,G_(":\n"));
		gale_print(stdout,0,G_("(End your message with EOF or a solitary dot.)\n"));
	}

	/* Add the default fragments to the message. */
	if (do_identify) headers();

	/* Get the message. */
	while ((line = get_line(ttyin))) {
		/* Check for a solitary dot if input comes from a TTY. */
		if (ttyin && !strcmp(line,".")) break;

		/* Append the line.  This is inefficient! */
		body = gale_text_concat(3,body,
			gale_text_from(gale_global->enc_console,line,-1),
			G_("\r\n"));
	}

	frag.name = G_("message/body");
	frag.type = frag_text;
	frag.value.text = body;
	gale_group_add(&msg->data,frag);

	/* Add the message to the outgoing queue. */
	gale_pack_message(oop,msg,on_pack,NULL);
}

/* Store a location address when lookup is complete. */
static void *on_location(struct gale_text n,struct gale_location *loc,void *x) {
	struct gale_location **ptr = (struct gale_location **) x;
	if (NULL != loc)
		*ptr = loc;
	else {
		*ptr = bad_location;
		gale_alert(GALE_WARNING,gale_text_concat(3,
			G_("cannot find \""),n,G_("\"")),0);
	}

	if (0 == --lookup_count) prepare_message();
	return OOP_CONTINUE;
}

/* Start looking up a location. */
static void find_location(struct gale_text name,struct gale_location **ptr) {
	*ptr = NULL;
	gale_find_location(oop,name,on_location,ptr);
	++lookup_count;
}

/* Output usage information, exit. */
static void usage(void) {
	fprintf(stderr,
		"%s\n"
		"usage: gsend [-hapP] [-s subj] [-S address] address [address ...]\n"
		"flags: -h          Display this message\n"
		"       -s subject  Set the message subject\n"
		"       -S address  Sign message with a specific <address>\n"
		"       -a          Do not sign message (anonymous)\n"
		"       -p          Always request a return receipt\n"
		"       -P          Never request a return receipt\n"
		"       -t nm=val   Include text fragment 'nm' set to 'val'\n"
		"You must specify at least one recipient address.\n"
		,GALE_BANNER);
	exit(1);
}

int main(int argc,char *argv[]) {
	struct oop_source_sys *sys;		/* Event loop */
	int arg;				/* Command line flags */
	struct gale_text subject = null_text;   /* Message subject */
	struct gale_fragment frag;

	/* Initialize the gale libraries. */
	gale_init("gsend",argc,argv);
	sys = oop_sys_new();
	oop = oop_sys_source(sys);
	/* gale_init_signals(oop); */

	/* Create a new message object to send. */
	gale_create(msg);
	msg->from = msg->to = NULL;
	msg->data = gale_group_empty();

	/* Parse command line options. */
	while ((arg = getopt(argc,argv,"Ddhat:Pps:S:")) != EOF) {
		struct gale_text str = (NULL == optarg) ? null_text :
			gale_text_from(gale_global->enc_cmdline,optarg,-1);
	switch (arg) {
	case 'd': ++gale_global->debug_level; break;
	case 'D': gale_global->debug_level += 5; break;
	case 'a': do_identify = 0; break;	/* Anonymous */

	case 's': 
		frag.name = G_("message/subject");
		frag.type = frag_text;
		frag.value.text = subject;
		gale_group_add(&msg->data,frag);
		break;

	case 'S': 
		gale_create_array(msg->from,2);
		find_location(str,&msg->from[0]);
		msg->from[1] = NULL;
		break;

	case 'p': do_rrcpt = 2; break;		/* Return receipt */
	case 'P': do_rrcpt = 0; break;

	case 't': 
		frag.type = frag_text;
		frag.name = null_text;
		gale_text_token(str,'=',&frag.name);
		frag.value.text = frag.name;
		if (!gale_text_token(str,'=',&frag.value.text))
			frag.value.text = null_text;
		gale_group_add(&msg->data,frag);
		break;

	case 'h':
	case '?': usage();
	} }

	if (do_identify) {
		++lookup_count;
		gale_find_default_location(oop,on_location,&user);
	}

	gale_create_array(msg->to,1 + argc - optind);
	msg->to[argc - optind] = NULL;
	for (arg = optind; argc != arg; ++arg)
		find_location(
		       gale_text_from(gale_global->enc_cmdline,argv[arg],-1),
		       &msg->to[arg - optind]);

	if (0 == lookup_count)
		gale_alert(GALE_ERROR,G_("No valid recipients."),0);

	/* Everything else is asynchronous. */
	oop_sys_run(sys);
	return 0;
}
