/* gsub.c -- subscription client, outputs messages to the tty, optionally
   sending them through a gsubrc filter. */

#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/utsname.h>

#include <curses.h>
#include <term.h>

#include "gale/all.h"

extern char **environ;

char *rcprog = "gsubrc";                /* Filter program name. */
struct gale_client *client;             /* Connection to server. */
char *tty,*agent;                       /* TTY device, user-agent string. */

int do_ping = 1;			/* Should we answer Receipt-To's? */
int do_termcap = 0;                     /* Should we highlight headers? */

void *gale_malloc(size_t size) { return malloc(size); }
void gale_free(void *ptr) { free(ptr); }

/* Generate a trivial little message with the given category.  Used for
   return receipts, login/logout notifications, and such. */
struct gale_message *slip(const char *cat,
                          struct gale_id *sign,struct auth_id *encrypt)
{
	struct gale_message *msg;
	int len = strlen(agent);
	static int sequence = 0;

	if (auth_id_comment(user_id)) len += strlen(auth_id_comment(user_id));

	/* Create a new message. */
	msg = new_message();
	msg->category = gale_strdup(cat);
	msg->data = gale_malloc(128 + len);

	/* A few obvious headers. */
	if (auth_id_comment(user_id))
		sprintf(msg->data,
			"From: %s\r\n"
			"Time: %lu\r\n"
			"Agent: %s\r\n"
			"Sequence: %d\r\n"
			"\r\n",
		        getenv("GALE_FROM"),time(NULL),agent,sequence++);
	else
		sprintf(msg->data,
			"Time: %lu\r\n"
			"Agent: %s\r\n"
			"Sequence: %d\r\n"
			"\r\n",time(NULL),agent,sequence++);

	msg->data_size = strlen(msg->data);

	/* Sign and encrypt the message, if appropriate. */
	if (sign) {
		struct gale_message *new = sign_message(sign,msg);
		if (new) {
			release_message(msg);
			msg = new;
		}
	}

	if (encrypt) {
		/* For safety's sake, don't leave the old message in place
		   if encryption fails. */
		struct gale_message *new = encrypt_message(1,&encrypt,msg);
		release_message(msg);
		msg = new;
	}

	return msg;
}

/* Output a terminal mode string. */
void tmode(char id[2]) {
	char *cap;
	if (do_termcap && (cap = tgetstr(id,NULL))) 
		tputs(cap,1,TPUTS_CAST putchar);
}

/* Print a user ID, with a default string (like "everyone") for NULL. */
void print_id(const char *id,const char *dfl) {
	putchar(' ');
	putchar(id ? '<' : '*');
	tmode("md");
	fputs(id ? id : dfl,stdout);
	tmode("me");
	putchar(id ? '>' : '*');
}

/* The default gsubrc implementation. */
void default_gsubrc(void) {
	char *tmp,buf[80],*cat = getenv("GALE_CATEGORY");
	char *nl = tty ? "\r\n" : "\n";
	int count = 0;

	/* Ignore messages to categories ending in /ping */
	tmp = cat;
	while ((tmp = strstr(tmp,"/ping"))) {
		tmp += 5;
		if (!*tmp || *tmp == ':') return;
	}

	/* Print the header: category, time, et cetera */
	putchar('[');
	tmode("md");
	fputs(cat,stdout);
	tmode("me");
	putchar(']');
	if ((tmp = getenv("HEADER_TIME"))) {
		time_t when = atoi(tmp);
		strftime(buf,sizeof(buf)," %m/%d %H:%M",localtime(&when));
		fputs(buf,stdout);
	}
	if (getenv("HEADER_RECEIPT_TO")) 
		printf(" [rcpt]");
	fputs(nl,stdout);

	/* Print who the message is from and to. */
	{
		char *from_comment = getenv("HEADER_FROM");
		char *from_id = getenv("GALE_SIGNED");
		char *to_comment = getenv("HEADER_TO");
		char *to_id = getenv("GALE_ENCRYPTED");

		fputs("From",stdout);
		if (from_comment || from_id) {
			print_id(from_id,"unverified");
			if (from_comment) printf(" (%s)",from_comment);
		} else
			print_id(NULL,"anonymous");

		fputs(" to",stdout);
		print_id(to_id,"everyone");
		if (to_comment) printf(" (%s)",to_comment);

		putchar(':');
		fputs(nl,stdout);
	}

	/* Print the message body.  Make sure to escape unprintables. */
	fputs(nl,stdout);
	while (fgets(buf,sizeof(buf),stdin)) {
		char *ptr,*end = buf + strlen(buf);
		for (ptr = buf; ptr < end; ++ptr) {
			if (isprint(*ptr & 0x7F) || *ptr == '\t')
				putchar(*ptr);
			else if (*ptr == '\n')
				fputs(nl,stdout);
			else {
				tmode("mr");
				if (*ptr < 32)
					printf("^%c",*ptr + 64);
				else
					printf("[0x%X]",*ptr);
				tmode("me");
			}
		}
		++count;
	}

	/* Add a final newline, if for some reason the message did not
           contain one. */
	if (count) fputs(nl,stdout);

	/* Out it goes! */
	fflush(stdout);
}

