#include "gale/misc.h"

#include <assert.h>

struct wt_node {
	struct gale_data key;
	struct gale_ptr *data;
	struct wt_node *left,*right;
};

struct gale_map {
	struct wt_node *root;
	int weak;
};

struct gale_map *gale_make_map(int weak) {
	struct gale_map *wt;
	gale_create(wt);
	wt->root = NULL;
	wt->weak = weak;
	return wt;
}

static struct wt_node **find(struct gale_map *wt,struct gale_data key) {
	struct wt_node **p = &wt->root;
	while (*p) {
		int x = gale_data_compare(key,(*p)->key);
		if (x < 0) 
			p = &(*p)->left;
		else if (x > 0)
			p = &(*p)->right;
		else
			break;
	}

	return p;
}

void gale_map_add(struct gale_map *wt,struct gale_data key,void *data) {
	struct wt_node *new = NULL,**p;

	if (NULL != data) gale_create(new); /* This must happen first! */
	p = find(wt,key);

	if (NULL != *p) new = *p;
	else if (NULL == data) return;
	else {
		*p = new;
		new->key = key;
		new->left = NULL;
		new->right = NULL;
	}

	if (NULL == data) new->data = NULL;
	else if (wt->weak) new->data = gale_make_weak(data);
	else new->data = gale_make_ptr(data);
}

void *gale_map_find(const struct gale_map *wt,struct gale_data key) {
	struct wt_node *n = *(find((struct gale_map *) wt,key));
	return n ? gale_get_ptr(n->data) : NULL;
}

static int empty(const struct wt_node *node) {
	return (NULL == node->right
            &&  NULL == node->left
	    && (NULL == node->data || NULL == gale_get_ptr(node->data)));
}

static int walk(struct wt_node *node,const struct gale_data *after,
	        struct gale_data *key,void **data) 
{
	int x = after ? gale_data_compare(*after,node->key) : -1;

	if (x < 0 && NULL != node->left) {
		if (walk(node->left,after,key,data)) return 1;
		if (empty(node->left)) node->left = NULL;
	}

	if (x < 0 && NULL != node->data) {
		void *ptr = gale_get_ptr(node->data);
		if (NULL != data) *data = ptr;
		if (NULL == ptr) node->data = NULL;
		else {
			if (NULL != key) *key = node->key;
			return 1;
		}
	}

	if (NULL != node->right) {
		if (x <= 0) after = NULL;
		if (walk(node->right,after,key,data)) return 1;
		if (empty(node->right)) node->right = NULL;
	}

	return 0;
}

int gale_map_walk(const struct gale_map *wt,const struct gale_data *after,
                  struct gale_data *key,void **data) {
	if (NULL == wt->root) return 0;
	if (walk(wt->root,after,key,data)) return 1;
	if (empty(wt->root)) ((struct gale_map *) wt)->root = NULL;
	return 0;
}
