/* gsub.c -- subscription client, outputs messages to the tty, optionally
   sending them through a gsubrc filter. 

   Beware of using this as an example; it's insufficiently Unicode-ized. */

#include "default.h"

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
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/utsname.h>

#ifdef HAVE_DLFCN_H
#define HAVE_DLOPEN
#include <dlfcn.h>
#endif

#define TIMEOUT 2 /* seconds: time to wait for logout notice */

extern char **environ;

struct gale_text rcprog = { NULL, 0 };	/* Filter program name. */
gsubrc_t *dl_gsubrc = NULL;		/* Loaded gsubrc function. */
gsubrc2_t *dl_gsubrc2 = NULL;		/* Extended gsubrc function. */

oop_source_sys *sys;			/* Event source. */
oop_source *source;
struct gale_link *conn;             	/* Physical link. */
struct gale_server *server;		/* Logical connection. */
struct auth_id *user_id;		/* The user. */
struct gale_text presence;		/* Current presence state. */
char *tty;                              /* TTY device */

int do_run_default = 0;			/* Flag to run default_gsubrc */
int do_presence = 0;			/* Should we announce presence? */
int do_keys = 1;			/* Should we answer key requests? */
int do_beep = 1;			/* Should we beep? */
int do_termcap = 0;                     /* Should we highlight headers? */
int sequence = 0;
int is_done = 0;			/* Ready to terminate? */

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
	if (sign) auth_sign(&msg->data,sign,AUTH_SIGN_NORMAL);

	/* For safety's sake, don't leave the old message in place if 
	   encryption fails. */
	if (encrypt && !auth_encrypt(&msg->data,1,&encrypt)) msg = NULL;

	return msg;
}

/* Register login/logout notices with the server. */
void notify(int in,struct gale_text presence) {
	struct gale_text cat;

	if (in) {
		cat = id_category(user_id,G_("notice"),G_("login"));
		link_put(conn,slip(cat,presence,NULL,user_id,NULL));
	} else {
		cat = id_category(user_id,G_("notice"),G_("logout"));
		link_will(conn,slip(cat,presence,NULL,user_id,NULL));
	}
}

/* Halt the main event loop when we finish sending our notices. */
void *on_empty(struct gale_link *link,void *data) {
	is_done = 1;
	gale_alert(GALE_NOTICE,"disconnecting and terminating",0);
	return OOP_HALT;
}

/* Give up trying to send a disconnection notice. */
void *on_timeout(oop_source *source,struct timeval time,void *x) {
	gale_alert(GALE_WARNING,"cannot send logout notice, giving up",0);
	is_done = 1;
	return OOP_HALT;
}

/* When we receive a signal, send termination notices, and prepare to halt. */
void *on_signal(oop_source *source,int sig,void *data) {
	struct timeval tv;

	if (do_presence) switch (sig) {
	case SIGHUP: notify(0,G_("out/logout")); break;
	case SIGTERM: notify(0,G_("out/quit")); break;
	case SIGINT: notify(0,G_("out/stopped")); break;
	}

	gettimeofday(&tv,NULL);
	tv.tv_sec += TIMEOUT;
	source->on_time(source,tv,on_timeout,NULL);
	link_on_empty(conn,on_empty,NULL);
	return OOP_CONTINUE; /* but real soon... */
}

