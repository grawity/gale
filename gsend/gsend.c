/* gsend.c -- simple client for sending messages */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pwd.h>

#include "gale/all.h"

void *gale_malloc(size_t size) { return malloc(size); }
void gale_free(void *ptr) { free(ptr); }

struct gale_message *msg;               /* The message we're building. */
int aflag = 0,alloc = 0;		/* Various flags. */

/* Whether we have collected any replacements for default headers. */
int have_from = 0,have_to = 0,have_time = 0,have_type = 0;

const char *pflag = NULL;               /* Return receipt destination. */
struct gale_id *recipient = NULL;       /* Encryption recipient (if any). */

/* Reserve space in the message buffer. */
void reserve(int len) {
	char *tmp;

	/* Reallocate as appropriate. */
	while (msg->data_size + len >= alloc) {
		alloc = alloc ? alloc * 2 : 4096;
		tmp = gale_malloc(alloc);
		if (msg->data) {
			memcpy(tmp,msg->data,msg->data_size);
			gale_free(msg->data);
		}
		msg->data = tmp;
	}
}

/* Add headers to the message.  Skip already-specified headers. */
void headers(void) {
	/* This one is a little silly. */
	if (!have_type) {
		reserve(40);
		sprintf(msg->data + msg->data_size,
			"Content-type: text/plain\r\n");
		msg->data_size += strlen(msg->data + msg->data_size);
	}

	/* These are fairly obvious. */
	if (!aflag && !have_from && auth_id_comment(user_id)) {
		reserve(20 + strlen(auth_id_comment(user_id)));
		sprintf(msg->data + msg->data_size,
		        "From: %s\r\n",auth_id_comment(user_id));
		msg->data_size += strlen(msg->data + msg->data_size);
	}
	if (!have_to && recipient && auth_id_comment(recipient)) {
		reserve(20 + strlen(auth_id_comment(recipient)));
		sprintf(msg->data + msg->data_size,
		        "To: %s\r\n",auth_id_comment(recipient));
		msg->data_size += strlen(msg->data + msg->data_size);
	}
	if (!have_time) {
		reserve(20);
		sprintf(msg->data + msg->data_size,"Time: %lu\r\n",time(NULL));
		msg->data_size += strlen(msg->data + msg->data_size);
	}
	if (pflag) {
		reserve(20 + strlen(pflag));
		sprintf(msg->data + msg->data_size,
		        "Receipt-To: %s\r\n",pflag);
		msg->data_size += strlen(msg->data + msg->data_size);
	}

	/* Add a CRLF pair to end the headers. */
	reserve(2);
	msg->data[msg->data_size++] = '\r';
	msg->data[msg->data_size++] = '\n';
}

void usage(void) {
	struct gale_id *id = lookup_id("username@domain");
	fprintf(stderr,
		"%s\n"
		"usage: gsend [-uU] [-e id] [-p cat] cat\n"
		"flags: -e id       Send private message to <id>\n"
		"       -a          Do not sign message (anonymous)\n"
		"       -r          Do not retry server connection\n"
		"       -p cat      Request a return receipt\n"
		"       -u          Expect user-supplied headers\n"
		"       -U          Ditto, and don't supply default headers\n"
		"With -e, cat defaults to \"%s\".\n"
		,GALE_BANNER,id_category(id,"user",""));
	exit(1);
}

/* I think you know what main does. */

