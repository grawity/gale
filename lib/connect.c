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

struct gale_connect *make_connect(const char *serv) {
	int alloc = 0;
	const char *end,*cp;
	struct gale_connect *conn;

	cp = serv;

	gale_create(conn);
	conn->len = 0;
	conn->array = NULL;

	do {
		struct sockaddr_in sin;
		int i;
		struct hostent *he;
		const char *colon = strchr(cp,':');
		char *name = NULL;
		end = strchr(cp,',');
		if (!end) end = cp + strlen(cp);
		if (!colon || colon > end) colon = end;

		name = gale_strndup(cp,colon - cp);
		he = gethostbyname(name);
		if (!he) {
			char *tmp = gale_malloc(strlen(name) + 128);
			sprintf(tmp,"can't find host \"%s\"",name);
			gale_alert(GALE_WARNING,tmp,0);
			gale_free(tmp);
		 	goto skip_host;
		}

		sin.sin_family = AF_INET;
		sin.sin_port = htons(colon < end ? atoi(colon + 1) : DEF_PORT);
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

	skip_host:
		if (name) gale_free(name);
		cp = end;
		if (*cp == ',') ++cp;
	} while (*cp);

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
