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
	struct gale_text spec;
	size_t alloc;
	struct node *child;
	struct node *next;
	int num,size;
	struct sub *array;
};

static int stamp = 0;
static wch null = 0;
static const struct gale_text empty = { &null,0 };
static struct node root = { { &null,0 },0,NULL,NULL,0,0,NULL };
static struct connect *list = NULL;

static void add(struct node *ptr,struct gale_text spec,struct sub *sub) {
	struct node *child,*node;
	size_t i;

	gale_dprintf(3,"[%d] subscribing to \"%s\"\n",
		sub->connect->rfd,gale_text_hack(spec));

	if (spec.l == 0) {
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
	while (child != NULL && child->spec.p[0] != spec.p[0])
		child = child->next;

	if (child == NULL) {
		gale_dprintf(4,"+++ new node \"%s\"\n",gale_text_hack(spec));
		node = gale_malloc(sizeof(struct node));
		node->spec = gale_text_dup(spec);
		node->alloc = node->spec.l;
		node->child = NULL;
		node->next = ptr->child;
		ptr->child = node;
		node->size = node->num = 0;
		node->array = NULL;
		add(node,empty,sub);
		return;
	}

	i = 0;
	while (i < spec.l && i < child->spec.l && spec.p[i] == child->spec.p[i])
		++i;

	if (i != child->spec.l) {
		gale_dprintf(4,"+++ truncating \"%s\" node to \"%s\"\n",
			gale_text_hack(child->spec),
			gale_text_hack(gale_text_left(spec,i)));
		node = gale_malloc(sizeof(struct node));
		node->spec = gale_text_dup(gale_text_right(child->spec,-i));
		node->alloc = node->spec.l;
		node->child = child->child;
		node->next = NULL;
		node->array = child->array;
		node->size = child->size;
		node->num = child->num;
		child->child = node;
		child->array = NULL;
		child->size = child->num = 0;
		child->spec = gale_text_left(child->spec,i);
	} else
		gale_dprintf(4,"+++ matched \"%s\"\n",
			gale_text_hack(child->spec));

	add(child,gale_text_right(spec,-i),sub);
}

static int same_sub(const struct sub *a,const struct sub *b) {
	return (a->priority == b->priority && a->flag == b->flag &&
	        a->connect == b->connect);
}

static void remove(struct node *ptr,struct gale_text spec,struct sub *sub) {
	struct node *parent = NULL,*prev = NULL;
	int i;

	gale_dprintf(3,"[%d] unsubscribing from \"%s\"\n",
	             sub->connect->rfd,gale_text_hack(spec));

	while (spec.l != 0) {
		parent = ptr;
		prev = NULL;
		ptr = ptr->child;
		while (ptr && ptr->spec.p[0] != spec.p[0]) {
			prev = ptr;
			ptr = ptr->next;
		}
		assert(ptr && ptr->spec.l <= spec.l &&
			!gale_text_compare(ptr->spec,
				gale_text_left(spec,ptr->spec.l)));
		gale_dprintf(4,"--- matched \"%s\"\n",
			gale_text_hack(ptr->spec));
		spec = gale_text_right(spec,-ptr->spec.l);
	}

	gale_dprintf(4,"--- removing connection from node\n");
	for (i = 0; i < ptr->num && !same_sub(&ptr->array[i],sub); ++i) ;
	assert(i != ptr->num);
	ptr->array[i] = ptr->array[--ptr->num];

	if (!parent) {
		gale_dprintf(4,"--- root node, done\n");
		return;
	}

	if (ptr->num) {
		gale_dprintf(4,"--- node still has other connections, done\n");
		return;
	}

	if (ptr->child && ptr->child->next) {
		gale_dprintf(4,"--- node has > 1 child, done\n");
		return;
	}

	if (!ptr->child) {
		gale_dprintf(4,"--- removing childless node\n");
		if (prev)
			prev->next = ptr->next;
		else
			parent->child = ptr->next;

		free_gale_text(ptr->spec);
		if (ptr->array) gale_free(ptr->array);
		gale_free(ptr);

		if (parent->num) {
			gale_dprintf(4,"--- parent has connections, done\n");
			return;
		}
		if (parent == &root) {
			gale_dprintf(4,"--- parent is root, done\n");
			return;
		}
		assert(parent->child);
		if (parent->child->next) {
			gale_dprintf(4,"--- parent has > 1 child, done\n");
			return;
		}
		gale_dprintf(4,"--- moving to parent ...\n");
		ptr = parent;
	}

	prev = ptr->child;
	gale_dprintf(4,"--- merging with singleton child \"%s\"\n",
		gale_text_hack(prev->spec));

	if (ptr->alloc < ptr->spec.l + prev->spec.l) {
		struct gale_text tmp;
		ptr->alloc = ptr->spec.l + prev->spec.l;
		tmp = new_gale_text(ptr->alloc);
		gale_text_append(&tmp,ptr->spec);
		free_gale_text(ptr->spec);
		ptr->spec = tmp;
	}

	gale_text_append(&ptr->spec,prev->spec);

	if (ptr->array) gale_free(ptr->array);
	ptr->array = prev->array;
	ptr->num = prev->num;
	ptr->size = prev->size;
	ptr->child = prev->child;

	free_gale_text(prev->spec);
	gale_free(prev);
}

static void subscr(struct connect *conn,
                   void (*func)(struct node *,struct gale_text,struct sub *))
{
	struct gale_text cat = null_text;
	struct sub sub;
	sub.connect = conn;
	sub.priority = 0;
	conn->stamp = stamp;

	gale_dprintf(3,"--- subscribing to all of \"%s\"\n",
		gale_text_hack(conn->subscr));

	while (gale_text_token(conn->subscr,':',&cat)) {
		sub.flag = 1;
		if (cat.l > 0 && cat.p[0] == '+')
			cat = gale_text_right(cat,-1);
		else if (cat.l > 0 && cat.p[0] == '-') {
			sub.flag = 0;
			cat = gale_text_right(cat,-1);
		}
		func(&root,cat,&sub);
		++sub.priority;
	}
}

void add_subscr(struct connect *conn) {
	subscr(conn,add);
}

void remove_subscr(struct connect *conn) {
	subscr(conn,remove);
}

static void transmit(struct node *ptr,struct gale_text spec,
                     struct gale_message *msg,struct connect *avoid)
{
	int i;
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
		if (ptr->spec.l <= spec.l && !gale_text_compare(ptr->spec,
			gale_text_left(spec,ptr->spec.l))) 
		{
			gale_dprintf(4,"*** matched \"%s\"\n",
				gale_text_hack(ptr->spec));
			transmit(ptr,gale_text_right(spec,-ptr->spec.l),
				msg,avoid);
		}
}

void subscr_transmit(struct gale_message *msg,struct connect *avoid) {
	struct gale_text cat = null_text;
	++stamp;

	assert(list == NULL);

	while (gale_text_token(msg->cat,':',&cat)) {
		gale_dprintf(3,"*** transmitting \"%s\", avoiding [%d]\n",
			gale_text_hack(cat),
			avoid ? avoid->rfd : -1);
		transmit(&root,cat,msg,avoid);
	}

	while (list != NULL) {
		if (list->flag) {
			gale_dprintf(4,"[%d] transmitting message\n",list->wfd);
			link_put(list->link,msg);
		}
		list = list->sub_next;
	}
}
