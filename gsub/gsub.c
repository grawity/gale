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

struct gale_text rcprog = { NULL, 0 };	/* Filter program name. */
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
	struct gale_fragment frag;

	/* Create a new message. */
	msg = new_message();
	msg->cat = cat;

	frag.name = G_("message/sender");
	frag.type = frag_text;
	frag.value.text = gale_var(G_("GALE_FROM"));
	gale_group_add(&msg->data,frag);

	frag.name = G_("notice/presence");
	frag.type = frag_text;
	frag.value.text = presence;
	gale_group_add(&msg->data,frag);

	gale_add_id(&msg->data,gale_text_from_latin1(tty,-1));
	if (extra) gale_group_add(&msg->data,*extra);

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
	struct gale_fragment frag;

	frag.name = G_("answer/key");
	frag.type = frag_data;
	export_auth_id(user_id,&frag.value.data,0);

	return slip(id_category(user_id,G_("auth/key"),G_("")),
	            presence,&frag,NULL,NULL);
}

/* Print a user ID, with a default string (like "everyone") for NULL. */
void print_id(struct gale_text id,struct gale_text dfl) {
	gale_print(stdout,0,id.l ? G_(" <") : G_(" *"));
	gale_print(stdout,gale_print_bold,id.l ? id : dfl);
	gale_print(stdout,0,id.l ? G_(">") : G_("*"));
}

