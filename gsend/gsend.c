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
int num = 0,alloc = 0;

/* Various flags. */
int do_encrypt = 0,do_rrcpt = 1,do_identify = 1;

struct auth_id **rcpt = NULL;		/* Encryption recipients. */
struct auth_id *signer;			/* Identity to sign the message. */
int num_rcpt = 0;			/* Number of recipients. */

/* Parse user-supplied text. */
void parse_text(struct gale_text arg) {
	struct gale_fragment frag;

	frag.type = frag_text;
	frag.name = null_text;
	gale_text_token(arg,'=',&frag.name);
	frag.value.text = frag.name;
	if (!gale_text_token(arg,'=',&frag.value.text))
		frag.value.text = null_text;
	gale_group_add(&msg->data,frag);
}

/* Add default fragments to the message, if not already specified. */
void headers(void) {
	struct gale_fragment frag;
	char *tty;

	/* Most of these are fairly obvious. */
	if (signer) {
		frag.name = G_("message/sender");
		frag.type = frag_text;
		frag.value.text = gale_var(G_("GALE_FROM"));
		gale_group_add(&msg->data,frag);
	}

	if (num_rcpt) {
		int i;

		frag.name = G_("message/recipient");
		frag.type = frag_text;
		frag.value.text = null_text;

		for (i = 0; i < num_rcpt; ++i)
			frag.value.text = gale_text_concat(3,
				frag.value.text,
				i ? G_(", ") : G_(""),
				auth_id_comment(rcpt[i]));

		if (!do_encrypt)
			frag.value.text = gale_text_concat(2,
				frag.value.text,G_(", et al."));

		gale_group_add(&msg->data,frag);
	}

	tty = ttyname(0);
	if (tty && strchr(tty,'/')) tty = strrchr(tty,'/') + 1;
	gale_add_id(&msg->data,gale_text_from_local(tty,-1));

	if (do_rrcpt) {
		frag.name = G_("question/receipt");
		frag.type = frag_text;
		frag.value.text = 
			id_category(gale_user(),G_("user"),G_("receipt"));
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

/* Output usage information, exit. */
void usage(void) {
	struct auth_id *id = lookup_id(G_("name@domain"));
	fprintf(stderr,
		"%s\n"
		"usage: gsend [-hapP] [-s subj] [-S id] [-cC cat] [id [id ...]]\n"
		"flags: -h          Display this message\n"
		"       -c cat      Add public category <cat> to recipients\n"
		"       -C cat      Only use category <cat> for message\n"
		"       -s subject  Set the message subject\n"
		"       -S id       Sign message with a specific <id>\n"
		"       -a          Do not sign message (anonymous)\n"
		"       -p          Always request a return receipt\n"
		"       -P          Never request a return receipt\n"
		"       -t nm=val   Include text fragment 'nm' set to 'val'\n"
		"With an id of \"name@domain\", category defaults to \"%s\".\n"
		"You must specify one of -c, -C, or a recipient user.\n"
		,GALE_BANNER
		,gale_text_to_local(id_category(id,G_("user"),G_(""))));
	exit(1);
}

/* Terminate the event loop when the message is sent. */
void *on_empty(struct gale_link *link,void *user) {
	return OOP_HALT;
}

/* I think you know what main does. */
int main(int argc,char *argv[]) {
	struct oop_source_sys *sys;		/* Event loop */
	struct gale_link *link;			/* The physical connection */
	struct gale_server *server;		/* The logical link */
	int arg;				/* Command line flags */
	int ttyin = isatty(0);	  		/* Input options */
	char *line = NULL;			/* The current input line */
	struct gale_text public = null_text;	/* Public cateogry */
	struct gale_text body = null_text;	/* Message body */
	struct gale_text subject = null_text;   /* Message subject */
	struct gale_fragment frag;

	/* Initialize the gale libraries. */
	gale_init("gsend",argc,argv);

	/* Create a new message object to send. */
	msg = new_message();
	msg->data = gale_group_empty();

	/* Default is to sign with our key. */
	signer = gale_user();

	/* Parse command line options. */
	while ((arg = getopt(argc,argv,"Ddhac:C:t:Pps:S:uU")) != EOF) 
	switch (arg) {
	case 'd': ++gale_global->debug_level; break;
	case 'D': gale_global->debug_level += 5; break;
	case 'a': signer = NULL; 		/* Anonymous */
	          do_identify = 0; break;
	case 'c': if (!public.p) public =       /* Public message */
	          gale_text_from_local(optarg,-1);
	          else public = 
	          gale_text_concat(3,public,G_(":"),gale_text_from_local(optarg,-1));
	          break;
	          gale_text_from_local(optarg,-1);
	          break;
	case 'C': msg->cat =			/* Select a category */
	          gale_text_from_local(optarg,-1); 
	          break;
	case 's': subject =                     /* Set the subject */
	          gale_text_from_local(optarg,-1);
	          break;
	case 'S': signer =      		/* Select an ID to sign with */
	          lookup_id(gale_text_from_local(optarg,-1));
	          break;
	case 'p': do_rrcpt = 2; break;		/* Return receipt */
	case 'P': do_rrcpt = 0; break;
	case 't': parse_text(			/* User defined fragment */
	          gale_text_from_local(optarg,-1));
	          break;
	case 'h':
	case '?': usage();
	}

	gale_create_array(rcpt,argc - optind);
	for (; argc != optind; ++optind) {
		struct auth_id *id = 
			lookup_id(gale_text_from_local(argv[optind],-1));
		if (!public.p) do_encrypt = 1;

		if (!auth_id_public(id)) {
			char *buf = gale_malloc(strlen(argv[optind]) + 30);
			sprintf(buf,"cannot find user \"%s\"",argv[optind]);
			gale_alert(GALE_WARNING,buf,0);
			gale_free(buf);
			continue;
		}

		rcpt[num_rcpt++] = id;
	}

	if (do_encrypt && !num_rcpt) 
		gale_alert(GALE_ERROR,"No valid recipients.",0);

	if (signer && !auth_id_private(signer))
		gale_alert(GALE_ERROR,"No private key to sign with.",0);

	if (!msg->cat.p && !public.p && !num_rcpt) usage();
	if (!do_encrypt && do_rrcpt == 1) do_rrcpt = 0;

	if (!msg->cat.p) {  /* Select default category... */
		int i;
		msg->cat = public;
		for (i = 0; i < num_rcpt; ++i) {
			if (msg->cat.l)
				msg->cat = gale_text_concat(3,
					msg->cat,G_(":"),
					id_category(rcpt[i],G_("user"),G_("")));
			else
				msg->cat = 
					id_category(rcpt[i],G_("user"),G_(""));
		}
	}

	/* A silly little check for a common mistake. */
	if (ttyin && getpwnam(gale_text_to_local(msg->cat))) {
		gale_beep(stderr);
		gale_alert(GALE_WARNING,"*** DANGER! *** "
		                        "Category is a username!",0);
	}
	if (ttyin) {
		int found = 0;
		struct gale_text cat = null_text;
		while (!found && gale_text_token(msg->cat,':',&cat)) {
			int i;
			for (i = 0; i < cat.l; ++i) {
				if ('/' == cat.p[i]) {
					found = 0; 
					break;
				}
				if ('@' == cat.p[i] && 0 < i) 
					found = 1;
			}
		}

		if (found) {
			gale_beep(stderr);
			gale_alert(GALE_WARNING,"*** HEATH ALERT! *** "
			           "Category may be an ID!",0);
		}
	}

	/* Open a connection to the server; don't subscribe to anything. */
	sys = oop_sys_new();
	link = new_link(oop_sys_source(sys));
	server = gale_open(oop_sys_source(sys),link,G_("-"),null_text,0);

	/* If stdin is a TTY, prompt the user. */
	if (ttyin) {
		if (!do_encrypt) {
			gale_print(stdout,gale_print_bold,G_("** PUBLIC **"));
			gale_print(stdout,0,G_(" message"));
		} else {
			int i;
			printf("Private message for ");
			for (i = 0; i < num_rcpt; ++i) {
				if (i > 0) {
					if (i < num_rcpt - 1) 
						printf(", ");
					else 
						printf(" and ");
				}
				gale_print(stdout,gale_print_bold,auth_id_name(rcpt[i]));
			}
		}
		gale_print(stdout,0,num_rcpt > 1 ? G_("\n") : G_(" "));
		gale_print(stdout,0,G_("in ["));
		gale_print(stdout,gale_print_bold,msg->cat);
		gale_print(stdout,0,G_("]:\n"));
		gale_print(stdout,0,G_("(End your message with EOF or a solitary dot.)\n"));
	}

	/* Add the default fragments to the message. */
	if (do_identify) headers();

	/* Get the message. */
	while ((line = get_line(ttyin))) {
		/* Check for a solitary dot if input comes from a TTY. */
		if (ttyin && !strcmp(line,".")) break;

		/* Append the line.  This is inefficient! */
		body = gale_text_concat(3,
			body,
			gale_text_from_local(line,-1),
			G_("\r\n"));
	}

	frag.name = G_("message/body");
	frag.type = frag_text;
	frag.value.text = body;
	gale_group_add(&msg->data,frag);

	if (subject.l) {
		frag.name = G_("message/subject");
		frag.type = frag_text;
		frag.value.text = subject;
		gale_group_add(&msg->data,frag);
	}

	/* Sign the message, unless we shouldn't. */
	if (signer) {
		struct gale_message *new = sign_message(signer,msg);
		if (new) msg = new;
	}

	/* Ounce is being completely psychotic right now. */
	if (do_encrypt) {
		msg = encrypt_message(num_rcpt,rcpt,msg);
		if (msg == NULL) gale_alert(GALE_ERROR,"encryption failure",0);
	}

	/* Add the message to the outgoing queue. */
	link_put(link,msg);
	link_on_empty(link,on_empty,NULL);
	link_shutdown(link);

	/* Wait for it to go! */
	oop_sys_run(sys);

	if (ttyin)
		printf("Message transmitted.\n");   /* Ta-daa! */

	gale_close(server);
	return 0;
}
