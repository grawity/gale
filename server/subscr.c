#include <assert.h>
#include <string.h>

#include "subscr.h"
#include "connect.h"
#include "server.h"
#include "gale/message.h"
#include "gale/link.h"
#include "gale/util.h"

struct node {
	char *spec;
	int len,alloc;
	struct node *child;
	struct node *next;
	int num,size;
	struct connect **array;
};

int stamp = 0;
struct node root = { "",0,0,NULL,NULL,0,0,NULL };

static void add(struct node *ptr,const char *spec,int len,
                struct connect *conn) 
{
	struct node *child,*node;
	int i;

	if (len == 0) {
		dprintf(4,"+++ adding connection to node\n");
		if (ptr->num == ptr->size) {
			struct connect **old = ptr->array;
			ptr->size = ptr->size ? ptr->size * 2 : 10;
			ptr->array = gale_malloc(ptr->size * sizeof(void *));
			memcpy(ptr->array,old,ptr->num * sizeof(void *));
			if (old) gale_free(old);
		}
		ptr->array[ptr->num++] = conn;
		return;
	}

	child = ptr->child;
	while (child != NULL && child->spec[0] != spec[0]) {
		child = child->next;
	}

	if (child == NULL) {
		dprintf(4,"+++ creating new node for \"%.*s\"\n",len,spec);
		node = gale_malloc(sizeof(struct node));
		node->spec = gale_malloc(node->alloc = len);
		node->len = len;
		strncpy(node->spec,spec,len);
		node->child = NULL;
		node->next = ptr->child;
		ptr->child = node;
		node->size = node->num = 0;
		node->array = NULL;
		add(node,spec + len,0,conn);
		return;
	}

	i = 0;
	while (i < len && i < child->len && spec[i] == child->spec[i]) ++i;

	if (i != child->len) {
		dprintf(4,"+++ truncating \"%.*s\" node to \"%.*s\"\n",
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
		dprintf(4,"+++ matched \"%.*s\"\n",child->len,child->spec);

	add(child,spec + i,len - i,conn);
}

void add_subscr(struct connect *conn) {
	const char *ep,*cp = conn->subscr;
	do {
		for (ep = cp; *ep && *ep != ':'; ++ep) ;
		dprintf(3,"[%d] subscribing to \"%.*s\"\n",
		        conn->rfd,ep - cp,cp);
		add(&root,cp,ep - cp,conn);
		cp = ep + 1;
	} while (*ep);
}

static void remove(struct node *ptr,const char *spec,int len,
                   struct connect *conn)
{
	struct node *parent = NULL,*prev;
	int i;

	dprintf(3,"[%d] unsubscribing from \"%.*s\"\n",conn->rfd,len,spec);

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
		dprintf(4,"--- matched \"%.*s\"\n",ptr->len,ptr->spec);
		spec += ptr->len;
		len -= ptr->len;
	}

	dprintf(4,"--- removing connection from node\n");
	for (i = 0; i < ptr->num && ptr->array[i] != conn; ++i) ;
	assert(i != ptr->num);
	ptr->array[i] = ptr->array[--ptr->num];

	if (!parent) {
		dprintf(4,"--- root node, all done\n");
		return;
	}

	if (ptr->num) {
		dprintf(4,"--- node still has other connections, all done\n");
		return;
	}

	if (ptr->child && ptr->child->next) {
		dprintf(4,"--- node has more than one child, all done\n");
		return;
	}

	if (!ptr->child) {
		dprintf(4,"--- removing node\n");
		if (prev)
			prev->next = ptr->next;
		else
			parent->child = ptr->next;
		if (ptr->spec) gale_free(ptr->spec);
		if (ptr->array) gale_free(ptr->array);
		gale_free(ptr);

		if (parent->num) {
			dprintf(4,"--- parent has connections, all done\n");
			return;
		}
		if (parent == &root) {
			dprintf(4,"--- parent is root, all done\n");
			return;
		}
		assert(parent->child);
		if (parent->child->next) {
			dprintf(4,"--- parent has more than one child,"
			          " all done\n");
			return;
		}
		dprintf(4,"--- moving to parent ...\n");
		ptr = parent;
	}

	prev = ptr->child;
	dprintf(4,"--- merging with singleton child \"%.*s\"\n",
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

void remove_subscr(struct connect *conn) {
	const char *ep,*cp = conn->subscr;
	do {
		for (ep = cp; *ep && *ep != ':'; ++ep) ;
		remove(&root,cp,ep - cp,conn);
		cp = ep + 1;
	} while (*ep);
}

static void transmit(struct node *ptr,const char *spec,int len,
                     struct gale_message *msg,struct connect *avoid)
{
	int i;
	if (len < 0) return;
	for (i = 0; i < ptr->num; ++i)
		if (ptr->array[i]->stamp != stamp && ptr->array[i] != avoid) {
			dprintf(4,"[%d] transmitting message\n",
			        ptr->array[i]->wfd);
			link_put(ptr->array[i]->link,msg);
			ptr->array[i]->stamp = stamp;
		}
	for (ptr = ptr->child; ptr; ptr = ptr->next)
		if (ptr->len <= len && !memcmp(ptr->spec,spec,ptr->len)) {
			dprintf(4,"*** matched \"%.*s\"\n",ptr->len,ptr->spec);
			transmit(ptr,spec + ptr->len,len - ptr->len,msg,avoid);
		}
}

void subscr_transmit(struct gale_message *msg,struct connect *avoid) {
	const char *ep,*cp = msg->category;
	++stamp;
	do {
		for (ep = cp; *ep && *ep != ':'; ++ep) ;
		dprintf(3,"*** transmitting \"%.*s\", avoiding [%d]\n",
		        ep - cp,cp,avoid ? avoid->rfd : -1);
		transmit(&root,cp,ep - cp,msg,avoid);
		cp = ep + 1;
	} while (*ep);
}
