/* gsub.c -- subscription client, outputs messages to the tty, optionally
   sending them through a gsubrc filter.  */

#include "default.h"

#include "gale/all.h"
#include "gale/gsubrc.h"

#include <time.h>
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

struct gale_location **sub_location = NULL;
int *sub_positive = NULL;
int sub_count = 0,sub_alloc = 0;

int do_run_default = 0;			/* Flag to run default_gsubrc */
int do_presence = 0;			/* Should we announce presence? */
int do_default = 1;			/* Default subscriptions? */
int do_keys = 1;			/* Should we answer key requests? */
int do_termcap = 0;                     /* Should we highlight headers? */
int do_fork = 0;			/* Run in the background? */
int do_kill = 0;			/* Kill other gsub processes? */
int do_chat = 0;			/* Report goings-on? */
int do_verbose = 0;			/* Report everything? */
int sequence = 0;

/* Queue used to serialize message formatting. */
struct gsub_queue { struct gale_message *msg; struct gsub_queue *next; } *queue = NULL;
int is_queueing = 0;

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
	struct gale_location *from,
	struct gale_location *to,
	struct gale_fragment *extra,
	gale_call_packet *func,void *user) 
{
	struct gale_message *msg;
	struct gale_fragment frag;

	/* Create a new message. */
	gale_create(msg);
	msg->data = gale_group_empty();

	gale_create_array(msg->from,2);
	msg->from[0] = from;
	msg->from[1] = NULL;

	gale_create_array(msg->to,2);
	msg->to[0] = to;
	msg->to[1] = NULL;

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
		slip(user_location,notice_location,&frag,in ? on_put : on_will,NULL);
		if (do_verbose && in)
			gale_alert(GALE_NOTICE,gale_text_concat(3,
				G_("reporting presence \""),presence,G_("\"")),0);
	}
}

