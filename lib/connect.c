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

/* #undef HAVE_ADNS */ /* buggy for now! */

#ifdef HAVE_ADNS
#include "adns.h"
#include "oop-adns.h"
#define ADNS_ONLY(x) x
#else
#define ADNS_ONLY(x)
#endif

struct address {
	int sock;
	enum { sock_connecting, sock_connected } state;
	struct sockaddr_in sin;
	struct gale_text name;
};

#ifdef HAVE_ADNS
struct resolution {
	struct gale_connect *owner;
	struct gale_text name;
	int port;
	oop_adns_query *query;
};
#endif

struct gale_connect {
	oop_source *source;
	ADNS_ONLY(oop_adapter_adns *adns;)

	int avoid_local_port,found_local;
	struct in_addr least_local;

	struct address **addresses;
	int num_address,alloc_address;

	ADNS_ONLY(struct resolution **resolving;)
	ADNS_ONLY(int num_resolve; int alloc_resolve;)
	ADNS_ONLY(int all_names);

	gale_connect_call *call;
	void *data;
};

#ifdef HAVE_SOCKS
extern int Rconnect(int,const struct sockaddr *,size_t);
#define CONNECT_F Rconnect
#else
#define CONNECT_F connect
#endif

static oop_call_fd on_write;
static oop_call_time on_abort;
ADNS_ONLY(static oop_adns_call on_lookup;)

static void *on_abort(oop_source *s,struct timeval tv,void *x) {
	struct gale_connect *conn = (struct gale_connect *) x;
	struct sockaddr_in addr;
	gale_abort_connect(conn);
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_NONE;
	addr.sin_port = 0;
	return conn->call(-1,null_text,addr,conn->found_local,conn->data);
}

static void check_done(struct gale_connect *conn) {
	if (0 == conn->num_address
	ADNS_ONLY(&& 0 == conn->num_resolve && conn->all_names))
		conn->source->on_time(conn->source,OOP_TIME_NOW,on_abort,conn);
}

static void del_address(struct gale_connect *conn,int i) {
	gale_dprintf(6,"(connect) removing address %s\n",
	             inet_ntoa(conn->addresses[i]->sin.sin_addr),
	             ntohs(conn->addresses[i]->sin.sin_port));
	conn->source->cancel_fd(conn->source,
	                        conn->addresses[i]->sock,OOP_WRITE);
	conn->addresses[i] = conn->addresses[--(conn->num_address)];
	check_done(conn);
}

static void add_address(
	struct gale_connect *conn,
	struct gale_text name,struct sockaddr_in sin) 
{
	struct address *addr;

	if (conn->alloc_address == conn->num_address) {
		gale_resize_array(conn->addresses,
			conn->alloc_address = conn->alloc_address 
			? 2*conn->alloc_address : 6);
	}

	/* Are we a sucker? */
	if (0 != conn->avoid_local_port && conn->found_local
	&&  ntohl(sin.sin_addr.s_addr) 
	>=  ntohl(conn->least_local.s_addr)) return;

	gale_dprintf(5,"(connect) connecting to %s:%d\n",
	             inet_ntoa(sin.sin_addr),ntohs(sin.sin_port));

	gale_create(addr);
	addr->sin = sin;
	addr->name = name;
	addr->state = sock_connecting;
	addr->sock = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
	if (addr->sock < 0) return;

	sin.sin_port = 0;
	if (conn->avoid_local_port == ntohs(addr->sin.sin_port)
	&& 0 == bind(addr->sock,(struct sockaddr *) &sin,sizeof(sin)))
	{
		gale_dprintf(5,"(connect) address %s is local, skipping\n",
		             inet_ntoa(sin.sin_addr));

		if (!conn->found_local
		||  sin.sin_addr.s_addr < conn->least_local.s_addr) {
			int i = 0;
			conn->found_local = 1;
			conn->least_local = sin.sin_addr;

			/* Terminate other suckers. */
			while (i < conn->num_address)
			      if (ntohl(conn->addresses[i]->sin.sin_addr.s_addr)
				>= ntohl(sin.sin_addr.s_addr))
					del_address(conn,i);
				else
					++i;
		}

		close(addr->sock);
		return;
	}

