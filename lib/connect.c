#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "gale/misc.h"
#include "gale/server.h" 
#include "gale/compat.h"

#define DEF_PORT (11511)

struct attempt {
	int sock;
	struct sockaddr_in sin;
};

struct gale_connect {
	struct attempt *array;
	int len;
};

struct gale_connect *make_connect(struct gale_text serv) {
	int alloc = 0;
	struct gale_text spec = null_text;
	struct gale_connect *conn;

	gale_create(conn);
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
			if (connect(fd,(struct sockaddr *) &sin,sizeof(sin)) && 
			    errno != EINPROGRESS) 
				goto skip_addr;

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
		abort_connect(conn);
		conn = NULL;
	}

	return conn;
}

void connect_select(struct gale_connect *conn,fd_set *wfd) {
	int i;
	for (i = 0; i < conn->len; ++i) FD_SET(conn->array[i].sock,wfd);
}

static void delete(struct gale_connect *conn,int i) {
	conn->array[i] = conn->array[--(conn->len)];
}

int select_connect(fd_set *wfd,struct gale_connect *conn) {
	int fd,i = 0,one = 1;
	while (i < conn->len) {
		if (!FD_ISSET(conn->array[i].sock,wfd)) {
			++i;
			continue;
		}
		if (connect(conn->array[i].sock,
		            (struct sockaddr *) &conn->array[i].sin,
		            sizeof(conn->array[i].sin)) && errno != EISCONN) {
			close(conn->array[i].sock);
			delete(conn,i);
			continue;
		}
		break;
	}
	if (conn->len == 0) {
		abort_connect(conn);
		return -1;
	}
	if (i == conn->len) return 0;
	fd = conn->array[i].sock;
	delete(conn,i);
	abort_connect(conn);
	fcntl(fd,F_SETFL,0);
	setsockopt(fd,SOL_SOCKET,SO_KEEPALIVE,
	           (SETSOCKOPT_ARG_4_T) &one,sizeof(one));
	return fd;
}

void abort_connect(struct gale_connect *conn) {
	while (conn->len) {
		close(conn->array[0].sock);
		delete(conn,0);
	}
	if (conn->array) gale_free(conn->array);
	gale_free(conn);
}
