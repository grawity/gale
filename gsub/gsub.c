/* gsub.c -- subscription client, outputs messages to the tty, optionally
   sending them through a gsubrc filter. 

   Beware of using this as an example; it's insufficiently Unicode-ized. */

#include "gale/all.h"
#include "gale/gsubrc.h"

#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/utsname.h>

#ifdef HAVE_DLFCN_H
#define HAVE_DLOPEN
#include <dlfcn.h>
#endif

extern char **environ;

const char *rcprog = "gsubrc";		/* Filter program name. */
gsubrc_t *dl_gsubrc = NULL;		/* Loaded gsubrc function. */
gsubrc2_t *dl_gsubrc2 = NULL;		/* Extended gsubrc function. */
struct gale_client *client;             /* Connection to server. */
struct auth_id *user_id;		/* The user. */
struct gale_text presence;		/* Current presence state. */
char *tty;                              /* TTY device */
int sig_received = 0;			/* Signal received */

int do_stealth = 0;			/* Should we answer Receipt-To's? */
int do_beep = 1;			/* Should we beep? */
int do_termcap = 0;                     /* Should we highlight headers? */
int sequence = 0;

void get_signal(int sig) {
	sig_received = sig;
}

void trap_signal(int num) {
	struct sigaction act;
	if (sigaction(num,NULL,&act)) gale_alert(GALE_ERROR,"sigaction",errno);
	if (act.sa_handler != SIG_DFL) return;
	act.sa_handler = get_signal;
	if (sigaction(num,&act,NULL)) gale_alert(GALE_ERROR,"sigaction",errno);
}

/* Generate a trivial little message with the given category.  Used for
   return receipts, login/logout notifications, and such. */
struct gale_message *slip(struct gale_text cat,
                          struct gale_text presence,
                          struct gale_fragment *extra,
                          struct auth_id *sign,struct auth_id *encrypt)
{
	struct gale_message *msg;
	struct gale_fragment *frags[7];

	/* Create a new message. */
	msg = new_message();
	msg->cat = cat;

	gale_create(frags[0]);
	frags[0]->name = G_("message/sender");
	frags[0]->type = frag_text;
	frags[0]->value.text = gale_var(G_("GALE_FROM"));

	gale_create(frags[1]);
	frags[1]->name = G_("notice/presence");
	frags[1]->type = frag_text;
	frags[1]->value.text = presence;

	frags[2] = gale_make_id_class();
	frags[3] = gale_make_id_instance(gale_text_from_latin1(tty,-1));
	frags[4] = gale_make_id_time();

	frags[5] = extra;
	frags[6] = NULL;

	msg->data = pack_message(frags);

	/* Sign and encrypt the message, if appropriate. */
	if (sign) {
		struct gale_message *new = sign_message(sign,msg);
		if (new) msg = new;
	}

	/* For safety's sake, don't leave the old message in place if 
	   encryption fails. */
	if (encrypt) msg = encrypt_message(1,&encrypt,msg);

	return msg;
}

/* Reply to an AKD request: post our key. */

struct gale_message *send_key(void) {
	struct gale_fragment *frag;

	gale_create(frag);
	frag->name = G_("answer/key");
	frag->type = frag_data;
	export_auth_id(user_id,&frag->value.data,0);

	return slip(id_category(user_id,G_("auth/key"),G_("")),
	            presence,frag,user_id,NULL);
}

/* Print a user ID, with a default string (like "everyone") for NULL. */
void print_id(const char *id,const char *dfl) {
	putchar(' ');
	putchar(id ? '<' : '*');
	gale_tmode("md");
	fputs(id ? id : dfl,stdout);
	gale_tmode("me");
	putchar(id ? '>' : '*');
}

