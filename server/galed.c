#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/utsname.h>
#include <limits.h> /* NetBSD (at least) requires this order. */

#include "gale/all.h"
#include "connect.h"
#include "server.h"

static int listener,old_listener,port = 11511,old_port = 8413;
static struct connect *list = NULL;
static struct attach *try = NULL;

static void add_connect(int fd,int old) {
	struct connect *conn = new_connect(fd,fd,old);
	conn->next = list;
	list = conn;
}

static void incoming(int fd,int old) {
	struct sockaddr_in sin;
	int len = sizeof(sin);
	int one = 1;
	int newfd = accept(fd,(struct sockaddr *) &sin,&len);
	if (newfd < 0) {
		if (errno != ECONNRESET)
			gale_alert(GALE_WARNING,"accept",errno);
		return;
	}
	gale_dprintf(2,"[%d] new connection\n",newfd);
	setsockopt(newfd,SOL_SOCKET,SO_KEEPALIVE,
	           (SETSOCKOPT_ARG_4_T) &one,sizeof(one));
	add_connect(newfd,old);
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
		if (0 <= listener) FD_SET(listener,&rfd);
		if (0 <= old_listener) FD_SET(old_listener,&rfd);
		timeo.tv_sec = LONG_MAX;
		timeo.tv_usec = 0;
		for (ptr = list; ptr; ptr = ptr->next)
			pre_select(ptr,&rfd,&wfd);
		for (att = try; att; att = att->next)
			attach_select(att,&wfd,&now,&timeo);

		if (gale_debug > 5) {
			int i;
			for (i = 0; i < FD_SETSIZE; ++i) {
				if (FD_ISSET(i,&rfd))
				gale_dprintf(5,"--> waiting for [%d] read\n",i);
				if (FD_ISSET(i,&wfd))
				gale_dprintf(5,"--> waiting for [%d] write\n",i);
			}
		}

		if (timeo.tv_sec == LONG_MAX) {
			gale_dprintf(4,"--> select: now = %d.%06d, no timeout\n",
		                now.tv_sec,now.tv_usec);
			ret = select(FD_SETSIZE,
			             (SELECT_ARG_2_T) &rfd,
			             (SELECT_ARG_2_T) &wfd,
			             NULL,NULL);
		} else {
			gale_dprintf(4,"--> select: now = %d.%06d, to = %d.%06d\n",
		        now.tv_sec,now.tv_usec,timeo.tv_sec,timeo.tv_usec);
			ret = select(FD_SETSIZE,
			             (SELECT_ARG_2_T) &rfd,
			             (SELECT_ARG_2_T) &wfd,
			             NULL,&timeo);
		}
		if (ret == 0) {
			FD_ZERO(&rfd);
			FD_ZERO(&wfd);
			gale_dprintf(4,"--> select: timed out\n");
		}
		if (ret < 0) {
			gale_alert(GALE_WARNING,"select",errno);
			continue;
		}

		gettimeofday(&now,&tz);

		if (gale_debug > 5) {
			int i;
			for (i = 0; i < FD_SETSIZE; ++i) {
				if (FD_ISSET(i,&rfd))
					gale_dprintf(5,"--> [%d] for reading\n",i);
				if (FD_ISSET(i,&wfd))
					gale_dprintf(5,"--> [%d] for writing\n",i);
			}
		}

		if (0 <= listener && FD_ISSET(listener,&rfd))
			incoming(listener,0);
		if (0 <= old_listener && FD_ISSET(old_listener,&rfd))
			incoming(old_listener,1);

		aprev = NULL; att = try;
		while (att != NULL) {
			int fd = select_attach(att,&wfd,&now);
			if (fd == -1) {
				aprev = att;
				att = att->next;
				continue;
			}
			add_connect(fd,0);
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
				gale_dprintf(2,"[%d] lost connection\n",ptr->rfd);
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

static void add_links(void) {
	char *str,*val;

	str = getenv("GALE_LINKS"); if (!str) return;
	str = gale_strdup(str);
	val = strtok(str,";");

	do {
		char *at = strrchr(val,'@');
		struct attach *att = new_attach();
		if (at) {
			att->subs = gale_text_from_local(val,at - val);
			att->server = gale_strdup(at + 1);
		} else {
			att->subs = gale_text_from_local("",-1);
			att->server = gale_strdup(val);
		}
		att->next = try;
		try = att;
	} while ((val = strtok(NULL,";")));

	gale_free(str);
}

static void usage(void) {
	fprintf(stderr,
	"%s\n"
	"usage: galed [-p port] [-P port]\n"
	"flags: -p       Set the port to listen on (default %d)\n"
	"       -P       Set the port for the old protocol (%d)\n"
	,GALE_BANNER,port,old_port);
	exit(1);
}

static int make_listener(int port) {
	struct sockaddr_in sin;
	int one = 1,sock = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
	if (sock < 0) {
		gale_alert(GALE_WARNING,"socket",errno);
		return sock;
	}
	fcntl(sock,F_SETFD,1);
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port = htons(port);
	if (setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,
	               (SETSOCKOPT_ARG_4_T) &one,sizeof(one)))
		gale_alert(GALE_WARNING,"setsockopt",errno);
	if (bind(sock,(struct sockaddr *)&sin,sizeof(sin))) {
		gale_alert(GALE_WARNING,"bind",errno);
		close(sock);
		return -1;
	}
	if (listen(sock,20)) {
		gale_alert(GALE_WARNING,"listen",errno);
		close(sock);
		return -1;
	}
	return sock;
}

int main(int argc,char *argv[]) {
	int opt;
	gale_init("galed",argc,argv);

	srand48(time(NULL) ^ getpid());

	while ((opt = getopt(argc,argv,"hdDp:P:")) != EOF) switch (opt) {
	case 'd': ++gale_debug; break;
	case 'D': gale_debug += 5; break;
	case 'P': old_port = atoi(optarg); break;
	case 'p': port = atoi(optarg); break;
	case 'h':
	case '?': usage();
	}

	add_links();

	if (optind != argc) usage();

	gale_dprintf(0,"starting gale server\n");
	openlog(argv[0],LOG_PID,LOG_LOCAL5);

	listener = make_listener(port);
	old_listener = make_listener(old_port);

	if (listener == -1 && old_listener == -1)
		gale_alert(GALE_ERROR,"could not bind either port",0);

	gale_dprintf(1,"now listening, entering main loop\n");
	gale_daemon(0);
	loop();

	return 0;
}