/* The default gsubrc implementation. */
void default_gsubrc(void) {
	char buf[80];
	struct gale_text timecode,text,cat = gale_var(G_("GALE_CATEGORY"));
	int count = 0,len = 0,termwid = gale_columns(stdout);

	/* Ignore messages to category /ping */
	text = null_text;
	while (gale_text_token(cat,':',&text)) {
		if (!gale_text_compare(G_("/ping"),
			gale_text_right(text,5)))
			return;
	}

	/* Get the time */
	if (0 == (timecode = gale_var(G_("GALE_TIME_ID_TIME"))).l) {
		char tstr[80];
		time_t when = time(NULL);
		strftime(tstr,sizeof(tstr),"%Y-%m-%d %H:%M:%S",localtime(&when));
		timecode = gale_text_from_local(tstr,-1);
	}

	/* Format return receipts specially */
	if (gale_var(G_("GALE_TEXT_ANSWER_RECEIPT")).l)  {
		struct gale_text from_comment = gale_var(G_("GALE_TEXT_MESSAGE_SENDER"));
		struct gale_text presence = gale_var(G_("GALE_TEXT_NOTICE_PRESENCE"));

		gale_print(stdout,
		gale_print_bold | gale_print_clobber_left,G_("* "));
		gale_print(stdout,0,timecode);

		gale_print(stdout,0,G_(" received:"));
		if (presence.l) {
			gale_print(stdout,0,G_(" "));
			gale_print(stdout,0,presence);
		}
		print_id(gale_var(G_("GALE_SIGNED")),G_("unverified"));
		if (from_comment.l) {
			gale_print(stdout,0,G_(" ("));
			gale_print(stdout,0,from_comment);
			gale_print(stdout,0,G_(")"));
		}
		gale_print(stdout,gale_print_clobber_right,G_(""));
		gale_print(stdout,0,G_("\n"));
		fflush(stdout);
		return;
	}

	gale_print(stdout,gale_print_clobber_left,G_("-"));
	for (len = 0; len < termwid - 3; ++len) gale_print(stdout,0,G_("-"));
	gale_print(stdout,gale_print_clobber_right,G_("-"));
	gale_print(stdout,0,G_("\n"));

	/* Print the header: category, et cetera */
	gale_print(stdout,gale_print_clobber_left,G_("["));
	gale_print(stdout,gale_print_bold,cat);
	gale_print(stdout,0,G_("]"));
	len += 2 + cat.l;

	text = gale_var(G_("GALE_TEXT_MESSAGE_SENDER"));
	if (text.l) {
		gale_print(stdout,0,G_(" from "));
		gale_print(stdout,gale_print_bold,text);
		len += G_(" from ").l + text.l;
	}

	text = gale_var(G_("GALE_TEXT_MESSAGE_RECIPIENT"));
	if (text.l) {
		gale_print(stdout,0,G_(" to "));
		gale_print(stdout,gale_print_bold,text);
		len += G_(" to ").l + text.l;
	}

	if (gale_var(G_("GALE_TEXT_QUESTION_RECEIPT")).l) {
		gale_print(stdout,gale_print_clobber_right,G_(" [rcpt]"));
		len += G_(" [rcpt]").l;
	}

	gale_print(stdout,gale_print_clobber_right,G_(""));
	gale_print(stdout,0,G_("\n"));

	/* Print the message body. */
	while (fgets(buf,sizeof(buf),stdin)) {
		const int attr = gale_print_clobber_right;
		gale_print(stdout,attr,gale_text_from_local(buf,strlen(buf)));
		++count;
	}

	/* Add a final newline, if for some reason the message did not
           contain one. */
	if (count && !strchr(buf,'\n')) gale_print(stdout,0,G_("\n"));

	/* Print the signature information. */
	{
		struct gale_text from_id = gale_var(G_("GALE_SIGNED"));
		struct gale_text to_id = gale_var(G_("GALE_ENCRYPTED"));
		struct gale_text from_comment = 
			gale_var(G_("GALE_TEXT_MESSAGE_SENDER"));
		int len = 0;

		if (from_id.l) len += from_id.l;
		else if (from_comment.l) len += G_("unverified").l;
		else len += G_("anonymous").l;

		if (to_id.l) len += to_id.l;
		else len += G_("everyone").l;

		while (len++ < termwid - 34) gale_print(stdout,0,G_(" "));

		gale_print(stdout,0,G_("--"));
		if (from_comment.l)
			print_id(from_id,G_("unverified"));
		else
			print_id(null_text,G_("anonymous"));

		gale_print(stdout,0,G_(" for"));
		print_id(to_id,G_("everyone"));

		gale_print(stdout,0,G_(" at "));
		gale_print(stdout,gale_print_clobber_right,
			gale_text_right(timecode,-5));
		gale_print(stdout,0,G_(" --"));
		gale_print(stdout,gale_print_clobber_right,G_("\n"));

		if (to_id.l && do_beep) gale_beep(stdout);
	}

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
	struct gale_environ *save = gale_save_environ();
	struct gale_group group;
	struct gale_text body = null_text;
	struct auth_id *id_encrypted = NULL,*id_sign = NULL;
	struct gale_message *rcpt = NULL,*akd = NULL,*msg = NULL;
	int status;
	pid_t pid;
	char *szbody = NULL;

	/* Decrypt, if necessary. */
	id_encrypted = decrypt_message(_msg,&msg);
	if (!msg) {
		char *tmp = gale_malloc(_msg->cat.l + 80);
		sprintf(tmp,"cannot decrypt message on category \"%s\"",
		        gale_text_to_local(_msg->cat));
		gale_alert(GALE_WARNING,tmp,0);
		return;
	}

	/* GALE_CATEGORY: the message category */
	gale_set(G_("GALE_CATEGORY"),_msg->cat);

	if (id_encrypted)
		gale_set(G_("GALE_ENCRYPTED"),auth_id_name(id_encrypted));

	/* Verify a signature, if possible. */
	id_sign = verify_message(msg,&msg);
	if (id_sign)
		gale_set(G_("GALE_SIGNED"),auth_id_name(id_sign));

	/* Go through the message fragments. */
	group = msg->data;
	while (!gale_group_null(group)) {
		struct gale_fragment frag = gale_group_first(group);
		struct gale_text name;
		wch *buf;
		int i;

		group = gale_group_rest(group);

		/* Process receipts, if we do. */
		if (!do_stealth && frag.type == frag_text
		&&  !gale_text_compare(frag.name,G_("question/receipt"))) {
			/* Generate a receipt. */
			struct gale_fragment reply;
			reply.name = G_("answer/receipt");
			reply.type = frag_text;
			reply.value.text = msg->cat;
			rcpt = slip(frag.value.text,
			            presence,&reply,user_id,id_sign);
		}

		/* Handle AKD requests for us. */
		if (!do_stealth && frag.type == frag_text
		&&  !gale_text_compare(frag.name,G_("question/key"))
		&&  !gale_text_compare(frag.value.text,auth_id_name(user_id)))
			akd = send_key();

		/* Save the message body for later. */
		if (frag.type == frag_text
		&&  !gale_text_compare(frag.name,G_("message/body")))
			body = frag.value.text;

		/* Form the name used for environment variables. */
		gale_create_array(buf,frag.name.l);
		for (i = 0; i < frag.name.l; ++i)
			if (isalnum(frag.name.p[i]))
				buf[i] = toupper(frag.name.p[i]);
			else
				buf[i] = '_';
		name.p = buf;
		name.l = frag.name.l;

		/* Create environment variables. */
		if (frag_text == frag.type) {
			struct gale_text varname = null_text;

			if (!gale_text_compare(frag.name,G_("message/subject")))
				varname = G_("HEADER_SUBJECT");
			else 
			if (!gale_text_compare(frag.name,G_("message/sender")))
				varname = G_("HEADER_FROM");
			else 
			if (!gale_text_compare(frag.name,G_("message/recipient")))
				varname = G_("HEADER_TO");
			else
			if (!gale_text_compare(frag.name,G_("question/receipt")))
				varname = G_("HEADER_RECEIPT_TO");
			else
			if (!gale_text_compare(frag.name,G_("id/instance")))
				varname = G_("HEADER_AGENT");

			if (varname.l > 0)
				gale_set(varname,frag.value.text);

			if (gale_text_compare(frag.name,G_("message/body")))
				gale_set(
				gale_text_concat(2,G_("GALE_TEXT_"),name),
				frag.value.text);
		}

		if (frag_time == frag.type) {
			struct gale_text time;
			char buf[30];
			struct timeval tv;
			time_t when;
			gale_time_to(&tv,frag.value.time);
			when = tv.tv_sec;
			strftime(buf,30,"%Y-%m-%d %H:%M:%S",localtime(&when));
			time = gale_text_from_latin1(buf,-1);

			if (!gale_text_compare(frag.name,G_("id/time")))
				gale_set(G_("HEADER_TIME"),
					gale_text_from_number(tv.tv_sec,10,0));

			gale_set(gale_text_concat(2,G_("GALE_TIME_"),name),time);
		}

		if (frag_number == frag.type)
			gale_set(gale_text_concat(2,G_("GALE_NUMBER_"),name),
				gale_text_from_number(frag.value.number,10,0));
	}

#ifndef NDEBUG
	/* In debug mode, restart if we get a properly authorized message. */
	if (!gale_text_compare(msg->cat,G_("debug/restart")) && id_sign 
	&&  !gale_text_compare(auth_id_name(id_sign),G_("egnor@ofb.net"))) {
		gale_alert(GALE_NOTICE,"Restarting from debug/restart.",0);
		notify(0,G_("restarting"));
		gale_set(G_("GALE_ANNOUNCE"),G_("in/restarted"));
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

	/* Convert the message body to local format. */
	szbody = gale_text_to_local(body);

	/* Use the extended loaded gsubrc, if present. */
	if (dl_gsubrc2) {
		status = dl_gsubrc2(environ,szbody,strlen(szbody));
		goto done;
	}

	/* Create a pipe to communicate with the gsubrc with. */
	if (pipe(pfd)) {
		gale_alert(GALE_WARNING,"pipe",errno);
		goto done;
	}

	/* Fork off a subprocess.  This should use gale_exec ... */
	pid = fork();
	if (!pid) {
		struct gale_text rc;

		/* Close off file descriptors. */
		close(client->socket);
		close(pfd[1]);

		/* Pipe goes to stdin. */
		dup2(pfd[0],0);
		if (pfd[0] != 0) close(pfd[0]);

		/* Use the loaded gsubrc, if we have one. */
		if (dl_gsubrc) exit(dl_gsubrc());

		/* Look for the file. */
		rc = dir_search(rcprog,1,dot_gale,sys_dir,null_text);
		if (rc.l) {
			execl(gale_text_to_local(rc),
			      gale_text_to_local(rcprog),
			      NULL);
			gale_alert(GALE_WARNING,gale_text_to_local(rc),errno);
			exit(1);
		}

		/* If we can't find or can't run gsubrc, use default. */
		default_gsubrc();
		exit(0);
	}

	if (pid < 0) gale_alert(GALE_WARNING,"fork",errno);

	/* Send the message to the gsubrc. */
	close(pfd[0]);
	send_message(szbody,szbody + strlen(szbody),pfd[1]);
	close(pfd[1]);

	/* Wait for the gsubrc to terminate. */
	status = gale_wait(pid);

done:
	/* Put the receipt on the queue, if we have one. */
	if (rcpt && !status) link_put(client->link,rcpt);
	gale_restore_environ(save);
}

void usage(void) {
	fprintf(stderr,
	"%s\n"
	"usage: gsub [-hbenkKa] [-f rcprog] [-l rclib] [-p state] cat\n"
	"flags: -h          Display this message\n"
	"       -b          Do not beep (normally personal messages beep)\n"
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
void load_gsubrc(struct gale_text name) {
#ifdef HAVE_DLOPEN
	struct gale_text rc;
	const char *err;
	void *lib;

	rc = dir_search(name.l ? name : G_("gsubrc.so"),1,
	                dot_gale,sys_dir,null_text);
	if (!rc.l) {
		if (name.l) 
			gale_alert(GALE_WARNING,
			           "Cannot find specified shared library.",0);
		return;
	}

#ifdef RTLD_LAZY
	lib = dlopen(gale_text_to_local(rc),RTLD_LAZY);
#else
	lib = dlopen(gale_text_to_local(rc),0);
#endif

	if (!lib) {
		while ((err = dlerror())) gale_alert(GALE_WARNING,err,0);
		return;
	}

	dl_gsubrc2 = (gsubrc2_t *) dlsym(lib,"gsubrc2");
	if (!dl_gsubrc2) {
		dl_gsubrc = (gsubrc_t *) dlsym(lib,"gsubrc");
		if (!dl_gsubrc) {
			while ((err = dlerror())) 
				gale_alert(GALE_WARNING,err,0);
			dlclose(lib);
			return;
		}
	}

#else
	if (name.l)
		gale_alert(GALE_WARNING,"Dynamic loading not supported.",0);
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
	struct gale_text rclib = null_text;
	/* Subscription list. */
	struct gale_text serv = null_text;

	/* Initialize the gale libraries. */
	gale_init("gsub",argc,argv);

	/* Figure out who we are. */
	user_id = gale_user();

	/* Default values. */
	rcprog = G_("gsubrc");

	/* If we're actually on a TTY, we do things a bit differently. */
	if ((tty = ttyname(1))) {
		/* Truncate the tty name for convenience. */
		char *tmp = strrchr(tty,'/');
		if (tmp) tty = tmp + 1;
		/* Go into the background; kill other gsub processes. */
		do_fork = do_kill = 1;
	}

	/* Don't line buffer; we'll flush when we need to. */
	setvbuf(stdout,NULL,_IOFBF,BUFSIZ);

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
	case 'K': if (tty) gale_kill(gale_text_from_local(tty,-1),1);
	          return 0;			/* *only* kill other gsubs */
	case 'f': rcprog = gale_text_from_local(optarg,-1); break;       
						/* Use a wacky gsubrc */
	case 'l': rclib = gale_text_from_local(optarg,-1); break;	
						/* Use a wacky gsubrc.so */
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
		gale_kill(gale_text_from_local(tty,-1),do_kill);
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
		while (0 == sig_received && !gale_send(client) 
		   &&  0 == sig_received && !gale_next(client)) {
			struct gale_message *msg;
			while ((msg = link_get(client->link)))
				present_message(msg);
		}

		if (sig_received) {
			if (!do_stealth) switch (sig_received) {
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
