/* gsub.c -- subscription client, outputs messages to the tty, optionally
   sending them through a gsubrc filter.  */

#include "default.h"

#include "gale/all.h"
#include "gale/gsubrc.h"

#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <assert.h>
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
struct gale_text presence;		/* Current presence state. */
struct gale_text routing;
char *tty;                              /* TTY device */

struct gale_location *user_location = NULL;
struct gale_location *notice_location = NULL;
struct gale_location *key_location = NULL;

#ifndef NDEBUG
struct gale_location *restart_from_location,*restart_to_location;
#endif

int lookup_count = 1;

struct sub { 
	struct gale_location *loc; 
	int positive; 
	struct sub *next; 
};

struct sub *subs = NULL;

int do_run_default = 0;			/* Flag to run default_gsubrc */
int do_presence = 0;			/* Should we announce presence? */
int do_default = 1;			/* Default subscriptions? */
int do_keys = 1;			/* Should we answer key requests? */
int do_termcap = 0;                     /* Should we highlight headers? */
int do_fork = 0;			/* Run in the background? */
int do_kill = 0;			/* Kill other gsub processes? */
int do_stop = 0;
int sequence = 0;

/* Send a message once it's all packed. */
static void *on_put(struct gale_packet *packet,void *user) {
	link_put(conn,packet);
	return OOP_CONTINUE;
}

static void *on_will(struct gale_packet *packet,void *user) {
	link_will(conn,packet);
	return OOP_CONTINUE;
}

/* Generate a trivial little message with the given category.  Used for
   return receipts, login/logout notifications, and such. */
static void slip(
	struct gale_location *to,
	struct gale_fragment *extra,
	gale_call_packet *func,void *user) 
{
	struct gale_message *msg;
	struct gale_fragment frag;

	/* Create a new message. */
	gale_create(msg);
	msg->data = gale_group_empty();

	gale_create_array(msg->to,2);
	msg->to[0] = to;
	msg->to[1] = NULL;

	gale_create_array(msg->from,2);
	msg->from[0] = user_location;
	msg->from[1] = NULL;

	frag.name = G_("message/sender");
	frag.type = frag_text;
	frag.value.text = gale_var(G_("GALE_NAME"));
	gale_group_add(&msg->data,frag);

	frag.name = G_("notice/presence");
	frag.type = frag_text;
	frag.value.text = presence;
	gale_group_add(&msg->data,frag);

	gale_add_id(&msg->data,gale_text_from(gale_global->enc_filesys,tty,-1));
	if (NULL != extra) gale_group_replace(&msg->data,*extra);
	gale_pack_message(source,msg,func,user);
}

/* Register login/logout notices with the server. */
static void notify(int in,struct gale_text presence) {
	if (NULL != notice_location) {
		struct gale_fragment frag;
		frag.name = G_("notice/presence");
		frag.type = frag_text;
		frag.value.text = presence;
		slip(notice_location,&frag,in ? on_put : on_will,NULL);
	}
}

/* Halt the main event loop when we finish sending our notices. */
static void *on_empty(struct gale_link *link,void *data) {
	gale_alert(GALE_NOTICE,G_("disconnecting and terminating"),0);
	do_stop = 1;
	return OOP_HALT;
}

/* Give up trying to send a disconnection notice. */
static void *on_timeout(oop_source *source,struct timeval time,void *x) {
	gale_alert(GALE_WARNING,G_("cannot send logout notice, giving up"),0);
	do_stop = 1;
	return OOP_HALT;
}