/* Transmit a message body to a gsubrc process. */
void send_message(char *body,char *end,int fd) {
	char *tmp;

	while (body != end) {
		/* Write data up to a newline. */
		tmp = memchr(body,'\r',end - body);
		if (!tmp) tmp = end;
		while (body != tmp) {
			int r = write(fd,body,tmp - body);
			if (r <= 0) {
				gale_alert(GALE_WARNING,"write",errno);
				return;
			}
			body += r;
		}

		/* Translate CRLF to NL. */
		if (tmp != end) {
			if (write(fd,"\n",1) != 1) {
				gale_alert(GALE_WARNING,"write",errno);
				return;
			}
			++tmp;
			if (tmp != end && *tmp == '\n') ++tmp;
		}
		body = tmp;
	}
}

/* Take the message passed as an argument and show it to the user, running
   their gsubrc if present, using the default formatter otherwise. */
void present_message(struct gale_message *msg) {
	int pfd[2];             /* Pipe file descriptors. */

	/* Lots of crap.  Discussed below, where they're used. */
	char *next,**envp = NULL,*key,*data,*end,*tmp;
	struct gale_id *id_encrypted = NULL,*id_sign = NULL;
	struct gale_message *rcpt = NULL;
	int envp_global,envp_alloc,envp_len,status;
	pid_t pid;

	/* Count the number of global environment variables. */
	envp_global = 0;
	for (envp = environ; *envp; ++envp) ++envp_global;

	/* Allocate space for ten more for the child.  That should do. */
	envp_alloc = envp_global + 10;
	envp = gale_malloc(envp_alloc * sizeof(*envp));
	memcpy(envp,environ,envp_global * sizeof(*envp));
	envp_len = envp_global;

	/* GALE_CATEGORY: the message category */
	next = gale_malloc(strlen(msg->category) + 15);
	sprintf(next,"GALE_CATEGORY=%s",msg->category);
	envp[envp_len++] = next;

	/* Decrypt, if necessary. */
	id_encrypted = decrypt_message(msg,&msg);
	if (!msg) goto error;
	if (id_encrypted) {
		tmp = gale_malloc(strlen(auth_id_name(id_encrypted)) + 16);
		sprintf(tmp,"GALE_ENCRYPTED=%s",auth_id_name(id_encrypted));
		envp[envp_len++] = tmp;
	}

	/* Verify a signature, if possible. */
	id_sign = verify_message(msg);
	if (id_sign) {
		tmp = gale_malloc(strlen(auth_id_name(id_sign))+13);
		sprintf(tmp,"GALE_SIGNED=%s",auth_id_name(id_sign));
		envp[envp_len++] = tmp;
	}

	/* Go through the message headers. */
	next = msg->data;
	end = msg->data + msg->data_size;
	while (parse_header(&next,&key,&data,end)) {

		/* Process receipts, if we do. */
		if (do_ping && !strcasecmp(key,"Receipt-To")) {
			const char *colon = data;
			struct gale_id *sign = id_encrypted;

			/* Make sure the receipt only goes to categories
			   beginning with "receipt/". */
			while (colon && *colon && !strncmp(colon,"receipt/",8))
				colon = strchr(colon + 1,':');
			if (colon && *colon) {
				gale_alert(GALE_WARNING,
				           "invalid receipt header",0);
				continue;
			}

			/* Generate a receipt. */
			if (!sign) sign = user_id;
			if (rcpt) release_message(rcpt);
			rcpt = slip(data,sign,id_sign);
		}

		/* Create a HEADER_... environment entry for this. */
		for (tmp = key; *tmp; ++tmp)
			*tmp = isalnum(*tmp) ? toupper(*tmp) : '_';
		tmp = gale_malloc(strlen(key) + strlen(data) + 9);
		sprintf(tmp,"HEADER_%s=%s",key,data);

		/* Allocate more space for the environment if necessary. */
		if (envp_len == envp_alloc - 1) {
			char **tmp = envp;
			envp_alloc *= 2;
			envp = gale_malloc(envp_alloc * sizeof(*envp));
			memcpy(envp,tmp,envp_len * sizeof(*envp));
			gale_free(tmp);
		}

		envp[envp_len++] = tmp;
	}

#ifndef NDEBUG
	/* In debug mode, restart if we get a properly authorized message. */
	if (!strcmp(msg->category,"debug/restart") &&
	    id_sign && !strcmp(auth_id_name(id_sign),"egnor@ofb.net")) {
		gale_alert(GALE_NOTICE,"Restarting from debug/restart.",0);
		gale_restart();
	}
#endif

	/* Terminate the new environment. */
	envp[envp_len] = NULL;

	/* Create a pipe to communicate with the gsubrc with. */
	if (pipe(pfd)) {
		gale_alert(GALE_WARNING,"pipe",errno);
		goto error;
	}

	/* Fork off a subprocess.  This should use gale_exec ... */
	pid = fork();
	if (!pid) {
		const char *rc;

		/* Set the environment.  (Why not execle?) */
		environ = envp;

		/* Close off file descriptors. */
		close(client->socket);
		close(pfd[1]);

		/* Pipe goes to stdin. */
		dup2(pfd[0],0);
		if (pfd[0] != 0) close(pfd[0]);

		/* Look for the file. */
		rc = dir_search(rcprog,1,dot_gale,sys_dir,NULL);
		if (rc) {
			execl(rc,rcprog,NULL);
			gale_alert(GALE_WARNING,rc,errno);
			exit(1);
		}

		/* If we can't find or can't run gsubrc, use default. */
		default_gsubrc();
		exit(0);
	}

	if (pid < 0) gale_alert(GALE_WARNING,"fork",errno);

	/* Send the message to the gsubrc. */
	close(pfd[0]);
	send_message(next,end,pfd[1]);
	close(pfd[1]);

	/* Wait for the gsubrc to terminate. */
	status = -1;
	if (pid > 0) {
		waitpid(pid,&status,0);
		if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
			status = 0;
		else
			status = -1;
	}

	/* Put the receipt on the queue, if we have one. */
	if (rcpt && !status) link_put(client->link,rcpt);

error:
	/* Clean up after ourselves. */
	if (envp) {
		while (envp_global != envp_len) gale_free(envp[envp_global++]);
		gale_free(envp);
	}
	if (id_encrypted) free_id(id_encrypted);
	if (id_sign) free_id(id_sign);
	if (msg) release_message(msg);
	if (rcpt) release_message(rcpt);
}

