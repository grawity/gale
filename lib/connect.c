#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "gale/misc.h"
#include "gale/compat.h"

#define DEF_PORT (11511)

struct attempt {
	int sock;
	struct sockaddr_in sin;
};

struct gale_connect {
	oop_source *source;
	void *(*call)(int fd,void *);
	void *call_data;
	struct attempt *array;
	int len;
};

#ifdef HAVE_SOCKS
extern int Rconnect(int,const struct sockaddr *,size_t);
#define CONNECT_F Rconnect
#else
#define CONNECT_F connect
#endif

static oop_call_fd on_write;

struct gale_connect *gale_make_connect(
	oop_source *src,struct gale_text serv,
	void *(*call)(int fd,void *),void *call_data)
{
	int i,alloc = 0;
	struct gale_text spec = null_text;
	struct gale_connect *conn;

	gale_create(conn);
	conn->source = src;
	conn->call = call;
	conn->call_data = call_data;
	conn->len = 0;
	conn->array = NULL;

	while (gale_text_token(serv,',',&spec)) {
		struct gale_text part = null_text;
		struct sockaddr_in sin;
		struct hostent *he;
		int i;

		sin.sin_family = AF_INET;

		gale_text_token(spec,':',&part);
		he = gethostbyname(gale_text_to_latin1(part));
		if (!he) {
			gale_alert(GALE_WARNING,gale_text_to_local(
				gale_text_concat(3,
					G_("can't find host \""),
					part,G_("\""))),0);
			continue;
		}

		if (gale_text_token(spec,':',&part))
			sin.sin_port = htons(gale_text_to_number(part));
		else
			sin.sin_port = htons(DEF_PORT);

		for (i = 0; he->h_addr_list[i]; ++i) {
			int fd = -1;
			sin.sin_addr = * (struct in_addr *) he->h_addr_list[i];
			fd = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
			if (fd < 0) goto skip_addr;
			if (fcntl(fd,F_SETFL,O_NONBLOCK)) goto skip_addr;
			while (CONNECT_F(fd,(struct sockaddr *) &sin,sizeof(sin))) {
				if (errno == EINPROGRESS) break;
				if (errno != EINTR) goto skip_addr;
			}

			if (alloc == conn->len) {
				const int len = sizeof(conn->array[0]);
				struct attempt *tmp = conn->array;
				alloc = alloc ? alloc * 2 : 16;
				conn->array = gale_malloc(alloc * len);
				memcpy(conn->array,tmp,conn->len * len);
				if (tmp) gale_free(tmp);
			}

			conn->array[conn->len].sock = fd;
			conn->array[conn->len].sin = sin;
			++(conn->len);
			fd = -1;
		skip_addr:
			if (fd != -1) close(fd);
		}
	}

	if (!conn->len) {
		gale_abort_connect(conn);
		conn = NULL;
	} else for (i = 0; i < conn->len; ++i)
		src->on_fd(src,conn->array[i].sock,OOP_WRITE,on_write,conn);
	return conn;
}

static void delete(struct gale_connect *conn,int i) {
	conn->source->cancel_fd(
		conn->source,conn->array[i].sock,
		OOP_WRITE,on_write,conn);
	conn->array[i] = conn->array[--(conn->len)];
}

static void *on_write(oop_source *src,int fd,oop_event event,void *user)
{
	struct sockaddr *sa;
	int i;

	struct gale_connect *conn = (struct gale_connect *) user;
	for (i = 0; fd != conn->array[i].sock; ++i) assert(i < conn->len);
	if (conn->len == i) return OOP_CONTINUE;

	sa = (struct sockaddr *) &conn->array[i].sin;
	do errno = 0;
	while (CONNECT_F(fd,sa,sizeof(struct sockaddr_in))
	   &&  EINTR == errno);

	if (EISCONN != errno && 0 != errno) {
		close(fd);
		delete(conn,i);
		if (0 == conn->len) {
			gale_abort_connect(conn);
			return conn->call(-1,conn->call_data);
		}
	} else {
		int one = 1;
		struct linger linger = { 1, 5000 }; /* 5 seconds */
		delete(conn,i);
		gale_abort_connect(conn);
		fcntl(fd,F_SETFL,0);
		setsockopt(fd,SOL_SOCKET,SO_KEEPALIVE,
		           (SETSOCKOPT_ARG_4_T) &one,sizeof(one));
		setsockopt(fd,SOL_SOCKET,SO_LINGER,
		           (SETSOCKOPT_ARG_4_T) &linger,sizeof(linger));
		return conn->call(fd,conn->call_data);
	}

	return OOP_CONTINUE;
}

void gale_abort_connect(struct gale_connect *conn) {
	while (conn->len) {
		close(conn->array[0].sock);
		delete(conn,0);
	}
}