/* The default gsubrc implementation. */
void default_gsubrc(void) {
	char *tmp,buf[80],*cat = getenv("GALE_CATEGORY");
	char *nl = tty ? "\r\n" : "\n";
	int count = 0;
	time_t when;

	/* Ignore messages to category /ping */
	tmp = cat;
	while ((tmp = strstr(tmp,"/ping"))) {
		if ((tmp == cat || tmp[-1] == ':')
		&&  (tmp[5] == '\0' || tmp[5] == ':'))
			return;
		tmp += 5;
	}

	/* The star. */
	gale_tmode("md");
	fputs("*",stdout);
	gale_tmode("me");

	/* Print the time */
	if ((tmp = getenv("GALE_TIME_ID_TIME")))
		printf(" %s",tmp);
	else {
		time_t when = time(NULL);
		strftime(buf,sizeof(buf)," %Y-%m-%d %H:%M:%S",localtime(&when));
		fputs(buf,stdout);
	}

	/* Format return receipts specially */
	tmp = cat;
	while ((tmp = strstr(tmp,"/receipt"))) {
		tmp += 8;
		if (!*tmp || *tmp == ':') {
			char *from_comment = getenv("HEADER_FROM");
			char *presence = getenv("GALE_TEXT_NOTICE_PRESENCE");
			fputs(" received:",stdout);
			if (presence) printf(" %s",presence);
			print_id(getenv("GALE_SIGNED"),"unverified");
			if (from_comment) printf(" (%s)",from_comment);
			fputs(nl,stdout);
			fflush(stdout);
			return;
		}
	}

	/* Print the header: category, time, et cetera */
	fputs(" [",stdout);
	gale_tmode("md");
	fputs(cat,stdout);
	gale_tmode("me");
	putchar(']');
	if ((tmp = getenv("HEADER_TIME"))) {
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
		if (tty && to_id && do_beep) putchar('\a');
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
				unsigned char ch = *ptr;
				gale_tmode("mr");
				if (ch < 32)
					printf("^%c",ch + 64);
				else
					printf("[0x%X]",ch);
				gale_tmode("me");
			}
		}
		++count;
	}

	/* Add a final newline, if for some reason the message did not
           contain one. */
	if (count) fputs(nl,stdout);

	/* Out it goes! */
	if (fflush(stdout) < 0) exit(1);
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
				if (errno != EPIPE)
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

void notify(int in,struct gale_text presence) {
	struct gale_text cat;

	if (in) {
		cat = id_category(user_id,G_("notice"),G_("login"));
		link_put(client->link,slip(cat,presence,NULL,user_id,NULL));
	} else {
		cat = id_category(user_id,G_("notice"),G_("logout"));
		link_will(client->link,slip(cat,presence,NULL,user_id,NULL));
	}
}

/* Take the message passed as an argument and show it to the user, running
   their gsubrc if present, using the default formatter otherwise. */
