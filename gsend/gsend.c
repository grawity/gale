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
int do_sign = 1,do_encrypt = 0;		/* Various flags. */
int alloc = 0;

/* Whether we have collected any replacements for default headers. */
int have_from = 0,have_to = 0,have_time = 0,have_type = 0;

const char *pflag = NULL;               /* Return receipt destination. */
struct auth_id **rcpt = NULL;		/* Encryption recipients. */
struct auth_id *signer;			/* Identity to sign the message. */
int num_rcpt = 0;			/* Number of recipients. */

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

	/* Most of these are fairly obvious. */
	if (do_sign && !have_from) {
		const char *from = NULL;
		if (signer != user_id) from = auth_id_comment(signer);
		if (!from || !*from) from = getenv("GALE_FROM");
		reserve(20 + strlen(from));
		sprintf(msg->data + msg->data_size,"From: %s\r\n",from);
		msg->data_size += strlen(msg->data + msg->data_size);
	}
	if (!have_to && num_rcpt) {
		/* Build a comma-separated list of encryption recipients. */
		int i,size = 30;
		const char **n = gale_malloc(sizeof(const char *) * num_rcpt);
		for (i = 0; i < num_rcpt; ++i) {
			const char *comment = auth_id_comment(rcpt[i]);
			const char *name = auth_id_name(rcpt[i]);
			n[i] = (comment && *comment) ? comment : name;
			size += strlen(n[i]) + 2;
		}
		reserve(size);

		strcpy(msg->data + msg->data_size,"To: ");
		msg->data_size += 4;

		for (i = 0; i < num_rcpt; ++i) {
			strcpy(msg->data + msg->data_size,i ? ", " : "");
			strcat(msg->data + msg->data_size,n[i]);
			msg->data_size += strlen(msg->data + msg->data_size);
		}

		strcpy(msg->data + msg->data_size,"\r\n");
		msg->data_size += 2;
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

/* Add an encryption recipient to the list. */

void add_rcpt(const char *name) {
	struct auth_id *id = lookup_id(name),**n;
	do_encrypt = 1;

	if (!id) return;
	if (!find_id(id)) {
		char *buf = gale_malloc(strlen(name) + 30);
		sprintf(buf,"cannot locate key for \"%s\"",name);
		gale_alert(GALE_WARNING,buf,0);
		gale_free(buf);
		return;
	}

	n = gale_malloc((num_rcpt + 1) * sizeof(struct auth_id *));
	memcpy(n,rcpt,num_rcpt * sizeof(struct auth_id *));
	n[num_rcpt++] = id;
	gale_free(rcpt);
	rcpt = n;
}

void usage(void) {
	struct gale_id *id = lookup_id("username@domain");
	fprintf(stderr,
		"%s\n"
		"usage: gsend [-aruU] [-e id [-e id] ...] [-S id] [-p cat] cat\n"
		"flags: -e id       Send private message to <id> (many ok)\n"
		"       -S id       Sign message with a specific <id> (one)\n"
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
	char *cp,*tmp,*sign = NULL;          /* Various temporary strings */

	/* Initialize the gale libraries. */
	gale_init("gsend",argc,argv);

	/* Parse command line options. */
	while ((arg = getopt(argc,argv,"hae:t:p:S:ruU")) != EOF) 
	switch (arg) {
	case 'a': do_sign = 0; break;        /* Anonymous (no signature) */
	case 'e': add_rcpt(optarg); break;   /* Encrypt for recipient */
	case 'S': sign = optarg;             /* Select an ID to sign with */
	          do_sign = 1; 
	          break;
	case 'p': pflag = optarg; break;     /* Request return receipt */
	case 'r': rflag = 1; break;          /* Don't retry */
	case 'u': uflag = 1; break;          /* Expect user-supplied headers */
	case 'U': uflag = 2; break;          /* Don't supply our own headers */
	case 'h':                            /* Usage message */
	case '?': usage();
	}

	if (do_encrypt && !num_rcpt) 
		gale_alert(GALE_ERROR,"No valid recipients.",0);

	if (do_sign) {
		if (sign)
			signer = lookup_id(sign);
		else
			signer = user_id;
		if (!auth_id_private(signer))
			gale_alert(GALE_ERROR,"No private key to sign with.",0);
	}

	/* Create a new message object to send. */
	msg = new_message();

	/* Generate keys. */
	if (do_encrypt || do_sign) gale_keys();

	if (optind == argc && do_encrypt) {  /* Default category... */
		char **n = gale_malloc(sizeof(char *) * num_rcpt);
		int i,size = 0;
		for (i = 0; i < num_rcpt; ++i) {
			n[i] = id_category(rcpt[i],"user","");
			size += strlen(n[i]) + 1;
		}
		msg->category = gale_malloc(size);
		for (i = 0; i < num_rcpt; ++i) {
			if (i) {
				strcat(msg->category,":");
				strcat(msg->category,n[i]);
			} else
				strcpy(msg->category,n[i]);
		}
		for (i = 0; i < num_rcpt; ++i) gale_free(n[i]);
		gale_free(n);
	} else if (optind != argc - 1)       /* Wrong # arguments */
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
		printf("Message for ");
		if (!do_encrypt)
			printf("*everyone*");
		else {
			int i;
			for (i = 0; i < num_rcpt; ++i) {
				if (i > 0) {
					if (i < num_rcpt - 1) 
						printf(", ");
					else 
						printf(" and ");
				}
				printf("%s",auth_id_name(rcpt[i]));
			}
		}
		printf(" in category \"%s\":\n",msg->category);
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
	if (do_sign) {
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
