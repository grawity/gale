#include "gale/core.h"
#include "gale/misc.h"

#include "subscr.h"
#include "connect.h"
#include "server.h"

#include <assert.h>
#include <string.h>

struct sub {
	int flag,priority;
	struct connect *connect;
};

struct node {
	char *spec;
	int len,alloc;
	struct node *child;
	struct node *next;
	int num,size;
	struct sub *array;
};

static int stamp = 0;
static struct node root = { "",0,0,NULL,NULL,0,0,NULL };
static struct connect *list = NULL;

static void add(struct node *ptr,const char *spec,int len,
                struct sub *sub) 
{
	struct node *child,*node;
	int i;

	gale_dprintf(3,"[%d] subscribing to \"%.*s\"\n",
	             sub->connect->rfd,len,spec);

	if (len == 0) {
		gale_dprintf(4,"+++ adding connection to node\n");
		if (ptr->num == ptr->size) {
			struct sub *old = ptr->array;
			ptr->size = ptr->size ? ptr->size * 2 : 10;
			ptr->array = gale_malloc(ptr->size * sizeof(*old));
			memcpy(ptr->array,old,ptr->num * sizeof(*old));
			if (old) gale_free(old);
		}
		ptr->array[ptr->num++] = *sub;
		return;
	}

	child = ptr->child;
	while (child != NULL && child->spec[0] != spec[0]) {
		child = child->next;
	}

	if (child == NULL) {
		gale_dprintf(4,"+++ creating new node for \"%.*s\"\n",len,spec);
		node = gale_malloc(sizeof(struct node));
		node->spec = gale_malloc(node->alloc = len);
		node->len = len;
		strncpy(node->spec,spec,len);
		node->child = NULL;
		node->next = ptr->child;
		ptr->child = node;
		node->size = node->num = 0;
		node->array = NULL;
		add(node,spec + len,0,sub);
		return;
	}

	i = 0;
	while (i < len && i < child->len && spec[i] == child->spec[i]) ++i;

	if (i != child->len) {
		gale_dprintf(4,"+++ truncating \"%.*s\" node to \"%.*s\"\n",
		        child->len,child->spec,i,spec);
		node = gale_malloc(sizeof(struct node));
		node->spec = gale_malloc(node->alloc = child->len - i);
		node->len = child->len - i;
		memcpy(node->spec,child->spec + i,child->len - i);
		node->child = child->child;
		node->next = NULL;
		node->array = child->array;
		node->size = child->size;
		node->num = child->num;
		child->child = node;
		child->array = NULL;
		child->size = child->num = 0;
		child->len = i;
	} else
		gale_dprintf(4,"+++ matched \"%.*s\"\n",child->len,child->spec);

	add(child,spec + i,len - i,sub);
}

static int same_sub(const struct sub *a,const struct sub *b) {
	return (a->priority == b->priority && a->flag == b->flag &&
	        a->connect == b->connect);
}

static void remove(struct node *ptr,const char *spec,int len,
                   struct sub *sub)
{
	struct node *parent = NULL,*prev = NULL;
	int i;

	gale_dprintf(3,"[%d] unsubscribing from \"%.*s\"\n",
	             sub->connect->rfd,len,spec);

	while (len != 0) {
		parent = ptr;
		prev = NULL;
		ptr = ptr->child;
		while (ptr && ptr->spec[0] != spec[0]) {
			prev = ptr;
			ptr = ptr->next;
		}
		assert(ptr && ptr->len <= len &&
			!memcmp(spec,ptr->spec,ptr->len));
		gale_dprintf(4,"--- matched \"%.*s\"\n",ptr->len,ptr->spec);
		spec += ptr->len;
		len -= ptr->len;
	}

	gale_dprintf(4,"--- removing connection from node\n");
	for (i = 0; i < ptr->num && !same_sub(&ptr->array[i],sub); ++i) ;
	assert(i != ptr->num);
	ptr->array[i] = ptr->array[--ptr->num];

	if (!parent) {
		gale_dprintf(4,"--- root node, all done\n");
		return;
	}

	if (ptr->num) {
		gale_dprintf(4,"--- node still has other connections, all done\n");
		return;
	}

	if (ptr->child && ptr->child->next) {
		gale_dprintf(4,"--- node has more than one child, all done\n");
		return;
	}