/* Send a login notification, arrange for a logout notification. */
void notify(void) {
	struct gale_message *msg;
	char *tmp;

	/* Login: send it right away. */
	tmp = id_category(user_id,"notice","login");
	msg = slip(tmp,user_id,NULL);
	gale_free(tmp);
	link_put(client->link,msg);
	release_message(msg);

	/* Logout: "will" it to happen when we disconnect. */
	tmp = id_category(user_id,"notice","logout");
	msg = slip(tmp,user_id,NULL);
	gale_free(tmp);
	link_will(client->link,msg);
	release_message(msg);
}

/* Set the value to use for Agent: headers. */
void set_agent(void) {
	char *user = getenv("LOGNAME");
	const char *host = getenv("HOST");
	int len;

	/* Construct the string from our version, the user running us,
           the host we're on and so on. */
	len = strlen(GALE_VERSION) + strlen(user) + strlen(host);
	len += (tty ? strlen(tty) : 0) + 30;
	agent = gale_malloc(len);
	sprintf(agent,"gsub/%s %s@%s %s %d",
	        GALE_VERSION,user,host,tty ? tty : "none",(int) getpid());
}

void usage(void) {
	fprintf(stderr,
	"%s\n"
	"usage: gsub [-nkKrpy] [-f rcprog] cat\n"
	"flags: -n          Do not fork (default if stdout redirected)\n"
	"       -k          Do not kill other gsub processes\n"
	"       -K          Kill other gsub processes and terminate\n"
	"       -r          Do not retry server connection\n"
	"       -f rcprog   Use rcprog (default gsubrc, then built-in)\n"
	"       -p          Suppress return-receipt processing altogether\n"
	"       -y          Disable login/logout notification\n"
	,GALE_BANNER);
	exit(1);
}