/* When we receive a signal, send termination notices, and prepare to halt. */
static void *on_signal(oop_source *source,int sig,void *data) {
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

static void *on_disconnect(struct gale_server *server,void *data) {
	if (do_presence) {
		notify(1,G_("in/reconnected"));
		notify(0,G_("out/disconnected"));
	}
	return OOP_CONTINUE;
}

/* Transmit a message body to a gsubrc process. */
static void send_message(char *body,char *end,int fd) {
	char *tmp;

	while (body != end) {
		/* Write data up to a newline. */
		tmp = memchr(body,'\r',end - body);
		if (!tmp) tmp = end;
		while (body != tmp) {
			int r = write(fd,body,tmp - body);
			if (r <= 0) {
				if (errno != EPIPE)
					gale_alert(GALE_WARNING,G_("write"),errno);
				return;
			}
			body += r;
		}

		/* Translate CRLF to NL. */
		if (tmp != end) {
			if (write(fd,"\n",1) != 1) {
				gale_alert(GALE_WARNING,G_("write"),errno);
				return;
			}
			++tmp;
			if (tmp != end && *tmp == '\n') ++tmp;
		}
		body = tmp;
	}
}

/* Create a comma-separated list of locations. */
static struct gale_text comma_list(struct gale_location **loc) {
	struct gale_text list = null_text;
	if (NULL != loc && NULL != *loc) {
		list = gale_location_name(*loc);
		while (NULL != *++loc)
			list = gale_text_concat(3,
				list,G_(","),
				gale_location_name(*loc));
	}
	return list;
}

static void *on_receipt(struct gale_text n,struct gale_location *to,void *x) {
	struct gale_fragment reply;
	if (NULL != user_location) {
		reply.name = G_("answer/receipt");
		reply.type = frag_text;
		reply.value.text = gale_location_name(user_location);
		slip(to,&reply,on_put,NULL);
	}
	return OOP_CONTINUE;
}

static int on_gsubrc(int count,const struct gale_text *args,void *user) {
	/* Use the loaded gsubrc, if we have one. */
	if (NULL != dl_gsubrc) return dl_gsubrc();

	/* If we can't find or can't run gsubrc, use default. */
	default_gsubrc();
	return 0;
}

/* Take the message passed as an argument and show it to the user, running
   their gsubrc if present, using the default formatter otherwise. */
static void *on_message(struct gale_message *msg,void *data) {
	/* Lots of crap.  Discussed below, where they're used. */
	struct gale_environ *save = gale_save_environ();
	struct gale_group group;
	struct gale_text body = null_text;
	int status = 0;
	char *szbody = NULL;

	if (NULL == msg) return OOP_CONTINUE;

#ifndef NDEBUG
	/* In debug mode, restart if we get a properly authorized message. 
	   Do this before setting environment variables, to avoid "sticky
	   sender syndrome". */
	if (NULL != msg->to && restart_to_location == msg->to[0]
	&&  NULL != msg->from && restart_from_location == msg->from[0]) {
		gale_alert(GALE_NOTICE,G_("restarting from debug/restart."),0);
		notify(0,G_("restarting"));
		gale_set(G_("GALE_ANNOUNCE"),G_("in/restarted"));
		gale_restart();
	}
#endif

	/* Set some variables for gsubrc. */
	gale_set(G_("GALE_FROM"),comma_list(msg->from));
	gale_set(G_("GALE_TO"),comma_list(msg->to));

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
		&& !gale_text_compare(frag.name,G_("question.receipt")))
			gale_find_exact_location(source,
				frag.value.text,
				on_receipt,NULL);

		/* Handle AKD requests for us. */
		if (do_keys 
		&&  NULL != key_location
		&&  NULL != user_location
		&&  frag.type == frag_text
		&& !gale_text_compare(frag.name,G_("question.key"))
		&& !gale_text_compare(frag.value.text,
		                      gale_location_name(user_location))) {
			struct gale_fragment frag;
			frag.name = G_("answer.key");
			frag.type = frag_data;
			frag.value.data = gale_key_raw(gale_key_public(
				gale_location_key(user_location),
				gale_time_now()));
			slip(key_location,&frag,on_put,NULL);
			goto done;
		}

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
		if (frag_text == frag.type
		&&  gale_text_compare(frag.name,G_("message/body")))
			gale_set(gale_text_concat(2,G_("GALE_TEXT_"),name),
			         frag.value.text);

		if (frag_time == frag.type) {
			char buf[30];
			struct timeval tv;
			time_t when;
			gale_time_to(&tv,frag.value.time);
			when = tv.tv_sec;
			strftime(buf,30,"%Y-%m-%d %H:%M:%S",localtime(&when));
			gale_set(gale_text_concat(2,G_("GALE_TIME_"),name),
			         gale_text_from(NULL,buf,-1));
		}

		if (frag_number == frag.type)
			gale_set(gale_text_concat(2,G_("GALE_NUMBER_"),name),
				gale_text_from_number(frag.value.number,10,0));

		if (frag_data == frag.type)
			gale_set(gale_text_concat(2,G_("GALE_DATA_"),name),
				G_("(stuff)"));
	}

	/* Convert the message body to local format. */
	szbody = gale_text_to(gale_global->enc_console,body);

	/* Use the extended loaded gsubrc, if present. */
	if (dl_gsubrc2) {
		status = dl_gsubrc2(environ,szbody,strlen(szbody));
		goto done;
	}

	/* Create the gsubrc process. */
	{
		struct gale_text rc = null_text;
		int pfd;

		if (NULL == dl_gsubrc) rc = dir_search(rcprog,1,
			gale_global->dot_gale,
			gale_global->sys_dir,
			null_text);

		gale_exec(source,rc,1,&rc,&pfd,NULL,on_gsubrc,NULL,NULL);

		/* Send the message to the gsubrc. */
		if (-1 != pfd) {
			send_message(szbody,szbody + strlen(szbody),pfd);
			close(pfd);
		}
	}

