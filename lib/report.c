#include "gale/misc.h"

#include <string.h>
#include <assert.h>

struct entry {
	gale_report_call *func;
	void *data;
};

static struct gale_text meta(void *d) {
	return gale_report_run((struct gale_report *) d);
}

struct gale_report *gale_make_report(struct gale_report *outer) {
	struct gale_report *ret = (struct gale_report *) gale_make_wt(0);
	if (NULL != outer) gale_report_add(outer,meta,ret);
	return ret;
}

void gale_report_add(struct gale_report *rep,gale_report_call *func,void *data) {
	struct entry *ent;
	struct gale_data key;

	gale_create(ent);
	key.p = (void *) ent;
	key.l = sizeof(*ent);
	memset(key.p,0,key.l);

	ent->func = func;
	ent->data = data;
	gale_wt_add((struct gale_wt *) rep,key,rep);
}

void gale_report_remove(struct gale_report *rep,gale_report_call *func,void *data) {
	struct entry ent;
	struct gale_data key;

	key.p = (void *) &ent;
	key.l = sizeof(ent);
	memset(key.p,0,key.l);

	ent.func = func;
	ent.data = data;
	gale_wt_add((struct gale_wt *) rep,key,NULL);
}

struct gale_text gale_report_run(struct gale_report *rep) {
	struct gale_wt *tree = (struct gale_wt *) rep;
	struct gale_data key,*after = NULL;
	struct gale_text ret;
	int alloc = 0,len = 0;
	wch *buffer = 0;
	void *data;

	while (gale_wt_walk(tree,after,&key,&data))
	{
		struct entry *ent = (struct entry *) key.p;
		struct gale_text text = ent->func(ent->data);
		assert(data == rep);
		if (text.l + len > alloc)
		{
			wch *newbuf = gale_malloc(
				sizeof(wch) * (alloc = 2*(text.l + len)));
			memcpy(newbuf,buffer,len * sizeof(wch));
			buffer = newbuf;
		}
		memcpy(buffer + len,text.p,text.l * sizeof(wch));
		len += text.l;
		after = &key;
	}

	ret.p = buffer;
	ret.l = len;
	return ret;
}
