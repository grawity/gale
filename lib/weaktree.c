#include "gale/misc.h"

#include <assert.h>

struct wt_node {
	struct gale_text key;
	void *data;
	struct wt_node *left,*right;
};

struct gale_wt {
	struct wt_node *root;
};

struct gale_wt *gale_make_wt() {
	struct gale_wt *wt;
	gale_create(wt);
	wt->root = NULL;
}

static struct wt_node **find(struct gale_wt *wt,struct gale_text key) {
	struct wt_node **p = &wt->root;
	while (*p) {
		int x = gale_text_compare(key,(*p)->key);
		if (x < 0) 
			p = &(*p)->left;
		else if (x > 0)
			p = &(*p)->right;
		else
			break;
	}

	return p;
}

void gale_wt_add(struct gale_wt *wt,struct gale_text key,void *data) {
	struct wt_node *new,**p;

	gale_create(new); /* This must happen first! */
	p = find(wt,key);

	if (NULL != *p) {
		new = *p;
		assert(NULL == new->data); /* No duplicates allowed! */
	} else {
		*p = new;
		new->key = key;
		new->left = NULL;
		new->right = NULL;
	}

	assert(NULL != data);
	new->data = data;
	gale_weak_ptr(&new->data);
}

void *gale_wt_find(struct gale_wt *wt,struct gale_text key) {
	struct wt_node *n = *(find(wt,key));
	return n ? n->data : NULL;
}