done:
	gale_restore_environ(save);
	return OOP_CONTINUE;
}

static void *on_packet(struct gale_link *link,struct gale_packet *pkt,void *data) {
	gale_unpack_message(source,pkt,on_message,data);
	return OOP_CONTINUE;
}

static void usage(void) {
	fprintf(stderr,
	"%s\n"
	"usage: gsub [-haAekKnr] [-f rcprog] "
#ifdef HAVE_DLOPEN
	"[-l rclib] "
#endif
	"[-p state] cat\n"
	"flags: -h          Display this message\n"
	"       -a          Never announce presence or send receipts\n"
	"       -A          Always announce presence and send receipts\n"
	"       -e          Do not include default subscriptions\n"
	"       -k          Do not kill other gsub processes\n"
	"       -K          Kill other gsub processes and terminate\n"
	"       -n          Do not fork (default if stdout redirected)\n"
	"       -r          Run the default internal gsubrc and exit\n"
	"       -f rcprog   Use rcprog (default gsubrc, if found)\n"
#ifdef HAVE_DLOPEN
	"       -l rclib    Use module (default gsubrc.so, if found)\n" 
#endif
	"       -p state    Announce presence state (eg. \"out/to/lunch\")\n"
	,GALE_BANNER);
	exit(1);
}

/* Search for and load a shared library with a custom message presenter. */
static void load_gsubrc(struct gale_text name) {
#ifdef HAVE_DLOPEN
	struct gale_text rc;
	const char *err;
	void *lib;

	rc = dir_search(name.l ? name : G_("gsubrc.so"),1,
	                gale_global->dot_gale,gale_global->sys_dir,null_text);
	if (!rc.l) {
		if (name.l) 
			gale_alert(GALE_WARNING,
			           G_("cannot find specified shared library."),0);
		return;
	}

#ifdef RTLD_LAZY
	lib = dlopen(gale_text_to(gale_global->enc_filesys,rc),RTLD_LAZY);
#else
	lib = dlopen(gale_text_to(gale_global->enc_filesys,rc),0);
#endif

	if (!lib) {
		while ((err = dlerror())) gale_alert(GALE_WARNING,
			gale_text_from(gale_global->enc_sys,err,-1),0);
		return;
	}

	dl_gsubrc2 = (gsubrc2_t *) dlsym(lib,"gsubrc2");
	if (!dl_gsubrc2) {
		dl_gsubrc = (gsubrc_t *) dlsym(lib,"gsubrc");
		if (!dl_gsubrc) {
			while ((err = dlerror())) gale_alert(GALE_WARNING,
				gale_text_from(gale_global->enc_sys,err,-1),0);
			dlclose(lib);
			return;
		}
	}

#else
	if (name.l)
		gale_alert(GALE_WARNING,G_("dynamic loading not supported."),0);
#endif
}

