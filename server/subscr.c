#include "gale/core.h"
#include "gale/misc.h"
#include "gale/globals.h"

#include "subscr.h"
#include "connect.h"
#include "directed.h"

#include <assert.h>
#include <string.h>

struct sub_connect {
	int flag,priority,stamp;
	struct sub_connect *next;
	struct connect *link;
};

struct sub {
	int flag,priority;
	struct sub_connect *connect;
};

struct node {
	struct gale_text spec;
	struct node *child;
	struct node *next;
	int num,size;
	struct sub *array;
};

static int stamp = 0;
static wch null = 0;
static const struct gale_text empty = { &null,0 };
static struct node root = { { &null,0 },NULL,NULL,0,0,NULL };
static struct sub_connect *list = NULL;

static void add(struct node *ptr,struct gale_text spec,struct sub *sub) {
	struct node *child,*node;
	size_t i;

	gale_dprintf(3,"[%p] subscribing to \"%s\"\n",
		sub->connect->link,
		gale_text_to(gale_global->enc_console,spec));

	if (spec.l == 0) {
		gale_dprintf(4,"+++ adding connection to node\n");
		if (ptr->num == ptr->size) {
			struct sub *old = ptr->array;
			ptr->size = ptr->size ? ptr->size * 2 : 10;
			ptr->array = gale_malloc(ptr->size * sizeof(*old));
			memcpy(ptr->array,old,ptr->num * sizeof(*old));
		}
		ptr->array[ptr->num++] = *sub;
		return;
	}

	child = ptr->child;
	while (child != NULL && child->spec.p[0] != spec.p[0])
		child = child->next;

	if (child == NULL) {
		gale_dprintf(4,"+++ new node \"%s\"\n",
			gale_text_to(gale_global->enc_console,spec));
		gale_create(node);
		node->spec = spec;
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
			gale_text_to(gale_global->enc_console,child->spec),
			gale_text_to(gale_global->enc_console,gale_text_left(spec,i)));
		gale_create(node);
		node->spec = gale_text_right(child->spec,-i);
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
			gale_text_to(gale_global->enc_console,child->spec));

	add(child,gale_text_right(spec,-i),sub);
}

static int same_sub(const struct sub *a,const struct sub *b) {
	return (a->priority == b->priority && a->flag == b->flag &&
	        a->connect->link == b->connect->link);
}

static void do_remove(struct node *ptr,struct gale_text spec,struct sub *sub) {
	struct node *parent = NULL,*prev = NULL;
	int i;

	gale_dprintf(3,"[%p] unsubscribing from \"%s\"\n",
		sub->connect->link,
		gale_text_to(gale_global->enc_console,spec));

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
			gale_text_to(gale_global->enc_console,ptr->spec));
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
		gale_text_to(gale_global->enc_console,prev->spec));

	ptr->spec = gale_text_concat(2,ptr->spec,prev->spec);
	ptr->array = prev->array;
	ptr->num = prev->num;
	ptr->size = prev->size;
	ptr->child = prev->child;
}

static void subscr(oop_source *src,struct gale_text spec,struct connect *link,
                   void (*func)(struct node *,struct gale_text,struct sub *),
                   void (*dir)(oop_source *,struct gale_text))
{
	struct gale_text cat = null_text;
	struct sub sub;
	sub.priority = 0;
	gale_create(sub.connect);
	sub.connect->stamp = stamp;
	sub.connect->link = link;

	/* easy escape */
	if (!gale_text_compare(spec,G_("-"))) return;

	gale_dprintf(3,"--- subscribing to all of \"%s\"\n",
		gale_text_to(gale_global->enc_console,spec));

	while (gale_text_token(spec,':',&cat)) {
		struct gale_text host,base;
		if (is_directed(cat,&sub.flag,&base,&host)) dir(src,host);
		func(&root,base,&sub);
		++sub.priority;
	}
}

int category_flag(struct gale_text cat,struct gale_text *base) {
	if (cat.l > 0 && cat.p[0] == '+') {
		*base = gale_text_right(cat,-1);
		return 1;
	} else if (cat.l > 0 && cat.p[0] == '-') {
		*base = gale_text_right(cat,-1);
		return 0;
	}

	*base = cat;
	return 1;
}

struct gale_text category_escape(struct gale_text cat,int flag) {
	if (!flag) return gale_text_concat(2,G_("-"),cat);
	if (cat.l < 1 || (cat.p[0] != '+' && cat.p[0] != '-')) return cat;
	return gale_text_concat(2,G_("+"),cat);
}

void add_subscr(oop_source *src,struct gale_text sub,struct connect *link) {
	subscr(src,sub,link,add,sub_directed);
}

void remove_subscr(oop_source *src,struct gale_text sub,struct connect *link) {
	subscr(src,sub,link,do_remove,unsub_directed);
}

static void transmit(struct node *ptr,struct gale_text spec,
                     struct connect *avoid,int flag)
{
	int i;
	if (ptr != &root || spec.l < 1 || spec.p[0] != '@')
	for (i = 0; i < ptr->num; ++i) {
		if (ptr->array[i].connect->link == avoid) continue;
		if (ptr->array[i].connect->stamp != stamp) {
			ptr->array[i].connect->next = list;
			list = ptr->array[i].connect;
			ptr->array[i].connect->stamp = stamp;
			ptr->array[i].connect->priority = -1;
		}
		if (ptr->array[i].connect->priority > ptr->array[i].priority)
			continue;
		ptr->array[i].connect->priority = ptr->array[i].priority;
		ptr->array[i].connect->flag = ptr->array[i].flag && flag;
	}

	for (ptr = ptr->child; ptr; ptr = ptr->next)
		if (ptr->spec.l <= spec.l && !gale_text_compare(ptr->spec,
			gale_text_left(spec,ptr->spec.l))) 
		{
			gale_dprintf(4,"*** matched \"%s\"\n",
				gale_text_to(gale_global->enc_console,ptr->spec));
			transmit(ptr,gale_text_right(spec,-ptr->spec.l),
				 avoid,flag);
		}
}

void subscr_transmit(
	oop_source *src,
	struct gale_message *msg,struct connect *avoid) 
{
	struct gale_text cat = null_text;
	struct gale_message *rewrite = new_message();
	while (gale_text_token(msg->cat,':',&cat)) {
		struct gale_text host;
		if (is_directed(cat,NULL,NULL,&host)) 
			send_directed(src,host);
	}

	++stamp;
	assert(list == NULL);
	cat = null_text;
	rewrite->data = msg->data;
	while (gale_text_token(msg->cat,':',&cat)) {
		struct gale_text base,host;
		int flag;
		gale_dprintf(3,"*** transmitting \"%s\"\n",
		             gale_text_to(gale_global->enc_console,cat));
		is_directed(cat,&flag,&base,&host);
		transmit(&root,base,avoid,flag);
		base = category_escape(base,1);
		rewrite->cat = gale_text_concat(3,rewrite->cat,G_(":"),base);
	}

	/* strip leading colon */
	if (rewrite->cat.l > 0) rewrite->cat = gale_text_right(rewrite->cat,-1);

	while (list != NULL) {
		if (list->flag) {
			gale_dprintf(4,"[%p] sending message\n",list->link);
			send_connect(list->link,rewrite);
		}
		list = list->next;
	}
}