int main(int argc,char *argv[]) {
	struct gale_client *client;          /* The client structure */
	int arg,uflag = 0,rflag = 0;         /* Command line flags */
	int ttyin = isatty(0),newline = 1;   /* Input options */
	char *cp,*tmp,*eflag = NULL;         /* Various temporary strings */

	/* Initialize the gale libraries. */
	gale_init("gsend",argc,argv);

	/* Parse command line options. */
	while ((arg = getopt(argc,argv,"hae:t:p:ruU")) != EOF) 
	switch (arg) {
	case 'a': aflag = 1; break;          /* Anonymous (no signature) */
	case 'e': eflag = optarg; break;     /* Encrypt for recipient */
	case 'p': pflag = optarg; break;     /* Request return receipt */
	case 'r': rflag = 1; break;          /* Don't retry */
	case 'u': uflag = 1; break;          /* Expect user-supplied headers */
	case 'U': uflag = 2; break;          /* Don't supply our own headers */
	case 'h':                            /* Usage message */
	case '?': usage();
	}

	/* Create a new message object to send. */
	msg = new_message();

	if (eflag || !aflag) {
		gale_keys();
		if (!auth_id_public(user_id)) 
			auth_id_gen(user_id,getenv("GALE_FROM"));
	}

	/* If encrypting, look up the recipient. */
	if (eflag) {
		/* Generate our keys, in case we're sending to ourselves.
		   They should really exist by that point, but hey... */
		recipient = lookup_id(eflag);
	}

	if (optind == argc && eflag)         /* Default category... */
		msg->category = id_category(recipient,"user","");
	else if (optind != argc - 1)         /* Wrong # arguments */
		usage();
	else                                 /* Copy so we can free() later */
		msg->category = gale_strdup(argv[optind]);

	/* A silly little check for a common mistake. */
	if (ttyin && getpwnam(msg->category))
		gale_alert(GALE_WARNING,"Category name matches username!  "
		                        "Did you forget the \"-e\" flag?",0);

	/* Open a connection to the server; don't subscribe to anything. */
	client = gale_open(NULL);

	/* Retry as long as necessary to get the connection open. */
	while (gale_error(client)) {
		/* (Well, don't if they told us not to.) */
		if (rflag) gale_alert(GALE_ERROR,"could not contact server",0);
		gale_retry(client);
	}

	/* If stdin is a TTY, prompt the user. */
	if (ttyin) {
		printf("Message for %s in category \"%s\":\n",
			recipient ? auth_id_name(recipient) : "*everyone*",
			msg->category);
		printf("(End your message with EOF or a solitary dot.)\n");
	}

	/* Add the default headers to the message (unless we shouldn't) */
	if (uflag == 0) headers();

	/* Get the message.  "newline" is 1 when at the beginning of a line. */
	for(;;) {
		/* Reserve 80 characters.  This isn't a limit, it's just how
                   much we preallocate... */
		reserve(80);

		/* Read some text from stdin. */
		cp = msg->data + msg->data_size;
		*cp = '\0'; /* just to make sure */
		fgets(cp,alloc - msg->data_size - 1,stdin);

		/* If no text was read, I guess that's EOF. */
		tmp = cp + strlen(cp);
		if (tmp == cp) break;

		/* Check for a solitary dot if input comes from a TTY. */
		if (ttyin && newline && !strcmp(cp,".\n")) break;

		/* If they're supplying headers, but we add defaults, check
                   to see if they replace any of the defaults. */
		if (uflag == 1) {
			if (!strncasecmp(cp,"From:",5)) 
				have_from = 1;
			else if (!strncasecmp(cp,"Content-type:",13)) 
				have_type = 1;
			else if (!strncasecmp(cp,"Time:",5)) 
				have_time = 1;
			else if (!strcmp(cp,"\n")) {
				/* Ooooh, they've finished the headers.  Now
                                   we get to add any remaining defaults. */
				headers();
				uflag = 0;
				/* The newline's already been added by
				   headers(); just go get more text. */
				continue;
			}
		}

		/* Extend the message to include the text we just read. */
		msg->data_size = tmp - msg->data;

		/* Convert NL to CRLF. */
		if ((newline = (tmp[-1] == '\n'))) {
			tmp[-1] = '\r';
			*tmp++ = '\n';
			*tmp = '\0';
			++(msg->data_size);
		}
	}

	/* Sign the message, unless we shouldn't. */
	if (!aflag) {
		struct gale_message *new = sign_message(user_id,msg);
		if (new) {
			release_message(msg);
			msg = new;
		}
	}

	/* Ounce is being completely psychotic right now. */
	if (recipient) {
		struct gale_message *new = encrypt_message(1,&recipient,msg);
		release_message(msg);
		msg = new;
		if (msg == NULL) gale_alert(GALE_ERROR,"encryption failure",0);
	}

	/* Add the message to the outgoing queue. */
	link_put(client->link,msg);
	while (1) {
		/* Flush out the queue. */
		if (!gale_send(client)) break;
		if (rflag) gale_alert(GALE_ERROR,"transmission failed",0);
		/* Retry as necessary; put the message back on the queue
		   if it went away during the failure. */
		gale_retry(client);
		if (!link_queue(client->link))
			link_put(client->link,msg);
	}

	/* Here we free the message.  I don't know why. */
	release_message(msg);

	if (ttyin)
		printf("Message sent.\n");   /* Ta-daa! */

	return 0;
}