static void add_sub(int positive,struct gale_location *loc) {
	struct sub *sub;
	if (NULL == loc) return;
	if (!gale_location_receive_ok(loc))
		gale_alert(GALE_WARNING,gale_text_concat(3,
			G_("no private key for \""),
			gale_location_name(loc),
			G_("\"")),0);
	gale_create(sub);
	sub->loc = loc;
	sub->next = subs;
	sub->positive = positive;
	subs = sub;
}

static void *on_reconnect(
	struct gale_server *server,
	struct gale_text host,struct sockaddr_in addr,void *d) 
{
	assert(0 != routing.l);
	link_subscribe(conn,routing);
	return OOP_CONTINUE;
}

static void *on_complete() {
	struct sub *s;
	struct gale_location **list;
	int count,*positive;

	if (0 != --lookup_count) return OOP_CONTINUE;

	if (do_default) add_sub(1,user_location);
	/* if (do_keys) add_sub(1,key_query_location); */
#ifndef NDEBUG
	add_sub(1,restart_to_location);
#endif

	if (NULL == subs) gale_alert(GALE_ERROR,G_("no subscriptions!"),0);

	/* Fork ourselves into the background, unless we shouldn't. */
	if (do_fork) gale_daemon(source);
	source->on_signal(source,SIGHUP,on_signal,NULL);
	source->on_signal(source,SIGTERM,on_signal,NULL);
	source->on_signal(source,SIGINT,on_signal,NULL);
	if (tty) {
		gale_kill(gale_text_from(gale_global->enc_filesys,tty,-1),do_kill);
		gale_watch_tty(source,1);
	}

	count = 0;
	for (s = subs; NULL != s; s = s->next) ++count;

	gale_create_array(list,1 + count);
	gale_create_array(positive,count);

	count = 0;
	for (s = subs; NULL != s; s = s->next) {
		list[count] = s->loc;
		positive[count] = s->positive;
		++count;
	}
	list[count] = NULL;
	routing = gale_pack_subscriptions(list,positive);

	/* Send a login message, as needed. */
	if (do_presence) {
		struct gale_text announce = gale_var(G_("GALE_ANNOUNCE"));
		if (!announce.l) announce = presence;
		notify(1,announce);
		notify(0,G_("out/disconnected"));
	}

	link_on_message(conn,on_packet,NULL);
	gale_on_connect(server,on_reconnect,NULL);
	gale_on_disconnect(server,on_disconnect,NULL);
	return OOP_CONTINUE;
}

static void *on_subscr_loc(struct gale_text n,struct gale_location *l,void *x) {
	if (NULL == l) 
		gale_alert(GALE_WARNING,gale_text_concat(3,
			G_("could not find \""),n,G_("\"")),0);
	else
		add_sub((int) x,l);
	return on_complete();
}

static void *on_static_loc(struct gale_text n,struct gale_location *l,void *x) {
	struct gale_location **target = (struct gale_location **) x;
	*target = l;
	if (target == &user_location) {
		lookup_count += 3;
		gale_find_exact_location(source,gale_text_concat(2,
			G_("_notice."),gale_location_name(l)),
			on_static_loc,&notice_location);
		gale_find_exact_location(source,gale_text_concat(2,
			G_("_gale.key."),gale_location_name(l)),
			on_static_loc,&key_location);
		gale_find_exact_location(source,gale_text_concat(2,
			G_("_gale.query."),gale_location_name(l)),
			on_subscr_loc,(void *) 1);
	}

	return on_complete();
}