/* main */

int main(int argc,char **argv) {
	/* Various flags. */
	int opt,do_retry = 1,do_notify = 1,do_fork = 0,do_kill = 0;
	char *serv;             /* Subscription list. */

	/* Initialize the gale libraries. */
	gale_init("gsub",argc,argv);

	/* If we're actually on a TTY, we do things slightly different. */
	if ((tty = ttyname(1))) {
		/* Find out the terminal type. */
		char *term = getenv("TERM");
		/* Truncate the tty name for convenience. */
		char *tmp = strrchr(tty,'/');
		if (tmp) tty = tmp + 1;
		/* Go into the background; kill other gsub processes. */
		do_fork = do_kill = 1;
		/* Do highlighting, if available. */
		if (term && 1 == tgetent(NULL,term)) do_termcap = 1;
	}

	/* Parse command line arguments. */
	while (EOF != (opt = getopt(argc,argv,"nkKrpyf:h"))) switch (opt) {
	case 'n': do_fork = 0; break;           /* Do not go into background */
	case 'k': do_kill = 0; break;           /* Do not kill other gsubs */
	case 'K': if (tty) gale_kill(tty,1);    /* *only* kill other gsubs */
	          return 0;
	case 'f': rcprog = optarg; break;       /* Use a wacky gsubrc */
	case 'r': do_retry = 0; break;          /* Do not retry */
	case 'p': do_ping = 0; break;           /* Do not honor Receipt-To: */
	case 'y': do_notify = 0; break;         /* Do not send login/logout */
	case 'h':                               /* Usage message */
	case '?': usage();
	}

	/* One argument, at most (subscriptions) */
	if (optind < argc - 1) usage();

	/* Use the default subscriptions, unless they specify some. */
	if (optind == argc - 1)
		serv = argv[optind];
	else
		serv = getenv("GALE_SUBS");

	/* We need to subscribe to *something* */
	if (serv == NULL) 
		gale_alert(GALE_ERROR,"No subscriptions specified.",0);

	/* Generate keys so people can send us messages. */
	gale_keys();

#ifndef NDEBUG
	/* If in debug mode, listen to debug/ for restart messages. */
	{
		char *tmp = gale_malloc(strlen(serv) + 8);
		sprintf(tmp,"%s:debug/",serv);
		serv = tmp;
	}
#endif

	/* Open a connection to the server. */
	client = gale_open(serv);
	if (!do_retry && gale_error(client))
		gale_alert(GALE_ERROR,"Could not connect to server.",0);

	/* Fork ourselves into the background, unless we shouldn't. */
	if (do_fork) {
		gale_daemon(1);
		gale_kill(tty,do_kill);
	}

	/* Set our Agent: header value. */
	set_agent();

	/* Send a login message, as needed. */
	if (do_notify) notify();
	do {
		/* Get messages and process them. */
		while (!gale_send(client) && !gale_next(client)) {
			struct gale_message *msg;
			if (tty && !isatty(1)) return 0;
			if ((msg = link_get(client->link))) {
				present_message(msg);
				release_message(msg);
			}
		}
		/* Retry the server connection, unless we shouldn't. */
		if (do_retry) {
			gale_retry(client);
			if (do_notify) notify();
		}
	} while (do_retry);

	gale_alert(GALE_ERROR,"connection lost",0);
	return 0;
}
