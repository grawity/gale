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

void *gale_malloc(size_t size) { return malloc(size); }
void gale_free(void *ptr) { free(ptr); }

struct gale_message *msg;               /* The message we're building. */
int do_encrypt = 0,do_rrcpt = 1;
size_t alloc = 0;

/* Whether we have collected any replacements for default headers. */
int have_from = 0,have_to = 0,have_time = 0,have_type = 0;

struct auth_id **rcpt = NULL;		/* Encryption recipients. */
struct auth_id *signer;			/* Identity to sign the message. */
int num_rcpt = 0;			/* Number of recipients. */

/* Reserve space in the message buffer. */
void reserve(size_t len) {
	char *tmp;

	/* Reallocate as appropriate. */
	while (msg->data.l + len >= alloc) {
		alloc = alloc ? alloc * 2 : 4096;
		tmp = gale_malloc(alloc);
		if (msg->data.p) {
			memcpy(tmp,msg->data.p,msg->data.l);
			gale_free(msg->data.p);
		}
		msg->data.p = tmp;
	}
}

/* Add headers to the message.  Skip already-specified headers. */
void headers(void) {
	/* This one is a little silly. */
	if (!have_type) {
		reserve(40);
		sprintf(msg->data.p + msg->data.l,
			"Content-type: text/plain\r\n");
		msg->data.l += strlen(msg->data.p + msg->data.l);
	}

	/* Most of these are fairly obvious. */
	if (signer && !have_from) {
		const char *from = getenv("GALE_FROM");
		reserve(20 + strlen(from));
		sprintf(msg->data.p + msg->data.l,"From: %s\r\n",from);
		msg->data.l += strlen(msg->data.p + msg->data.l);
	}
	if (!have_to && num_rcpt) {
		/* Build a comma-separated list of encryption recipients. */
		int i,size = 30;
		for (i = 0; i < num_rcpt; ++i)
			size += auth_id_comment(rcpt[i]).l + 2;
		reserve(size);

		strcpy(msg->data.p + msg->data.l,"To: ");
		msg->data.l += 4;

		for (i = 0; i < num_rcpt; ++i) {
			strcpy(msg->data.p + msg->data.l,i ? ", " : "");
			strcat(msg->data.p + msg->data.l,
				gale_text_hack(auth_id_comment(rcpt[i])));
			msg->data.l += strlen(msg->data.p + msg->data.l);
		}

		strcpy(msg->data.p + msg->data.l,"\r\n");
		msg->data.l += 2;
	}
	if (!have_time) {
		reserve(20);
		sprintf(msg->data.p + msg->data.l,"Time: %lu\r\n",time(NULL));
		msg->data.l += strlen(msg->data.p + msg->data.l);
	}
	if (do_rrcpt) {
		struct gale_text cat = 
			id_category(user_id,_G("user"),_G("receipt"));
		char *pch = gale_text_to_latin1(cat);
		reserve(20 + strlen(pch));
		sprintf(msg->data.p + msg->data.l,"Receipt-To: %s\r\n",pch);
		msg->data.l += strlen(msg->data.p + msg->data.l);
		free_gale_text(cat);
		gale_free(pch);
	}

	/* Add a CRLF pair to end the headers. */
	reserve(2);
	msg->data.p[msg->data.l++] = '\r';
	msg->data.p[msg->data.l++] = '\n';
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

	line = gale_malloc(alloc = 80);
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
	struct gale_id *id = lookup_id(_G("name@domain"));
	fprintf(stderr,
		"%s\n"
		"usage: gsend [-auUpP] [-S id] [-cC cat] [id [id ...]]\n"
		"flags: -c cat      Add public category <cat> to recipients\n"
		"       -C cat      Only use category <cat> for message\n"
		"       -S id       Sign message with a specific <id>\n"
		"       -a          Do not sign message (anonymous)\n"
		"       -p          Always request a return receipt\n"
		"       -P          Never request a return receipt\n"
		"       -u          Expect user-supplied headers\n"
		"       -U          Ditto, and don't supply default headers\n"
		"With an id of \"name@domain\", category defaults to \"%s\".\n"
		"You must specify one of -c, -C, or a recipient user.\n"
		,GALE_BANNER
		,gale_text_hack(id_category(id,_G("user"),_G(""))));
	exit(1);
}