	if (fcntl(addr->sock,F_SETFL,O_NONBLOCK)) {
		close(addr->sock);
		return;
	}

	sin.sin_port = addr->sin.sin_port;
	while (CONNECT_F(addr->sock,(struct sockaddr *) &sin,sizeof(sin))) {
		if (errno == EINPROGRESS) break;
		if (errno != EINTR) {
			gale_dprintf(5,"(connect) error connecting to %s: %s\n",
				     inet_ntoa(sin.sin_addr),strerror(errno));
			close(addr->sock);
			return;
		}
	}

	conn->addresses[conn->num_address++] = addr;
	conn->source->on_fd(conn->source,addr->sock,OOP_WRITE,on_write,conn);
}

static void last_address(struct gale_connect *conn) {
	ADNS_ONLY(assert(0 == conn->num_resolve);)
	check_done(conn);
}

static void add_name(struct gale_connect *conn,struct gale_text name,int port) {
#ifdef HAVE_ADNS
	struct resolution *res;
#else
	struct hostent *he = gethostbyname(gale_text_to_latin1(name));
	struct sockaddr_in sin;
	int i;
#endif

	gale_dprintf(4,"(connect) looking for \"%s\"\n",
	             gale_text_to_local(name));

#ifdef HAVE_ADNS
	if (conn->alloc_resolve == conn->num_resolve) {
		gale_resize_array(conn->resolving,
			conn->alloc_resolve = conn->alloc_resolve 
			? 2*conn->alloc_resolve : 6);
	}

	gale_create(res);
	res->owner = conn;
	res->name = name;
	res->port = port;
	res->query = oop_adns_submit(conn->adns,
		gale_text_to_latin1(name),
		adns_r_a,0,on_lookup,res);

	if (NULL != res->query) conn->resolving[conn->num_resolve++] = res;
#else
	if (NULL == he) return;
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	for (i = 0; NULL != he->h_addr_list[i]; ++i) {
		sin.sin_addr = * (struct in_addr *) he->h_addr_list[i];
		add_address(conn,name,sin);
	}
#endif
}

#ifdef HAVE_ADNS
static void del_name(struct gale_connect *conn,int i) {
	assert(conn->resolving[i]->owner == conn);
	if (NULL != conn->resolving[i]->query) {
		oop_adns_cancel(conn->resolving[i]->query);
		conn->resolving[i]->query = NULL;
	}
	conn->resolving[i] = conn->resolving[--(conn->num_resolve)];
	check_done(conn);
}
#endif

static void last_name(struct gale_connect *conn) {
#ifdef HAVE_ADNS
	conn->all_names = 1;
	if (0 == conn->num_resolve) last_address(conn);
#else
	last_address(conn);
#endif
}

struct gale_connect *gale_make_connect(
	oop_source *src,struct gale_text serv,int avoid_local_port,
	gale_connect_call *call,void *data)
{
	struct gale_connect *conn;
	struct gale_text spec = null_text;

	gale_create(conn);
	conn->source = src;
	ADNS_ONLY(conn->adns = oop_adns_new(conn->source,0,NULL);)
	conn->avoid_local_port = avoid_local_port;
	conn->found_local = 0;

	conn->addresses = NULL;
	conn->alloc_address = conn->num_address = 0;

	ADNS_ONLY(conn->resolving = NULL;)
	ADNS_ONLY(conn->alloc_resolve = conn->num_resolve = 0;)
	ADNS_ONLY(conn->all_names = 0);

	conn->call = call;
	conn->data = data;

	while (gale_text_token(serv,',',&spec)) {
		struct gale_text part = null_text;
		struct gale_text name;
		int port;

		gale_text_token(spec,':',&part);
		name = part;

		if (gale_text_token(spec,':',&part))
			port = gale_text_to_number(part);
		else
			port = gale_port;

		add_name(conn,name,port);
		add_name(conn,gale_text_concat(2,G_("gale."),name),port);
		add_name(conn,gale_text_concat(2,name,G_(".gale.org")),port);
	}

	last_name(conn);
	return conn;
}

