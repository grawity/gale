#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <syslog.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/utsname.h>

#include "gale/all.h"
#include "connect.h"
#include "server.h"

const char *server_id;

static int debug_level;
static int listener,port = 8413;
static struct connect *list = NULL;
static struct attach *try = NULL;

void dprintf(int level,const char *fmt,...) {
	va_list ap;
	if (level >= debug_level) return;
	va_start(ap,fmt);
	vfprintf(stderr,fmt,ap);
	va_end(ap);
}

static void early(char *s) {
	char *msg = strerror(errno);
	syslog(LOG_ERR,"fatal error (%s): %s",s,msg);
	fprintf(stderr,"fatal error (%s): %s\r\n",s,msg);
	exit(1);
}

static void later(char *s) {
	char *msg = strerror(errno);
	syslog(LOG_ERR,"error (%s): %s",s,msg);
	dprintf(0,"error (%s): %s\n",s,msg);
}

static void add_connect(int fd) {
	struct connect *conn = new_connect(fd,fd,20,262144);
	conn->next = list;
	list = conn;
}

static void incoming(void) {
	struct sockaddr_in sin;
	int len = sizeof(sin);
	int one = 1;
	int newfd = accept(listener,(struct sockaddr *) &sin,&len);
	if (newfd < 0) {
		later("accept");
		return;
	}
	dprintf(2,"[%d] new connection\n",newfd);
	setsockopt(newfd,SOL_SOCKET,SO_KEEPALIVE,&one,sizeof(one));
	add_connect(newfd);
}

static void loop(void) {
	fd_set rfd,wfd;
	int ret;
	struct connect *ptr,*prev;
	struct attach *att,*aprev;
	struct timeval now,timeo;
	struct timezone tz;

	gettimeofday(&now,&tz);
	for(;;) {
		FD_ZERO(&rfd);
		FD_ZERO(&wfd);
		FD_SET(listener,&rfd);
		timeo.tv_sec = LONG_MAX;
		timeo.tv_usec = 0;
		for (ptr = list; ptr; ptr = ptr->next)
			pre_select(ptr,&rfd,&wfd);
		for (att = try; att; att = att->next)
			attach_select(att,&wfd,&now,&timeo);

		if (debug_level > 5) {
			int i;
			for (i = 0; i < FD_SETSIZE; ++i) {
				if (FD_ISSET(i,&rfd))
				dprintf(5,"--> waiting for [%d] read\n",i);
				if (FD_ISSET(i,&wfd))
				dprintf(5,"--> waiting for [%d] write\n",i);
			}
		}

		if (timeo.tv_sec == LONG_MAX) {
			dprintf(4,"--> select: now = %d.%06d, no timeout\n",
		                now.tv_sec,now.tv_usec);
			ret = select(FD_SETSIZE,HPINT &rfd,HPINT &wfd,
			             NULL,NULL);
		} else {
			dprintf(4,"--> select: now = %d.%06d, to = %d.%06d\n",
		        now.tv_sec,now.tv_usec,timeo.tv_sec,timeo.tv_usec);
			ret = select(FD_SETSIZE,HPINT &rfd,HPINT &wfd,
			             NULL,&timeo);
		}
		if (ret == 0) {
			FD_ZERO(&rfd);
			FD_ZERO(&wfd);
			dprintf(4,"--> select: timed out\n");
		}
		if (ret < 0) {
			later("select");
			continue;
		}

		gettimeofday(&now,&tz);

		if (debug_level > 5) {
			int i;
			for (i = 0; i < FD_SETSIZE; ++i) {
				if (FD_ISSET(i,&rfd))
					dprintf(5,"--> [%d] for reading\n",i);
				if (FD_ISSET(i,&wfd))
					dprintf(5,"--> [%d] for writing\n",i);
			}
		}

		if (FD_ISSET(listener,&rfd))
			incoming();
		aprev = NULL; att = try;
		while (att != NULL) {
			int fd = select_attach(att,&wfd,&now);
			if (fd == -1) {
				aprev = att;
				att = att->next;
				continue;
			}
			add_connect(fd);
			list->retry = att;
			link_subscribe(list->link,att->subs);
			subscribe_connect(list,att->subs);
			if (aprev)
				att = aprev->next = att->next;
			else
				att = try = att->next;
		}
		prev = NULL; ptr = list;
		while (ptr != NULL)
			if (post_select(ptr,&rfd,&wfd)) {
				dprintf(2,"[%d] lost connection\n",ptr->rfd);
				att = ptr->retry;
				ptr->retry = NULL;
				if (prev) {
					prev->next = ptr->next;
					free_connect(ptr);
					ptr = prev->next;
				} else {
					list = ptr->next;
					free_connect(ptr);
					ptr = list;
				}
				if (att) {
					att->next = try;
					try = att;
				}
			} else {
				prev = ptr;
				ptr = ptr->next;
			}
	}
}

static void add_link(char *arg) {
	char *at = strrchr(arg,'@');
	struct attach *att = new_attach();
	if (at) {
		att->subs = gale_strndup(arg,at - arg);
		att->server = gale_strdup(at + 1);
	} else {
		att->subs = gale_strdup(arg);
	}
	att->next = try;
	try = att;
}

static void usage(void) {
	fprintf(stderr,
	"usage: server -[dD] [-p port] [-l server]\n"
	"flags: -d       Increment debugging level by one\n"
	"       -D       Increment debugging level by five\n");
	exit(1);
}

int main(int argc,char *argv[]) {
	struct sockaddr_in sin;
	struct utsname un;
	int one = 1,opt;
	char *tmp;

	srand48(time(NULL) ^ getpid());

	debug_level = 0;
	while ((opt = getopt(argc,argv,"dDp:l:")) != EOF) switch (opt) {
	case 'd': ++debug_level; break;
	case 'D': debug_level += 5; break;
	case 'l': add_link(optarg); break;
	case 'p': port = atoi(optarg); break;
	case '?': usage();
	}

	if (optind != argc) usage();

	dprintf(0,"starting gale server\n");
	openlog(argv[0],LOG_PID,LOG_DAEMON);

	if (uname(&un)) 
		early("uname");
	tmp = gale_malloc(strlen(un.nodename) + 20);
	sprintf(tmp,"%s:%d",un.nodename,port);
	server_id = tmp;

	listener = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
	if (listener < 0) 
		early("socket");
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port = htons(port);
	if (setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one)))
		early("setsockopt");
	if (bind(listener,(struct sockaddr *)&sin,sizeof(sin))) 
		early("bind");
	if (listen(listener,20))
		early("listen");

	dprintf(1,"bound socket, entering main loop\n");
	if (!debug_level && fork()) return 0;

	if (!debug_level) setsid();
	loop();

	return 0;
}