/* I think you know what main does. */
int main(int argc,char *argv[]) {
	struct gale_client *client;		/* The client structure */
	int arg,uflag = 0;			/* Command line flags */
	int ttyin = isatty(0);	  		/* Input options */
	char *line = NULL;			/* Various temporary strings */
	struct gale_text public = null_text;	/* Public cateogry */

	/* Initialize the gale libraries. */
	gale_init("gsend",argc,argv);

	/* Create a new message object to send. */
	msg = new_message();

	/* Default is to sign with our key. */
	signer = user_id;

	/* Parse command line options. */
	while ((arg = getopt(argc,argv,"hac:C:t:PpS:uU")) != EOF) 
	switch (arg) {
	case 'a': signer = NULL; break;		/* Anonymous (no signature) */
	case 'c': public = 			/* Public message */
	          gale_text_from_local(optarg,-1);
	          break;
	case 'C': msg->cat =			/* Select a category */
	          gale_text_from_local(optarg,-1); 
	          break;
	case 'S': signer =      		/* Select an ID to sign with */
	          lookup_id(gale_text_from_local(optarg,-1));
	          break;
	case 'p': do_rrcpt = 2; break;		/* Return receipt */
	case 'P': do_rrcpt = 0; break;
	case 'u': uflag = 1; break;		/* User-supplied headers */
	case 'U': uflag = 2; break;		/* Don't supply headers */
	case 'h':
	case '?': usage();
	}

	rcpt = gale_malloc(sizeof(*rcpt) * (argc - optind));
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

	/* Generate keys. */
	if (do_encrypt || signer) gale_keys();

	if (signer && !auth_id_private(signer))
		gale_alert(GALE_ERROR,"No private key to sign with.",0);

	if (!msg->cat.p && !public.p && !num_rcpt) usage();
	if (!do_encrypt && do_rrcpt == 1) do_rrcpt = 0;

	if (!msg->cat.p) {  /* Select default category... */
		struct gale_text *n = gale_malloc(sizeof(*n) * num_rcpt);
		struct gale_text colon = gale_text_from_latin1(":",1);
		int i;
		size_t size = 0;
		if (public.p) size += public.l + 1;
		for (i = 0; i < num_rcpt; ++i) {
			n[i] = id_category(rcpt[i],_G("user"),_G(""));
			size += n[i].l + 1;
		}
		msg->cat = new_gale_text(size);
		if (public.p) gale_text_append(&msg->cat,public);
		for (i = 0; i < num_rcpt; ++i) {
			if (msg->cat.l) gale_text_append(&msg->cat,colon);
			gale_text_append(&msg->cat,n[i]);
		}
		for (i = 0; i < num_rcpt; ++i) free_gale_text(n[i]);
		free_gale_text(colon);
		gale_free(n);
	}

	/* A silly little check for a common mistake. */
	if (ttyin && getpwnam(gale_text_hack(msg->cat)))
		gale_alert(GALE_WARNING,"*** DANGER! ***\a "
		                        "Category is a username!  "
		                        "Did you want that?",0);
	if (ttyin) {
		char *at,*cat = gale_text_to_latin1(msg->cat);
		for (at = cat; (at = strchr(at,'@')); ) {
			if (at != cat && at[-1] != ':')
				gale_alert(GALE_WARNING,"*** DANGER! ***\a "
					"Category contains '@'!  "
					"Did you want that?",0);
			++at;
		}
		gale_free(cat);
	}

	/* Open a connection to the server; don't subscribe to anything. */
	client = gale_open(gale_text_from_latin1(NULL,0));

	/* Retry as long as necessary to get the connection open. */
	while (gale_error(client)) gale_retry(client);

	/* If stdin is a TTY, prompt the user. */
	if (ttyin) {
		if (!do_encrypt)
			printf("** PUBLIC ** message");
		else {
			int i;
			printf("Private message for ");
			for (i = 0; i < num_rcpt; ++i) {
				if (i > 0) {
					if (i < num_rcpt - 1) 
						printf(", ");
					else 
						printf(" and ");
				}
				printf("%s",
				gale_text_hack(auth_id_name(rcpt[i])));
			}
		}
		putchar(num_rcpt > 1 ? '\n' : ' ');
		printf("in category \"%s\":\n",gale_text_hack(msg->cat));
		printf("(End your message with EOF or a solitary dot.)\n");
	}

	/* Add the default headers to the message (unless we shouldn't) */
	if (uflag == 0) headers();

	/* Get the message. */
	while ((line = get_line(ttyin))) {
		/* Check for a solitary dot if input comes from a TTY. */
		if (ttyin && !strcmp(line,".")) break;

		/* Copy in the data. */
		reserve(strlen(line) + 1);
		strcpy(msg->data.p + msg->data.l,line);
		msg->data.l += strlen(line);

		/* If they're supplying headers, but we add defaults, check
                   to see if they replace any of the defaults. */
		if (uflag == 1) {
			if (!strncasecmp(line,"From:",5)) 
				have_from = 1;
			else if (!strncasecmp(line,"Content-type:",13)) 
				have_type = 1;
			else if (!strncasecmp(line,"Time:",5)) 
				have_time = 1;
			else if (!strcmp(line,"\n")) {
				/* Ooooh, they've finished the headers.  Now
                                   we get to add any remaining defaults. */
				headers();
				uflag = 0;
				/* The newline's already been added by
				   headers(); just go get more text. */
				continue;
			}
		}

		reserve(3);
		strcpy(msg->data.p + msg->data.l,"\r\n");
		msg->data.l += 2;
	}

	/* Sign the message, unless we shouldn't. */
	if (signer) {
		struct gale_message *new = sign_message(signer,msg);
		if (new) {
			release_message(msg);
			msg = new;
		}
	}

	/* Ounce is being completely psychotic right now. */
	if (do_encrypt) {
		struct gale_message *new = encrypt_message(num_rcpt,rcpt,msg);
		release_message(msg);
		msg = new;
		if (msg == NULL) gale_alert(GALE_ERROR,"encryption failure",0);
	}

	/* Add the message to the outgoing queue. */
	link_put(client->link,msg);
	while (1) {
		/* Flush out the queue. */
		if (!gale_send(client)) break;
		/* Retry as necessary; put the message back on the queue
		   if it went away during the failure. */
		gale_retry(client);
		if (link_queue_num(client->link) < 1)
			link_put(client->link,msg);
	}

	/* Here we free the message.  I don't know why. */
	release_message(msg);

	if (ttyin)
		printf("Message transmitted.\n");   /* Ta-daa! */

	return 0;
}
