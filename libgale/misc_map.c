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

static struct wt_node *distill(struct wt_node *node) {
	struct wt_node **parent,*temp;

	if (NULL == node) return NULL;
	if (NULL != gale_get_ptr(node->data)) return node;
	if (NULL == (node->right = distill(node->right)))
		return distill(node->left);
	if (NULL == (node->left = distill(node->left)))
		return node->right;

	parent = &node->left;
	while (NULL != (*parent)->right)
		parent = &(*parent)->right;

	temp = *parent;
	*parent = temp->left;
	temp->left = node->left;
	temp->right = node->right;
	return distill(temp);
}

static struct wt_node **find(struct gale_map *wt,struct gale_data key) {
	struct wt_node **p = &wt->root;
	while (NULL != (*p = distill(*p))) {
		const int x = gale_data_compare(key,(*p)->key);
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

	if (NULL != *p) 
		new = *p;
	else if (NULL == data) 
		return;
	else {
		new->key = key;
		new->left = NULL;
		new->right = NULL;
	}

	new->data = (NULL == data) ? NULL
	          : (wt->weak ? gale_make_weak : gale_make_ptr)(data);
	*p = distill(new);
}

void *gale_map_find(const struct gale_map *wt,struct gale_data key) {
	struct wt_node *n = *(find((struct gale_map *) wt,key));
	return n ? gale_get_ptr(n->data) : NULL;
}

static int walk(
	struct wt_node *node,
	const struct gale_data *after,
	struct gale_data *key,void **data) 
{
	int x;
	if (NULL == node) return 0;
	x = after ? gale_data_compare(*after,node->key) : -1;

	if (x < 0) {
		node->left = distill(node->left);
		if (walk(node->left,after,key,data)) return 1;
		if (NULL != data) *data = gale_get_ptr(node->data);
		if (NULL != key) *key = node->key;
		return 1;
	}

	node->right = distill(node->right);
	return walk(node->right,after,key,data);
}

int gale_map_walk(
	const struct gale_map *wt,
	const struct gale_data *after,
	struct gale_data *key,void **data) 
{
	((struct gale_map *) wt)->root = distill(wt->root);
	return walk(wt->root,after,key,data);
}