void *on_disconnect(struct gale_server *server,void *data) {
	if (do_presence) {
		notify(1,G_("in/reconnected"));
		notify(0,G_("out/disconnected"));
	}
	return OOP_CONTINUE;
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

/* Take the message passed as an argument and show it to the user, running
   their gsubrc if present, using the default formatter otherwise. */
void *on_message(struct gale_link *link,struct gale_message *msg,void *data) {
	int pfd[2];             /* Pipe file descriptors. */

	/* Lots of crap.  Discussed below, where they're used. */
	struct gale_environ *save = gale_save_environ();
	struct gale_group group;
	struct gale_text body = null_text;
	struct auth_id *id_encrypted = NULL,*id_sign = NULL;
	struct gale_message *rcpt = NULL,*akd = NULL;
	int status = 0;
	pid_t pid;
	char *szbody = NULL;

	/* Decrypt, if necessary. */
	id_encrypted = auth_decrypt(&msg->data);

	/* Verify a signature, if possible. */
	id_sign = auth_verify(&msg->data);

#ifndef NDEBUG
	/* In debug mode, restart if we get a properly authorized message. 
	   Do this before setting environment variables, to avoid "sticky
	   sender syndrome". */
	if (!gale_text_compare(msg->cat,G_("debug/restart")) && id_sign 
	&&  !gale_text_compare(auth_id_name(id_sign),G_("egnor@ofb.net"))) {
		gale_alert(GALE_NOTICE,"restarting from debug/restart.",0);
		notify(0,G_("restarting"));
		gale_set(G_("GALE_ANNOUNCE"),G_("in/restarted"));
		gale_restart();
	}
#endif

	/* Set some variables for gsubrc. */
	gale_set(G_("GALE_CATEGORY"),msg->cat);

	if (id_encrypted)
		gale_set(G_("GALE_ENCRYPTED"),auth_id_name(id_encrypted));

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
		if (do_presence && frag.type == frag_text
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
		if (do_keys && frag.type == frag_text
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

	/* Give them our key, if they wanted it. */
	if (akd) {
		link_put(conn,akd);
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
		gale_close(server);
		close(pfd[1]);

		/* Pipe goes to stdin. */
		dup2(pfd[0],0);
		if (pfd[0] != 0) close(pfd[0]);

		/* Use the loaded gsubrc, if we have one. */
		if (dl_gsubrc) exit(dl_gsubrc());

		/* Look for the file. */
		rc = dir_search(rcprog,1,
		                gale_global->dot_gale,
		                gale_global->sys_dir,
		                null_text);
		if (rc.l) {
			execl(gale_text_to_local(rc),
			      gale_text_to_local(rcprog),
			      NULL);
			gale_alert(GALE_WARNING,gale_text_to_local(rc),errno);
			exit(1);
		}

		/* If we can't find or can't run gsubrc, use default. */
		default_gsubrc(do_beep);
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
	if (rcpt && !status) link_put(conn,rcpt);
	gale_restore_environ(save);
	return OOP_CONTINUE;
}

void usage(void) {
	fprintf(stderr,
	"%s\n"
	"usage: gsub [-habekKnr] [-o id] [-f rcprog] "
#ifdef HAVE_DLOPEN
	"[-l rclib] "
#endif
	"[-p state] cat\n"
	"flags: -h          Display this message\n"
	"       -a          Never announce presence or send receipts\n"
	"       -A          Always announce presence and send receipts\n"
	"       -b          Do not beep (normally personal messages beep)\n"
	"       -e          Do not include default subscriptions\n"
	"       -k          Do not kill other gsub processes\n"
	"       -K          Kill other gsub processes and terminate\n"
	"       -n          Do not fork (default if stdout redirected)\n"
	"       -o id       Listen to messages for id (as well as %s)\n"
	"       -r          Run the default internal gsubrc and exit\n"
	"       -f rcprog   Use rcprog (default gsubrc, if found)\n"
#ifdef HAVE_DLOPEN
	"       -l rclib    Use module (default gsubrc.so, if found)\n" 
#endif
	"       -p state    Announce presence state (eg. \"out/to/lunch\")\n"
	,GALE_BANNER,gale_text_to_local(auth_id_name(user_id)));
	exit(1);
}

/* Search for and load a shared library with a custom message presenter. */
void load_gsubrc(struct gale_text name) {
#ifdef HAVE_DLOPEN
	struct gale_text rc;
	const char *err;
	void *lib;

	rc = dir_search(name.l ? name : G_("gsubrc.so"),1,
	                gale_global->dot_gale,gale_global->sys_dir,null_text);
	if (!rc.l) {
		if (name.l) 
			gale_alert(GALE_WARNING,
			           "cannot find specified shared library.",0);
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
		gale_alert(GALE_WARNING,"dynamic loading not supported.",0);
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

/* listen to another user category */

void add_other(struct gale_text *subs,struct gale_text add) {
	struct auth_id *id;
	init_auth_id(&id,add);
	if (!auth_id_private(id)) {
		struct gale_text err = gale_text_concat(3,
			G_("no private key for \""),
			auth_id_name(id),
			G_("\""));
		gale_alert(GALE_ERROR,gale_text_to_local(err),0);
	}
	add_subs(subs,id_category(id,G_("user"),G_("")));
}

/* set presence information */

void set_presence(char *arg) {
	if (strchr(arg,'.') || strchr(arg,'/'))
		presence = gale_text_from_latin1(arg,-1);
	else
		presence = gale_text_concat(2,G_("in/"),
			gale_text_from_latin1(arg,-1));
}

/* notify the user when a connection is established */

void *on_connected(struct gale_server *server,
	struct gale_text host,struct sockaddr_in addr,
	void *d) 
{
	gale_alert(GALE_NOTICE,
		gale_text_to_local(gale_text_concat(2,
			G_("connected to "),
			gale_connect_text(host,addr))),0);
	return OOP_CONTINUE;
}

/* main */

int main(int argc,char **argv) {
	/* Various flags. */
	int opt,do_fork = 0,do_kill = 0;
	struct gale_text rclib = null_text;
	struct gale_text others,other = null_text;

	/* Subscription list. */
	struct gale_text serv = null_text;

	/* Initialize the gale libraries. */
	gale_init("gsub",argc,argv);
	gale_init_signals(source = oop_sys_source(sys = oop_sys_new()));

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
		/* Announce ourselves. */
		do_presence = 1;
	}

	/* Don't line buffer; we'll flush when we need to. */
	setvbuf(stdout,NULL,_IOFBF,BUFSIZ);

	/* Default presence. */
	presence = gale_var(G_("GALE_PRESENCE"));
	if (!presence.l) presence = G_("in/present");

	/* Default subscriptions. */
	add_subs(&serv,gale_var(G_("GALE_SUBS")));
	add_subs(&serv,gale_var(G_("GALE_GSUB")));

	/* Other IDs to listen to. */
	others = gale_var(G_("GALE_OTHERS"));
	while (gale_text_token(others,',',&other))
		if (0 != other.l) add_other(&serv,other);

	/* Parse command line arguments. */
	while (EOF != (opt = getopt(argc,argv,"haAbenkKro:f:l:p:"))) 
	switch (opt) {
	case 'a': do_presence = do_keys = 0; break;
						/* Stay quiet */
	case 'A': do_presence = do_keys = 1; break;
						/* Don't */
	case 'b': do_beep = 0; break;		/* Do not beep */
	case 'e': serv.l = 0; break;            /* Do not include defaults */
	case 'n': do_fork = 0; break;           /* Do not background */
	case 'k': do_kill = 0; break;           /* Do not kill other gsubs */
	case 'K': if (tty) gale_kill(gale_text_from_local(tty,-1),1);
	          return 0;			/* only kill other gsubs */
	case 'r': do_run_default = 1; break;	/* only run default_gsubrc */
	case 'f': rcprog = gale_text_from_local(optarg,-1); break;       
						/* Use a wacky gsubrc */
	case 'l': rclib = gale_text_from_local(optarg,-1); break;	
						/* Use a wacky gsubrc.so */
	case 'o': add_other(&serv,gale_text_from_local(optarg,-1)); break;
						/* Listen to a different id */
	case 'p': do_presence = 1; set_presence(optarg); 
	          break;			/* Presence */
	case 'h':                               /* Usage message */
	case '?': usage();
	}

	if (do_run_default) {
		default_gsubrc(do_beep);
		return 0;
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
	if (do_keys)
		add_subs(&serv,id_category(user_id,G_("auth/query"),G_("")));

#ifndef NDEBUG
	/* If in debug mode, listen to debug/ for restart messages. */
	add_subs(&serv,G_("debug/"));
#endif

	/* Open a connection to the server. */
	conn = new_link(source);
	server = gale_open(source,conn,serv,null_text,0);
	gale_on_connect(server,on_connected,NULL);

	/* Fork ourselves into the background, unless we shouldn't. */
	if (do_fork) gale_daemon(source,1);
	source->on_signal(source,SIGHUP,on_signal,NULL);
	source->on_signal(source,SIGTERM,on_signal,NULL);
	source->on_signal(source,SIGINT,on_signal,NULL);
	if (tty) {
		gale_kill(gale_text_from_local(tty,-1),do_kill);
		gale_watch_tty(source,1);
	}

	/* Send a login message, as needed. */
	if (do_presence) {
		struct gale_text announce = gale_var(G_("GALE_ANNOUNCE"));
		if (!announce.l) announce = presence;
		notify(1,announce);
		notify(0,G_("out/disconnected"));
	}

	link_on_message(conn,on_message,NULL);
	gale_on_disconnect(server,on_disconnect,NULL);
	while (!is_done) oop_sys_run(sys);

	return 0;
}