static void *on_write(oop_source *src,int fd,oop_event event,void *user) {
	struct sockaddr *sa;
	int i;

	struct gale_connect *conn = (struct gale_connect *) user;
	for (i = 0; fd != conn->addresses[i]->sock; ++i) 
		assert(i < conn->num_address);

	sa = (struct sockaddr *) &conn->addresses[i]->sin;
	do errno = 0;
	while (CONNECT_F(fd,sa,sizeof(struct sockaddr_in))
	   &&  EINTR == errno);

	if (EISCONN != errno && 0 != errno) {
		gale_dprintf(4,"(connect) connection to %s:%d failed: %s\n",
		             inet_ntoa(conn->addresses[i]->sin.sin_addr),
		             ntohs(conn->addresses[i]->sin.sin_port),
		             strerror(errno));
		close(fd);
		del_address(conn,i);
	} else {
		int one = 1;
		struct linger linger = { 1, 5000 }; /* 5 seconds */
		struct gale_text name = conn->addresses[i]->name;
		struct sockaddr_in addr = conn->addresses[i]->sin;

		gale_dprintf(4,"(connect) established connection to %s:%d\n",
		             inet_ntoa(addr.sin_addr),ntohs(addr.sin_port));

		del_address(conn,i);
		gale_abort_connect(conn);

		fcntl(fd,F_SETFL,0);
		setsockopt(fd,SOL_SOCKET,SO_KEEPALIVE,
		           (SETSOCKOPT_ARG_4_T) &one,sizeof(one));
		setsockopt(fd,SOL_SOCKET,SO_LINGER,
		           (SETSOCKOPT_ARG_4_T) &linger,sizeof(linger));

		return conn->call(fd,name,addr,conn->found_local,conn->data);
	}

	return OOP_CONTINUE;
}

#ifdef HAVE_ADNS
static void *on_lookup(oop_adapter_adns *adns,adns_answer *answer,void *data) {
	struct resolution *res = (struct resolution *) data;
	struct gale_connect *conn = res->owner;
	int i;

	for (i = 0; res != conn->resolving[i]; ++i) 
		assert(i < conn->num_resolve);

	res->query = NULL;
	del_name(conn,i);

	if (adns_s_ok == answer->status) {
		struct gale_text name = answer->cname 
		     ? gale_text_from_latin1(answer->cname,-1) 
		     : res->name;
		struct sockaddr_in sin;
		assert(adns_r_a == answer->type);
		sin.sin_family = AF_INET;
		sin.sin_port = htons(res->port);
		for (i = 0; i < answer->nrrs; ++i) {
			sin.sin_addr = answer->rrs.inaddr[i];
			add_address(conn,name,sin);
		}
	}

	free(answer);

	if (0 == conn->num_resolve && conn->all_names) 
		last_address(conn);

	return OOP_CONTINUE;
}
#endif

void gale_abort_connect(struct gale_connect *conn) {
	ADNS_ONLY(while (conn->num_resolve) del_name(conn,0);)
	while (conn->num_address) {
		close(conn->addresses[0]->sock);
		del_address(conn,0);
	}
#ifdef HAVE_ADNS
	if (NULL != conn->adns) {
		oop_adns_delete(conn->adns);
		conn->adns = NULL;
	}
#endif
	conn->source->cancel_time(conn->source,OOP_TIME_NOW,on_abort,conn);
}

struct gale_text gale_connect_text(
	struct gale_text host,
	struct sockaddr_in addr) 
{
	return gale_text_concat(6,
		host,
		G_(" ("),
		gale_text_from_latin1(inet_ntoa(addr.sin_addr),-1),
		G_(":"),
		gale_text_from_number(ntohs(addr.sin_port),10,0),
		G_(")"));
}