static void *on_disconnect(struct gale_server *server,void *data) {
	if (do_presence) {
		notify(1,G_("in.reconnected"));
		notify(0,G_("out.disconnected"));
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

/* Define a set of environment variables. */
static void set_list(struct gale_text base,struct gale_location **loc) {
	int i;
	for (i = 0; NULL != loc && NULL != loc[i]; ++i) {
		struct gale_text name = base;
		if (i > 0) name = gale_text_concat(3,
			base,G_("_"),
			gale_text_from_number(1 + i,10,0));
		gale_set(name,gale_location_name(loc[i]));
	}
}

static void *on_receipt(struct gale_text n,struct gale_location *to,void *x) {
	struct gale_fragment reply;
	if (NULL != user_location) {
		reply.name = G_("answer.receipt");
		reply.type = frag_text;
		reply.value.text = gale_location_name(user_location);
		slip(user_location,to,&reply,on_put,NULL);
		if (do_verbose)
			gale_alert(GALE_NOTICE,gale_text_concat(3,
				G_("sending receipt to \""),
				gale_location_name(to),G_("\"")),0);
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

static void *show_message(struct gale_message *);

static void *next_message(void) {
	struct gale_message *msg;

	assert(is_queueing);
	if (NULL == queue) {
		is_queueing = 0;
		return OOP_CONTINUE;
	}

	if (queue->next == queue) {
		msg = queue->msg;
		queue = NULL;
		return show_message(msg);
	}

	msg = queue->next->msg;
	queue->next = queue->next->next;
	return show_message(msg);
}

static void *on_status(int status,void *x) {
	if (WIFSIGNALED(status))
		gale_alert(GALE_WARNING,gale_text_concat(2,
			G_("gsubrc died with signal "),
			gale_text_from_number(WTERMSIG(status),10,0)),0);
	else if (WIFEXITED(status) && WEXITSTATUS(status) > 0)
		gale_alert(GALE_NOTICE,gale_text_concat(2,
			G_("gsubrc returned error "),
			gale_text_from_number(WEXITSTATUS(status),10,0)),0);
	return next_message();
}

/* Take the message passed as an argument and show it to the user, running
   their gsubrc if present, using the default formatter otherwise. */
static void *show_message(struct gale_message *msg) {
	/* Lots of crap.  Discussed below, where they're used. */
	struct gale_environ *save = gale_save_environ();
	struct gale_group group;
	struct gale_fragment f;
	struct gale_text body = null_text;
	int status = 0;
	char *szbody = NULL;

	if (NULL == msg) return next_message();

#ifndef NDEBUG
	/* In debug mode, restart if we get a properly authorized message. 
	   Do this before setting environment variables, to avoid "sticky
	   sender syndrome". */
	if (NULL != msg->to && restart_to_location == msg->to[0]
	&&  NULL != msg->from && restart_from_location == msg->from[0]) {
		gale_alert(GALE_NOTICE,G_("restarting from debug.restart."),0);
		notify(0,G_("restarting"));
		gale_set(G_("GALE_ANNOUNCE"),G_("in.restarted"));
		gale_restart();
	}
#endif

	/* Handle AKD requests for us. */
	/* TODO: Move this to when the message is queued. */
	if (do_keys 
	&&  NULL != key_location
	&&  NULL != user_location
	&&  gale_group_lookup(msg->data,G_("question.key"),frag_text,&f)
	&& !gale_text_compare(f.value.text,gale_location_name(user_location))) {
		f.name = G_("answer/key");
		f.type = frag_data;
		f.value.data = gale_key_raw(gale_key_public(
			gale_location_key(user_location),
			gale_time_now()));
		slip(NULL,key_location,&f,on_put,NULL);
		if (do_verbose) gale_alert(GALE_NOTICE,gale_text_concat(3,
			G_("answering key request for \""),
			gale_location_name(user_location),
			G_("\"")),0);
	} else 
	if (do_verbose
	&& (gale_group_lookup(msg->data,G_("question.key"),frag_text,&f)
	||  gale_group_lookup(msg->data,G_("question/key"),frag_text,&f))) {
		gale_alert(GALE_WARNING,gale_text_concat(3,
			G_("ignoring key request for \""),
			f.value.text,
			G_("\"")),0);
	}

	/* Set some variables for gsubrc. */
	set_list(G_("GALE_FROM"),msg->from);
	set_list(G_("GALE_SENDER"),msg->from);
	set_list(G_("GALE_TO"),msg->to);

	/* Go through the message fragments. */
	group = msg->data;
	while (!gale_group_null(group)) {
		struct gale_fragment frag = gale_group_first(group);
		struct gale_text base,name,value;
		wch *buf;
		int i;

		group = gale_group_rest(group);

		/* Process receipts, if we do. */
		if (do_presence && frag.type == frag_text
		&& !gale_text_compare(frag.name,G_("question.receipt")))
			gale_find_exact_location(source,
				frag.value.text,
				on_receipt,NULL);

		/* Save the message body for later. */
		if (frag.type == frag_text
		&&  !gale_text_compare(frag.name,G_("message/body"))) {
			body = frag.value.text;
			continue;
		}

		/* Form the name used for environment variables. */
		gale_create_array(buf,frag.name.l);
		for (i = 0; i < frag.name.l; ++i)
			if ((frag.name.p[i] >= '0' && frag.name.p[i] <= '9')
			||  (frag.name.p[i] >= 'A' && frag.name.p[i] <= 'Z'))
				buf[i] = frag.name.p[i];
			else if (frag.name.p[i] >= 'a' && frag.name.p[i] <= 'z')
				buf[i] = frag.name.p[i] - 'a' + 'A';
			else
				buf[i] = '_';

		base.p = buf;
		base.l = frag.name.l;

		/* Create environment variables. */
		switch (frag.type) {
		case frag_text:
			base = gale_text_concat(2,G_("GALE_TEXT_"),base);
			value = frag.value.text;
			break;

		case frag_time:
			base = gale_text_concat(2,G_("GALE_TIME_"),base);
			value = gale_time_format(frag.value.time);
#if 0
			{
				char buf[30];
				struct timeval tv;
				time_t when;
				gale_time_to(&tv,frag.value.time);
				when = tv.tv_sec;
				strftime(buf,30,
					"%Y-%m-%d %H:%M:%S",
					localtime(&when));
				value = gale_text_from(NULL,buf,-1);
			}
#endif
			break;

		case frag_number:
			base = gale_text_concat(2,G_("GALE_NUMBER_"),base);
			value = gale_text_from_number(frag.value.number,10,0);
			break;

		case frag_data:
			base = gale_text_concat(2,G_("GALE_DATA_"),base);
			value = G_("(stuff)");
			break;

		default:
			gale_alert(GALE_WARNING,G_("invalid fragment!"),0);
			continue;
		}

		i = 1;
		name = base;
		while (gale_var(name).l > 0)
			name = gale_text_concat(3,base,G_("_"),
				gale_text_from_number(++i,10,0));

		gale_set(name,value);
	}

	/* Set GALE_VERBOSE to the desired verbosity level */
	gale_set(G_("GALE_VERBOSE"),gale_text_from_number(do_verbose,10,0));

	/* Convert the message body to local format. */
	szbody = gale_text_to(gale_global->enc_console,body);

	/* Use the extended loaded gsubrc, if present. */
	if (dl_gsubrc2) {
		status = dl_gsubrc2(environ,szbody,strlen(szbody));
		gale_restore_environ(save);
		return next_message();
	} else {
		struct gale_text rc = null_text;
		int pfd;

		/* Create the gsubrc process. */
		if (NULL == dl_gsubrc) {
			static int first = 1;
			static struct gale_text notice;

			if (0 != rcprog.l) 
				rc = rcprog;
			else 
				rc = dir_search(G_("gsubrc"),1,
					gale_global->dot_gale,
					gale_global->sys_dir,
					null_text);

			if (do_verbose 
			&& (first || gale_text_compare(notice,rc))) {
				if (0 == rc.l)
					gale_alert(GALE_NOTICE,
						G_("using built-in gsubrc"),0);
				else
					gale_alert(GALE_NOTICE,gale_text_concat(
						3,G_("running gsubrc \""),
						rc,G_("\"")),0);
				notice = rc;
				first = 0;
			}
		}

		gale_exec(source,rc,1,&rc,&pfd,NULL,on_gsubrc,on_status,NULL);

		/* Send the message to the gsubrc. */
		if (-1 != pfd) {
			send_message(szbody,szbody + strlen(szbody),pfd);
			close(pfd);
		}

		gale_restore_environ(save);
		return OOP_CONTINUE;
	}
}

static void *on_message(struct gale_message *msg,void *x) {
	struct gsub_queue *entry;
	gale_create(entry);
	entry->msg = msg;

	/* TODO: process AKD requests now */

	if (NULL == queue)
		entry->next = entry;
	else {
		entry->next = queue->next;
		queue->next = entry;
	}
	queue = entry;

	if (is_queueing) {
		if (do_verbose) gale_alert(GALE_NOTICE,
			G_("queueing message for later display"),0);
		return OOP_CONTINUE;
	}

	is_queueing = 1;
	return next_message();
}

static void *on_packet(
	struct gale_link *link,
	struct gale_packet *pkt,void *data) 
{
	gale_unpack_message(source,pkt,on_message,data);
	return OOP_CONTINUE;
}

static void usage(void) {
	fprintf(stderr,
	"%s\n"
	"usage: gsub [-haAekKnqrv] [-f rcprog] "
#ifdef HAVE_DLOPEN
	"[-l rclib] "
#endif
	"[-p state] addr [[-] addr ...]\n"
	"flags: -h          Display this message\n"
	"       -a          Never announce presence or send receipts\n"
	"       -A          Always announce presence and send receipts\n"
	"       -e          Do not include default subscriptions\n"
	"       -k          Do not kill other gsub processes\n"
	"       -K          Kill other gsub processes and terminate\n"
	"       -n          Do not fork (default if stdout redirected)\n"
	"       -q          Extra quiet mode\n"
	"       -v          Extra verbose mode\n"
	"       -r          Run the default internal gsubrc and exit\n"
	"       -f rcprog   Use rcprog (default gsubrc, if found)\n"
#ifdef HAVE_DLOPEN
	"       -l rclib    Use module (default gsubrc.so, if found)\n" 
#endif
	"       -p state    Announce presence state (eg. \"out.to.lunch\")\n"
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
	if (NULL == dl_gsubrc2) {
		dl_gsubrc = (gsubrc_t *) dlsym(lib,"gsubrc");
		if (!dl_gsubrc) {
			while ((err = dlerror())) gale_alert(GALE_WARNING,
				gale_text_from(gale_global->enc_sys,err,-1),0);
			dlclose(lib);
			return;
		}
	}

	if (do_verbose) {
		if (NULL != dl_gsubrc2)
			gale_alert(GALE_NOTICE,gale_text_concat(3,
				G_("using gsubrc2() in \""),name,G_("\"")),0);
		else if (NULL != dl_gsubrc)
			gale_alert(GALE_NOTICE,gale_text_concat(3,
				G_("using gsubrc() in \""),name,G_("\"")),0);
	}

#else
	if (name.l)
		gale_alert(GALE_WARNING,G_("dynamic loading not supported."),0);
#endif
}

static int add_sub(void) {
	if (sub_count == sub_alloc) {
		sub_alloc = sub_alloc ? 2*sub_alloc : 10;
		sub_location = gale_realloc(sub_location,
			sizeof(*sub_location) * sub_alloc);
		sub_positive = gale_realloc(sub_positive,
			sizeof(*sub_positive) * sub_alloc);
	}

	sub_location[sub_count] = NULL;
	sub_positive[sub_count] = 1;
	return sub_count++;
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
	int i,count;
	if (0 != --lookup_count) return OOP_CONTINUE;
	if (do_default) sub_location[add_sub()] = user_location;

#ifndef NDEBUG
	sub_location[add_sub()] = restart_to_location;
#endif

	add_sub(); /* ensure NULL termination */
	count = 0;
	for (i = 0; i != sub_count; ++i)
		if (NULL != sub_location[i]) {
			if (sub_positive[i]
			&& !gale_location_receive_ok(sub_location[i]))
				gale_alert(GALE_WARNING,gale_text_concat(3,
					G_("unauthorized location \""),
					gale_location_name(sub_location[i]),
					G_("\"")),0);

			if (do_verbose) 
				gale_alert(GALE_NOTICE,gale_text_concat(3,
					sub_positive[i] 
						? G_("subscription: \"")
						: G_("filtering out: \""),
					gale_location_name(sub_location[i]),
					G_("\"")),0);

			sub_location[count] = sub_location[i];
			sub_positive[count] = sub_positive[i];
			++count;
		}

	if (0 == count) gale_alert(GALE_ERROR,G_("no subscriptions!"),0);

	/* Fork ourselves into the background, unless we shouldn't. */
	if (do_fork) gale_daemon(source);

	if (do_verbose)
		gale_alert(GALE_NOTICE,gale_text_concat(2,
			(do_fork ? G_("running in background as pid ") 
			         : G_("running in foreground as pid ")),
			gale_text_from_number(getpid(),10,0)),0);

	if (tty) {
		const struct gale_text terminal =
			gale_text_from(gale_global->enc_filesys,tty,-1);
		if (do_verbose && do_kill)
			gale_alert(GALE_NOTICE,gale_text_concat(3,
				G_("killing any other gsub on \""),
				terminal,G_("\"")),0);
		gale_kill(terminal,do_kill);
		gale_watch_tty(source,1);
	}

	routing = gale_pack_subscriptions(sub_location,sub_positive);

	/* Send a login message, as needed. */
	if (do_presence) {
		struct gale_text announce = gale_var(G_("GALE_ANNOUNCE"));
		if (!announce.l) announce = presence;
		notify(1,announce);
		notify(0,G_("out.disconnected"));
	}

	link_on_message(conn,on_packet,NULL);
	gale_on_connect(server,on_reconnect,NULL);
	gale_on_disconnect(server,on_disconnect,NULL);
	return OOP_CONTINUE;
}

static gale_call_location on_subscr_loc,on_static_loc;

static void *on_subscr_loc(struct gale_text n,struct gale_location *l,void *x) {
	return on_static_loc(n,l,&sub_location[(int) x]);
}

static void *on_static_loc(struct gale_text n,struct gale_location *l,void *x) {
	struct gale_location **target = (struct gale_location **) x;
	*target = l;

	if (NULL == l)
		gale_alert(GALE_WARNING,gale_text_concat(3,
			G_("invalid location \""),n,G_("\"")),0);

	if (target == &user_location) {
		lookup_count += 3;
		gale_find_exact_location(source,gale_text_concat(2,
			G_("_gale.notice."),gale_location_name(l)),
			on_static_loc,&notice_location);
		gale_find_exact_location(source,gale_text_concat(2,
			G_("_gale.key."),gale_location_name(l)),
			on_static_loc,&key_location);
		gale_find_exact_location(source,gale_text_concat(2,
			G_("_gale.query."),gale_location_name(l)),
			on_subscr_loc,(void *) add_sub());
	}

	return on_complete();
}

static void *on_connected(
	struct gale_server *server,
	struct gale_text host,struct sockaddr_in addr,void *d) 
{
	if (do_chat) gale_alert(GALE_NOTICE,gale_text_concat(2,
		G_("connected to "),
		gale_connect_text(host,addr)),0);
	return on_complete();
}

static void argument(struct gale_text arg,int *positive,int chat) {
	if (!gale_text_compare(arg,G_("-")))
		*positive = !*positive;
	else if (arg.l > 0 && gale_text_compare(arg,G_("+"))) {
		const int i = add_sub();

		if (!gale_text_compare(gale_text_left(arg,1),G_("-")))
			gale_alert(GALE_WARNING,gale_text_concat(3,
				G_("leading dash on \""),
				arg,G_("\" does NOT mean unsubscription")),0);

		sub_positive[i] = *positive;
		++lookup_count;
		gale_find_location(source,arg,on_subscr_loc,(void *) i);

		if (chat) gale_alert(GALE_NOTICE,gale_text_concat(3,
			*positive ? G_("subscription: \"")
			          : G_("filtering out: \""),
			arg,G_("\"")),0);

		*positive = 1;
	}
}

/* main */
int main(int argc,char **argv) {
	/* Various flags. */
	int opt,positive = 1;
	struct gale_text rclib = null_text;
	struct gale_text subs,line;

	/* Initialize the gale libraries. */
	gale_init("gsub",argc,argv);
	gale_init_signals(source = oop_sys_source(sys = oop_sys_new()));

	/* If we're actually on a TTY, we do things a bit differently. */
	if ((tty = ttyname(1))) {
		/* Truncate the tty name for convenience. */
		char *tmp = strrchr(tty,'/');
		if (tmp) tty = tmp + 1;
		/* Go into the background; kill other gsub processes. */
		do_fork = do_kill = 1;
		/* Announce ourselves. */
		do_presence = 1;
		/* Let the user know what's happenning. */
		do_chat = 1;
	}

	/* Don't line buffer; we'll flush when we need to. */
	setvbuf(stdout,NULL,_IOFBF,BUFSIZ);

	/* Default presence. */
	presence = gale_var(G_("GALE_PRESENCE"));
	if (!presence.l) presence = G_("in.present");

	/* Parse command line arguments. */
	while (EOF != (opt = getopt(argc,argv,"dDhaAenkKqvrf:l:p:"))) {
	struct gale_text str = !optarg ? null_text :
		gale_text_from(gale_global->enc_cmdline,optarg,-1);
	switch (opt) {
	case 'd': ++gale_global->debug_level; break;
	case 'D': gale_global->debug_level += 5; break;

	case 'a': /* Anonymous */
		if (do_chat) gale_alert(GALE_NOTICE,
			G_("disabling presence and receipts"),0);
		do_presence = do_keys = 0; 
		break;

	case 'A': /* Not */
		if (do_chat) gale_alert(GALE_NOTICE,
			G_("enabling presence and receipts"),0);
		do_presence = do_keys = 1; 
		break;

	case 'e': /* Do not include defaults */
		if (do_chat) gale_alert(GALE_NOTICE,
			G_("skipping default subscriptions"),0);
		do_default = 0; 
		break;

	case 'n': /* Do not background */
		if (do_chat) gale_alert(GALE_NOTICE,
			G_("running in foreground"),0);
		do_fork = do_kill = 0; 
		break; 

	case 'k': /* Do not kill other gsubs */
		do_kill = 0; break;           

	case 'K': /* only kill other gsubs */
		if (tty) gale_kill(gale_text_from(
			gale_global->enc_filesys,tty,-1),1);
		return 0;			

	case 'r': /* only run default_gsubrc */
		do_run_default = 1;
		break;

	case 'f': /* Use a wacky gsubrc */
		rcprog = str;	                
		if (do_chat) gale_alert(GALE_NOTICE,gale_text_concat(3,
			G_("will use gsubrc \""),rcprog,G_("\"")),0);
		break;

	case 'l': /* Use a wacky gsubrc.so */
		rclib = str;
		if (do_chat) gale_alert(GALE_NOTICE,gale_text_concat(3,
			G_("will load module \""),rclib,G_("\"")),0);
		break;

	case 'p': /* Presence */
		do_presence = 1; presence = str;
		if (do_chat) gale_alert(GALE_NOTICE,gale_text_concat(3,
			G_("presence set to \""),presence,G_("\"")),0);
		break;

	case 'q': /* Quiet */
		do_chat = 0;
		do_verbose = 0;
		break;

	case 'v': /* Verbose */
		do_chat = 0;
		do_verbose = 1;
		break;

	case 'h': /* Usage message */
	case '?': usage();
	} }

	if (do_run_default) {
		if (do_verbose) 
			gale_alert(GALE_NOTICE,G_("running built-in gsubrc"),0);
		default_gsubrc();
		return 0;
	}

	if (do_verbose && !do_presence)
		gale_alert(GALE_NOTICE,G_("presence is disabled"),0);

	subs = null_text;
	if (do_default) subs = gale_var(G_("GALE_SUBSCRIBE"));
	if (0 != subs.l) {
		do_default = 0;
		line = null_text;
		while (gale_text_token(subs,'\n',&line)) {
			struct gale_text space = null_text;
			while (gale_text_token(line,' ',&space)) {
				struct gale_text tab = null_text;
				while (gale_text_token(space,'\t',&tab))
					argument(tab,&positive,0);
			}
		}
	}

	if (!positive) {
		gale_alert(GALE_WARNING,G_("trailing - in GALE_SUBSCRIBE"),0);
		positive = 1;
	}

	while (argc != optind)
		argument(gale_text_from(
			gale_global->enc_cmdline,
			argv[optind++],-1),&positive,do_chat);

	if (!positive)
		gale_alert(GALE_WARNING,G_("trailing - in arguments"),0);

#ifndef NDEBUG
	lookup_count += 2;
	gale_find_exact_location(source,G_("egnor@ofb.net"),
		on_static_loc,&restart_from_location);
	gale_find_exact_location(source,G_("debug.restart@gale.org"),
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

	oop_sys_run(sys);
	return 0;
}