static void *on_connected(
	struct gale_server *server,
	struct gale_text host,struct sockaddr_in addr,void *d) 
{
	gale_alert(GALE_NOTICE,gale_text_concat(2,
		G_("connected to "),
		gale_connect_text(host,addr)),0);
	return on_complete();
}

/* main */
int main(int argc,char **argv) {
	/* Various flags. */
	int opt;
	struct gale_text rclib = null_text;
	struct gale_text subs = null_text;

	/* Initialize the gale libraries. */
	gale_init("gsub",argc,argv);
	gale_init_signals(source = oop_sys_source(sys = oop_sys_new()));

	/* Default values. */
	rcprog = G_("cheeserc");

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

	/* Parse command line arguments. */
	while (EOF != (opt = getopt(argc,argv,"dDhaAenkKr:f:l:p:"))) {
	struct gale_text str = !optarg ? null_text :
		gale_text_from(gale_global->enc_cmdline,optarg,-1);
	switch (opt) {
	case 'd': ++gale_global->debug_level; break;
	case 'D': gale_global->debug_level += 5; break;
	case 'a': do_presence = do_keys = 0; break;
						/* Stay quiet */
	case 'A': do_presence = do_keys = 1; break;
						/* Don't */
	case 'e': do_default = 0; break;        /* Do not include defaults */
	case 'n': do_fork = do_kill = 0; break; /* Do not background */
	case 'k': do_kill = 0; break;           /* Do not kill other gsubs */
	case 'K': if (tty) gale_kill(gale_text_from(
			gale_global->enc_filesys,tty,-1),1);
	          return 0;			/* only kill other gsubs */
	case 'r': do_run_default = 1; break;	/* only run default_gsubrc */
	case 'f': rcprog = str;	break;          /* Use a wacky gsubrc */
	case 'l': rclib = str; break;	        /* Use a wacky gsubrc.so */
	case 'p': do_presence = 1; presence = str;
	          break;			/* Presence */
	case 'h':                               /* Usage message */
	case '?': usage();
	} }

	if (do_run_default) {
		default_gsubrc();
		return 0;
	}

	if (do_default) subs = gale_var(G_("GALE_CHEESEBALL"));

	while (argc != optind)
		subs = gale_text_concat(3,subs,
			(subs.l ? G_(",") : null_text),
			gale_text_from(
				gale_global->enc_cmdline,
				argv[optind++],-1));

	if (0 != subs.l) {
		struct gale_text sub = null_text;
		while (gale_text_token(subs,',',&sub)) {
			int positive = 1;
			if (!gale_text_compare(G_("-"),gale_text_left(sub,1))) {
				positive = 0;
				sub = gale_text_right(sub,-1);
			}

			++lookup_count;
			gale_find_location(source,sub,
				on_subscr_loc,(void *) positive);
		}
		do_default = 0;
	}

#ifndef NDEBUG
	lookup_count += 2;
	gale_find_location(source,G_("egnor@ofb.net"),
		on_static_loc,&restart_from_location);
	gale_find_location(source,G_("debug.restart@gale.org"),
		on_static_loc,&restart_to_location);
#endif

	++lookup_count;
	gale_find_default_location(source,on_static_loc,&user_location);

	/* Look for a gsubrc.so */
	load_gsubrc(rclib);

	/* Open a connection to the server. */
	conn = new_link(source);
	routing = null_text;
	server = gale_make_server(source,conn,null_text,0);
	gale_on_connect(server,on_connected,NULL);

	while (!do_stop) oop_sys_run(sys);
	return 0;
}
