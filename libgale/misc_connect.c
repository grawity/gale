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
#include "gale/globals.h"

#ifdef HAVE_ADNS
#include "adns.h"
#include "oop-adns.h"
#define ADNS_ONLY(x) x
#else
#define ADNS_ONLY(x)
#endif

static struct in_addr *local_addrs = NULL;

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
	memset(&addr,0,sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = 0;
	addr.sin_port = 0;
	return conn->call(-1,null_text,addr,conn->found_local,conn->data);
}

static void check_done(struct gale_connect *conn) {
	if (0 == conn->num_address
	ADNS_ONLY(&& 0 == conn->num_resolve && conn->all_names))
		conn->source->on_time(conn->source,OOP_TIME_NOW,on_abort,conn);
}

static void del_address(struct gale_connect *conn,int i) {
	gale_dprintf(6,"(connect %p) removing address %s\n", conn,
	             inet_ntoa(conn->addresses[i]->sin.sin_addr),
	             ntohs(conn->addresses[i]->sin.sin_port));
	conn->source->cancel_fd(conn->source,
	                        conn->addresses[i]->sock,OOP_WRITE);
	conn->addresses[i] = conn->addresses[--(conn->num_address)];
	check_done(conn);
}

static int is_local(int sock,struct in_addr *addr) {
	struct sockaddr_in sin;
	int i;
	memset(&sin,0,sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr = *addr;
	sin.sin_port = 0;
	if (0 == bind(sock,(struct sockaddr *) &sin,sizeof(sin))) 
		return 1;

	/* Too bad this isn't asynchronous, but that just hurts my head. */
	if (NULL == local_addrs) {
		struct hostent *host;
		host = gethostbyname(gale_text_to(
			gale_global->enc_sys,gale_var(G_("HOST"))));
		if (NULL == host) {
			local_addrs = gale_malloc_safe(sizeof(*local_addrs));
			local_addrs[0].s_addr = 0;
		} else {
			int num;
			assert(AF_INET == host->h_addrtype);
			assert(sizeof(*local_addrs) == host->h_length);
			for (num = 0; NULL != host->h_addr_list[num]; ++num) ;
			local_addrs = gale_malloc_safe(
				(1 + num) * sizeof(*local_addrs));
			for (num = 0; NULL != host->h_addr_list[num]; ++num)
				memcpy(&local_addrs[num],
					host->h_addr_list[num],
					host->h_length);
			local_addrs[num].s_addr = 0;
		}
	}

	for (i = 0; 0 != local_addrs[i].s_addr; ++i)
		if (local_addrs[i].s_addr == addr->s_addr)
			return 1;
	return 0;
}

static void add_address(
	struct gale_connect *conn,
	struct gale_text name,struct sockaddr_in sin) 
{
	struct address *addr;

	gale_dprintf(5,"(connect %p) \"%s\" is %s\n",
	             conn, gale_text_to(0, name), inet_ntoa(sin.sin_addr));
	if (conn->alloc_address == conn->num_address) {
		gale_resize_array(conn->addresses,
			conn->alloc_address = conn->alloc_address 
			? 2*conn->alloc_address : 6);
	}

	/* Are we a sucker? */
	if (0 != conn->avoid_local_port && conn->found_local
	&&  ntohl(sin.sin_addr.s_addr) 
	>=  ntohl(conn->least_local.s_addr)) {
		gale_dprintf(5,"(connect %p) ignoring sucker address %s\n",
		             conn, inet_ntoa(sin.sin_addr));
		return;
	}

	gale_create(addr);
	addr->sin = sin;
	addr->name = name;
	addr->state = sock_connecting;
	addr->sock = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
	if (addr->sock < 0) return;

	if (conn->avoid_local_port == ntohs(sin.sin_port)
	&&  is_local(addr->sock,&sin.sin_addr))
	{
		gale_dprintf(5,"(connect %p) address %s is local, skipping\n",
		             conn, inet_ntoa(sin.sin_addr));

		if (!conn->found_local
		||  ntohl(sin.sin_addr.s_addr) 
		  < ntohl(conn->least_local.s_addr)) {
			int i = 0;
			conn->found_local = 1;
			conn->least_local = sin.sin_addr;

			/* Terminate other suckers. */
			while (i < conn->num_address)
			    if (ntohl(conn->addresses[i]->sin.sin_addr.s_addr)
			    >=  ntohl(sin.sin_addr.s_addr)) {
				gale_dprintf(5,"(connect %p) killing sucker address %s\n", conn, inet_ntoa(conn->addresses[i]->sin.sin_addr));
				close(conn->addresses[i]->sock);
				del_address(conn,i);
			    } else
				++i;
		}

		close(addr->sock);
		return;
	}

	gale_dprintf(5,"(connect %p) connecting to %s:%d\n", conn,
	             inet_ntoa(sin.sin_addr),ntohs(sin.sin_port));
	if (fcntl(addr->sock,F_SETFL,O_NONBLOCK)) {
		close(addr->sock);
		return;
	}

	while (CONNECT_F(addr->sock,(struct sockaddr *) &sin,sizeof(sin))) {
		if (errno == EINPROGRESS) break;
		if (errno != EINTR) {
			gale_dprintf(5,"(connect %p) error connecting to %s: %s\n",
				     conn,inet_ntoa(sin.sin_addr),strerror(errno));
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
	struct hostent *he = gethostbyname(gale_text_to(NULL,name));
	struct sockaddr_in sin;
	int i;
#endif

	gale_dprintf(4,"(connect %p) looking for \"%s\"\n",
	             conn, gale_text_to(gale_global->enc_console,name));

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
		gale_text_to(NULL,name),
		adns_r_a,0,on_lookup,res);

	if (NULL != res->query) conn->resolving[conn->num_resolve++] = res;
#else
	if (NULL == he) {
		gale_dprintf(5,"(connect %p) no addresses for \"%s\"\n",
		             conn, gale_text_to(0, name));
		return;
	}

	memset(&sin,0,sizeof(sin));
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

/** Start attempting to establish a TCP connection.
 *  This function accepts a list of hostnames in \a serv and starts
 *  attempting to connect to all of them.  The connection attempts will
 *  proceed in the background after the function returns (scheduled by
 *  liboop); the function \a call will be invoked when one of them
 *  succeeds or all of them fail.  If the port is not specified with the
 *  \a serv string, the default Gale port will be used.
 *  \param src Liboop event source to use for connection.
 *  \param serv Comma-separated list of one or more hostnames.
 *  \param avoid_local_port If nonzero, avoid reconnecting to ourselves.
 *  \param call Callback to invoke when connected attempt completes.
 *  \param user User-supplied parameter, passed to \a call.
 *  \return Connection handle for use with gale_abort_connect().
 *  \sa gale_abort_connect() */
struct gale_connect *gale_make_connect(
	oop_source *src,struct gale_text serv,int avoid_local_port,
	gale_connect_call *call,void *user)
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
	conn->data = user;

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
		gale_dprintf(4,"(connect %p) connection to %s:%d failed: %s\n",
		             conn, inet_ntoa(conn->addresses[i]->sin.sin_addr),
		             ntohs(conn->addresses[i]->sin.sin_port),
		             strerror(errno));
		close(fd);
		del_address(conn,i);
	} else {
		int one = 1;
#if 0
		struct linger linger = { 1, 5000 }; /* 5 seconds */
#endif
		struct gale_text name = conn->addresses[i]->name;
		struct sockaddr_in addr = conn->addresses[i]->sin;

		gale_dprintf(4,"(connect %p) established connection to %s:%d\n",
		             conn,inet_ntoa(addr.sin_addr),ntohs(addr.sin_port));

		del_address(conn,i);
		gale_abort_connect(conn);

#if 0
		/* We actually want O_NONBLOCK now. */
		fcntl(fd,F_SETFL,0);

		/* SO_LINGER causes nothing but trouble. */
		setsockopt(fd,SOL_SOCKET,SO_LINGER,
		           (SETSOCKOPT_ARG_4_T) &linger,sizeof(linger));
#endif
		setsockopt(fd,SOL_SOCKET,SO_KEEPALIVE,
		           (SETSOCKOPT_ARG_4_T) &one,sizeof(one));

		return conn->call(fd,name,addr,conn->found_local,conn->data);
	}

	return OOP_CONTINUE;
}

#ifdef HAVE_ADNS
static void *on_lookup(oop_adapter_adns *adns,adns_answer *answer,void *data) {
	struct resolution *res = (struct resolution *) data;
	struct gale_connect *conn = res->owner;
	int i;

	res->query = NULL;

	if (adns_s_ok == answer->status) {
		struct gale_text name = answer->cname 
		     ? gale_text_from(NULL,answer->cname,-1) 
		     : res->name;
		struct sockaddr_in sin;
		assert(adns_r_a == answer->type);
		memset(&sin,0,sizeof(sin));
		sin.sin_family = AF_INET;
		sin.sin_port = htons(res->port);
		for (i = 0; i < answer->nrrs; ++i) {
			sin.sin_addr = answer->rrs.inaddr[i];
			add_address(conn,name,sin);
		}
	}
	else {
		gale_dprintf(5,"(connect %p) no addresses for \"%s\"\n",
		             conn, gale_text_to(0, res->name));
	}

	free(answer);

	for (i = 0; res != conn->resolving[i]; ++i) 
		assert(i < conn->num_resolve);
	del_name(conn,i);

	if (0 == conn->num_resolve && conn->all_names) 
		last_address(conn);

	return OOP_CONTINUE;
}
#endif

/** Abort a connection attempt.
 *  This function stops attempting to establish a connection and releases
 *  all resources associated with the connection attempt.
 *  \param conn Connection handle from gale_make_connect().
 *  \sa gale_make_connect() */
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

/** Return a description of the connected host.
 *  \param host Hostname (usually as passed to ::gale_make_connect).
 *  \param addr IP address (usually as passed to ::gale_make_connect).
 *  \return Human-readable text string describing the remote host. */
struct gale_text gale_connect_text(
	struct gale_text host,
	struct sockaddr_in addr) 
{
	return gale_text_concat(6,
		host,
		G_(" ("),
		gale_text_from(NULL,inet_ntoa(addr.sin_addr),-1),
		G_(":"),
		gale_text_from_number(ntohs(addr.sin_port),10,0),
		G_(")"));
}