	if (!ptr->child) {
		gale_dprintf(4,"--- removing node\n");
		if (prev)
			prev->next = ptr->next;
		else
			parent->child = ptr->next;
		if (ptr->spec) gale_free(ptr->spec);
		if (ptr->array) gale_free(ptr->array);
		gale_free(ptr);

		if (parent->num) {
			gale_dprintf(4,"--- parent has connections, all done\n");
			return;
		}
		if (parent == &root) {
			gale_dprintf(4,"--- parent is root, all done\n");
			return;
		}
		assert(parent->child);
		if (parent->child->next) {
			gale_dprintf(4,"--- parent has more than one child,"
			          " all done\n");
			return;
		}
		gale_dprintf(4,"--- moving to parent ...\n");
		ptr = parent;
	}

	prev = ptr->child;
	gale_dprintf(4,"--- merging with singleton child \"%.*s\"\n",
	        prev->len,prev->spec);

	if (ptr->alloc < ptr->len + prev->len) {
		char *tmp = gale_malloc(ptr->len + prev->len);
		memcpy(tmp,ptr->spec,ptr->len);
		gale_free(ptr->spec);
		ptr->spec = tmp;
	}

	memcpy(ptr->spec + ptr->len,prev->spec,prev->len);
	ptr->len += prev->len;

	if (ptr->array) gale_free(ptr->array);
	ptr->array = prev->array;
	ptr->num = prev->num;
	ptr->size = prev->size;
	ptr->child = prev->child;

	if (prev->spec) gale_free(prev->spec);
	gale_free(prev);
}

static void subscr(struct connect *conn,
                   void (*func)(struct node *,const char *,int,struct sub *))
{
	const char *ep,*cp = conn->subscr;
	struct sub sub;
	sub.connect = conn;
	sub.priority = 0;
	conn->stamp = stamp;
	do {
		for (ep = cp; *ep && *ep != ':'; ++ep) ;
		sub.flag = 1;
		if (*cp == '+') ++cp;
		else if (*cp == '-') { ++cp; sub.flag = 0; }
		func(&root,cp,ep - cp,&sub);
		cp = ep + 1;
		++(sub.priority);
	} while (*ep);
}

void add_subscr(struct connect *conn) {
	subscr(conn,add);
}

void remove_subscr(struct connect *conn) {
	subscr(conn,remove);
}

static void transmit(struct node *ptr,const char *spec,int len,
                     struct gale_message *msg,struct connect *avoid)
{
	int i;
	if (len < 0) return;
	for (i = 0; i < ptr->num; ++i) {
		if (ptr->array[i].connect == avoid) continue;
		if (ptr->array[i].connect->stamp != stamp) {
			ptr->array[i].connect->sub_next = list;
			list = ptr->array[i].connect;
			ptr->array[i].connect->stamp = stamp;
			ptr->array[i].connect->priority = -1;
		}
		if (ptr->array[i].connect->priority > ptr->array[i].priority)
			continue;
		ptr->array[i].connect->priority = ptr->array[i].priority;
		ptr->array[i].connect->flag = ptr->array[i].flag;
	}

	for (ptr = ptr->child; ptr; ptr = ptr->next)
		if (ptr->len <= len && !memcmp(ptr->spec,spec,ptr->len)) {
			gale_dprintf(4,"*** matched \"%.*s\"\n",
			             ptr->len,ptr->spec);
			transmit(ptr,spec + ptr->len,len - ptr->len,msg,avoid);
		}
}

void subscr_transmit(struct gale_message *msg,struct connect *avoid) {
	const char *ep,*cp = msg->category;
	++stamp;

	assert(list == NULL);

	do {
		for (ep = cp; *ep && *ep != ':'; ++ep) ;
		gale_dprintf(3,"*** transmitting \"%.*s\", avoiding [%d]\n",
		        ep - cp,cp,avoid ? avoid->rfd : -1);
		transmit(&root,cp,ep - cp,msg,avoid);
		cp = ep + 1;
	} while (*ep);

	while (list != NULL) {
		if (list->flag) {
			gale_dprintf(4,"[%d] transmitting message\n",list->wfd);
			link_put(list->link,msg);
		}
		list = list->sub_next;
	}
}