void present_message(struct gale_message *_msg) {
	int pfd[2];             /* Pipe file descriptors. */

	/* Lots of crap.  Discussed below, where they're used. */
	char **envp = NULL,*tmp;
	struct gale_fragment **frags;
	struct gale_text body = null_text;
	struct auth_id *id_encrypted = NULL,*id_sign = NULL;
	struct gale_message *rcpt = NULL,*akd = NULL,*msg = NULL;
	int envp_global,envp_alloc,envp_len,status;
	pid_t pid;

	/* Count the number of global environment variables. */
	envp_global = 0;
	for (envp = environ; *envp; ++envp) ++envp_global;

	/* Allocate space for some more for the child.  That should do. */
	envp_alloc = envp_global + 20;
	gale_create_array(envp,envp_alloc);
	memcpy(envp,environ,envp_global * sizeof(*envp));
	envp_len = envp_global;

	/* GALE_CATEGORY: the message category */
	envp[envp_len++] = gale_text_to_local(
		gale_text_concat(2,G_("GALE_CATEGORY="),_msg->cat));

	/* Decrypt, if necessary. */
	id_encrypted = decrypt_message(_msg,&msg);
	if (!msg) {
		char *tmp = gale_malloc(_msg->cat.l + 80);
		sprintf(tmp,"cannot decrypt message on category \"%s\"",
		        gale_text_to_local(_msg->cat));
		gale_alert(GALE_WARNING,tmp,0);
		return;
	}

	if (id_encrypted) {
		tmp = gale_malloc(auth_id_name(id_encrypted).l + 16);
		sprintf(tmp,"GALE_ENCRYPTED=%s",
			gale_text_to_local(auth_id_name(id_encrypted)));
		envp[envp_len++] = tmp;
	}

	/* Verify a signature, if possible. */
	id_sign = verify_message(msg,&msg);
	if (id_sign) {
		tmp = gale_malloc(auth_id_name(id_sign).l + 13);
		sprintf(tmp,"GALE_SIGNED=%s",
			gale_text_to_local(auth_id_name(id_sign)));
		envp[envp_len++] = tmp;
	}

	/* Go through the message fragments. */
	frags = unpack_message(msg->data);
	while (*frags) {
		struct gale_fragment *frag = *frags++;
		struct gale_text name;
		wch *buf;
		int i;

		/* Process receipts, if we do. */
		if (!do_stealth && frag->type == frag_text
		&&  !gale_text_compare(frag->name,G_("question/receipt"))) {
			/* Generate a receipt. */
			struct gale_fragment *reply;
			gale_create(reply);
			reply->name = G_("answer/receipt");
			reply->type = frag_text;
			reply->value.text = msg->cat;
			rcpt = slip(frag->value.text,
			            presence,reply,user_id,id_sign);
		}

		/* Handle AKD requests for us. */
		if (!do_stealth && frag->type == frag_text
		&&  !gale_text_compare(frag->name,G_("question/key"))
		&&  !gale_text_compare(frag->value.text,auth_id_name(user_id)))
			akd = send_key();

		/* Save the message body for later. */
		if (frag->type == frag_text
		&&  !gale_text_compare(frag->name,G_("message/body")))
			body = frag->value.text;

		/* Form the name used for environment variables. */
		gale_create_array(buf,frag->name.l);
		for (i = 0; i < frag->name.l; ++i)
			if (isalnum(frag->name.p[i]))
				buf[i] = toupper(frag->name.p[i]);
			else
				buf[i] = '_';
		name.p = buf;
		name.l = frag->name.l;

		/* Create environment variables. */
		if (frag_text == frag->type) {
			struct gale_text varname = null_text;

			if (!gale_text_compare(frag->name,G_("message/subject")))
				varname = G_("HEADER_SUBJECT");
			else 
			if (!gale_text_compare(frag->name,G_("message/sender")))
				varname = G_("HEADER_FROM");
			else 
			if (!gale_text_compare(frag->name,G_("message/recipient")))
				varname = G_("HEADER_TO");
			else
			if (!gale_text_compare(frag->name,G_("question/receipt")))
				varname = G_("HEADER_RECEIPT_TO");
			else
			if (!gale_text_compare(frag->name,G_("id/instance")))
				varname = G_("HEADER_AGENT");

			if (varname.l > 0)
			envp[envp_len++] = gale_text_to_local(
				gale_text_concat(3,
					varname,G_("="),frag->value.text));

			if (gale_text_compare(frag->name,G_("message/body")))
			envp[envp_len++] = gale_text_to_local(
				gale_text_concat(4,
					G_("GALE_TEXT_"),
					name,G_("="),frag->value.text));
		}

		if (frag_time == frag->type) {
			struct gale_text time;
			char buf[30];
			struct timeval tv;
			time_t when;
			gale_time_to(&tv,frag->value.time);
			when = tv.tv_sec;
			strftime(buf,30,"%Y-%m-%d %H:%M:%S",localtime(&when));
			time = gale_text_from_latin1(buf,-1);

			if (!gale_text_compare(frag->name,G_("id/time"))) {
				char num[30];
				sprintf(num,"%lu",tv.tv_sec);
				envp[envp_len++] = gale_text_to_local(
					gale_text_concat(3,
						G_("HEADER_TIME"),G_("="),
						gale_text_from_latin1(num,-1)));
			}

			envp[envp_len++] = gale_text_to_local(
				gale_text_concat(4,
					G_("GALE_TIME_"),
					name,G_("="),time));
		}

		if (frag_number == frag->type) {
			char buf[30];
			sprintf(buf,"%d",frag->value.number);
			envp[envp_len++] = gale_text_to_local(
				gale_text_concat(3,
					G_("GALE_NUMBER_"),name,
					G_("="),gale_text_from_latin1(buf,-1)));
		}

		/* Allocate more space for the environment if necessary. */
		if (envp_len >= envp_alloc - 5) {
			char **tmp = envp;
			envp_alloc *= 2;
			gale_create_array(envp,envp_alloc);
			memcpy(envp,tmp,envp_len * sizeof(*envp));
		}

		envp[envp_len++] = tmp;
	}

#ifndef NDEBUG
	/* In debug mode, restart if we get a properly authorized message. */
	if (!gale_text_compare(msg->cat,G_("debug/restart")) && id_sign 
	&&  !gale_text_compare(auth_id_name(id_sign),G_("egnor@ofb.net"))) {
		gale_alert(GALE_NOTICE,"Restarting from debug/restart.",0);
		notify(0,G_("restarting"));
		putenv("GALE_ANNOUNCE=in/restarted");
		gale_restart();
	}
#endif

	/* Give them our key, if they wanted it. */
	if (akd) {
		link_put(client->link,akd);
		rcpt = NULL;
		status = 0;
		goto done;
	}

	/* Terminate the new environment. */
	envp[envp_len] = NULL;

	/* Convert the message body to local format. */
	tmp = gale_text_to_local(body);

	/* Use the extended loaded gsubrc, if present. */
	if (dl_gsubrc2) {
		status = dl_gsubrc2(envp,tmp,strlen(tmp));
		goto done;
	}

	/* Create a pipe to communicate with the gsubrc with. */
	if (pipe(pfd)) {
		gale_alert(GALE_WARNING,"pipe",errno);
		return;
	}

	/* Fork off a subprocess.  This should use gale_exec ... */
	pid = fork();
	if (!pid) {
		const char *rc;

		/* Set the environment. */
		environ = envp;

		/* Close off file descriptors. */
		close(client->socket);
		close(pfd[1]);

		/* Pipe goes to stdin. */
		dup2(pfd[0],0);
		if (pfd[0] != 0) close(pfd[0]);

		/* Use the loaded gsubrc, if we have one. */
		if (dl_gsubrc) exit(dl_gsubrc());

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
	send_message(tmp,tmp + strlen(tmp),pfd[1]);
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

done:
	/* Put the receipt on the queue, if we have one. */
	if (rcpt && !status) link_put(client->link,rcpt);
}

void usage(void) {
	fprintf(stderr,
	"%s\n"
	"usage: gsub [-benkKa] [-f rcprog] [-l rclib] cat\n"
	"flags: -b          Do not beep (normally personal messages beep)\n"
	"       -e          Do not include default subscriptions\n"
	"       -n          Do not fork (default if stdout redirected)\n"
	"       -k          Do not kill other gsub processes\n"
	"       -K          Kill other gsub processes and terminate\n"
	"       -a          Run in \"stealth\" mode\n"
	"       -p state    Announce presence state (eg. \"out/to/lunch\")\n"
	"       -f rcprog   Use rcprog (default gsubrc, if found)\n"
#ifdef HAVE_DLOPEN
	"       -l rclib    Use module (default gsubrc.so, if found)\n" 
#endif
	,GALE_BANNER);
	exit(1);
}

/* Search for and load a shared library with a custom message presenter. */
void load_gsubrc(const char *name) {
#ifdef HAVE_DLOPEN
	const char *rc,*err;
	void *lib;

	rc = dir_search(name ? name : "gsubrc.so",1,dot_gale,sys_dir,NULL);
	if (!rc) {
		if (name) 
			gale_alert(GALE_WARNING,
			"Cannot find specified shared library.",0);
		return;
	}

#ifdef RTLD_LAZY
	lib = dlopen((char *) rc,RTLD_LAZY);
#else
	lib = dlopen((char *) rc,0);
#endif

	if (!lib) {
		while ((err = dlerror())) gale_alert(GALE_WARNING,err,0);
		return;
	}

	dl_gsubrc2 = dlsym(lib,"gsubrc2");
	if (!dl_gsubrc2) {
		dl_gsubrc = dlsym(lib,"gsubrc");
		if (!dl_gsubrc) {
			while ((err = dlerror())) 
				gale_alert(GALE_WARNING,err,0);
			dlclose(lib);
			return;
		}
	}

#else
	if (name) gale_alert(GALE_WARNING,"Dynamic loading not supported.",0);
#endif
}

/* add subscriptions to a list */

void add_subs(struct gale_text *subs,struct gale_text add) {
	if (add.p == NULL) return;
	if (subs->l)
		*subs = gale_text_concat(3,*subs,G_(":"),add);
	else
		*subs = add;
}

/* set presence information */

void set_presence(char *arg) {
	if (strchr(arg,'/'))
		presence = gale_text_from_latin1(arg,-1);
	else
		presence = gale_text_concat(2,G_("in/"),
			gale_text_from_latin1(arg,-1));
}

/* main */

int main(int argc,char **argv) {
	/* Various flags. */
	int opt,do_fork = 0,do_kill = 0;
	const char *rclib = NULL;
	/* Subscription list. */
	struct gale_text serv = null_text;

	/* Initialize the gale libraries. */
	gale_init("gsub",argc,argv);

	/* Figure out who we are. */
	user_id = gale_user();

	/* If we're actually on a TTY, we do things a bit differently. */
	if ((tty = ttyname(1))) {
		/* Truncate the tty name for convenience. */
		char *tmp = strrchr(tty,'/');
#if 0 && defined(HAVE_CURSES)
		char buf[1024];
		/* Find out the terminal type. */
		char *term = getenv("TERM");
		/* Do highlighting, if available. */
		if (term && 1 == tgetent(buf,term)) do_termcap = 1;
#endif
		if (tmp) tty = tmp + 1;
		/* Go into the background; kill other gsub processes. */
		do_fork = do_kill = 1;
	}

	/* Default presence. */
	presence = gale_var(G_("GALE_PRESENCE"));
	if (!presence.l) presence = G_("in/present");

	/* Default subscriptions. */
	add_subs(&serv,gale_var(G_("GALE_SUBS")));
	add_subs(&serv,gale_var(G_("GALE_GSUB")));

	/* Parse command line arguments. */
	while (EOF != (opt = getopt(argc,argv,"benkKaf:l:p:h"))) switch (opt) {
	case 'b': do_beep = 0; break;		/* Do not beep */
	case 'e': serv.l = 0; break;            /* Do not include defaults */
	case 'n': do_fork = 0; break;           /* Do not go into background */
	case 'k': do_kill = 0; break;           /* Do not kill other gsubs */
	case 'K': if (tty) gale_kill(tty,1);    /* *only* kill other gsubs */
	          return 0;
	case 'f': rcprog = optarg; break;       /* Use a wacky gsubrc */
	case 'l': rclib = optarg; break;	/* Use a wacky gsubrc.so */
	case 'a': do_stealth = 1; break;        /* Do not send login/logout */
	case 'p': set_presence(optarg); break;	/* Presence */
	case 'h':                               /* Usage message */
	case '?': usage();
	}

	/* One argument, at most (subscriptions) */
	if (optind < argc - 1) usage();
	if (optind == argc - 1) 
		add_subs(&serv,gale_text_from_local(argv[optind],-1));

	/* We need to subscribe to *something* */
	if (0 == serv.l)
		gale_alert(GALE_ERROR,"No subscriptions specified.",0);

	/* Look for a gsubrc.so */
	load_gsubrc(rclib);

	/* Act as AKD proxy for this particular user. */
	if (!do_stealth)
		add_subs(&serv,id_category(user_id,G_("auth/query"),G_("")));

#ifndef NDEBUG
	/* If in debug mode, listen to debug/ for restart messages. */
	add_subs(&serv,G_("debug/"));
#endif

	/* Open a connection to the server. */
	client = gale_open(serv);

	/* Fork ourselves into the background, unless we shouldn't. */
	if (do_fork) gale_daemon(1);
	trap_signal(SIGHUP);
	trap_signal(SIGTERM);
	trap_signal(SIGINT);
	if (tty) {
		gale_kill(tty,do_kill);
		gale_watch_tty(1);
	}

	/* Send a login message, as needed. */
	if (!do_stealth) {
		struct gale_text announce = gale_var(G_("GALE_ANNOUNCE"));
		if (!announce.l) announce = presence;
		notify(1,announce);
		notify(0,G_("out/disconnected"));
	}

	for (;;) {
		int r = 0;

		while (0 == sig_received && !gale_send(client) 
		   &&  0 == sig_received && !gale_next(client)) {
			struct gale_message *msg;
			while ((msg = link_get(client->link)))
				present_message(msg);
		}

		if (sig_received) {
			switch (sig_received) {
			case SIGHUP: notify(0,G_("out/logout")); break;
			case SIGTERM: notify(0,G_("out/quit")); break;
			case SIGINT: notify(0,G_("out/stopped")); break;
			}
			gale_send(client);
			break;
		}

		/* Retry the server connection. */
		gale_retry(client);
		if (!do_stealth) {
			notify(1,G_("in/reconnected"));
			notify(0,G_("out/disconnected"));
		}
	}

	return 0;
}
