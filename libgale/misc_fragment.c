#include "gale/misc.h"
#include "gale/crypto.h" /* for hash */

#include <assert.h>
#include <time.h>

#define fragment_text 0
#define fragment_data 1
#define fragment_time 2
#define fragment_number 3
#define fragment_group 4
#define max_fragment 5

struct gale_group gale_group_empty(void) {
	struct gale_group g;
	g.list = NULL;
	g.len = 0;
	g.next = NULL;
	return g;
}

void gale_group_add(struct gale_group *g,struct gale_fragment f) {
	struct gale_group *gn;
	struct gale_fragment *list;

	gale_create(gn);
	*gn = *g;

	gale_create(list);
	list[0] = f;
	g->list = list;
	g->len = 1;
	g->next = gn;
}

void gale_group_append(struct gale_group *g,struct gale_group ga) {
	struct gale_group n,*t;
	const struct gale_group *p;
	struct gale_fragment *list;

	if (gale_group_null(ga)) return;
	if (gale_group_null(*g)) {
		*g = ga;
		return;
	}

	n.len = 0;
	for (p = g; p != NULL; p = p->next) n.len += p->len;
	gale_create_array(list,n.len);
	n.list = list;
	for (p = g; p != NULL; p = p->next) {
		memcpy(list,p->list,sizeof(*list) * p->len);
		list += p->len;
	}

	gale_create(t);
	*t = ga;
	n.next = t;

	*g = n;
}

struct gale_group gale_group_find(struct gale_group g,struct gale_text name) {
	while (!gale_group_null(g) && 
	       gale_text_compare(gale_group_first(g).name,name))
		g = gale_group_rest(g);
	return g;
}

int gale_group_lookup(struct gale_group group,struct gale_text name,
                      enum gale_fragment_type type,struct gale_fragment *frag)
{
	struct gale_group g = gale_group_find(group,name);

	while (!gale_group_null(g)) {
		struct gale_fragment f = gale_group_first(g);
		assert(0 == gale_text_compare(f.name,name));
		if (f.type == type) {
			*frag = f;
			return 1;
		}
		g = gale_group_find(gale_group_rest(g),name);
	}

	return 0;
}

int gale_group_remove(struct gale_group *g,struct gale_text name) {
	struct gale_group t,r = *g;
	int c = 0;

	*g = gale_group_empty();
	while (!gale_group_null((t = gale_group_find(r,name)))) {
		gale_group_prefix(&r,t);
		gale_group_append(g,r);
		r = gale_group_rest(t);
		++c;
	}

	gale_group_append(g,r);
	return c;
}

void gale_group_replace(struct gale_group *g,struct gale_fragment f) {
	struct gale_group t = gale_group_find(*g,f.name);
	if (!gale_group_null(t)) {
		gale_group_prefix(g,t);
		t = gale_group_rest(t);
	}
	gale_group_add(&t,f);
	gale_group_append(g,t);
}

int gale_group_null(struct gale_group g) {
	return g.len == 0 && (g.next == NULL || gale_group_null(*g.next));
}

struct gale_fragment gale_group_first(struct gale_group g) {
	assert (!gale_group_null(g) && "car of an atom");
	while (g.len == 0) g = *g.next;
	return g.list[0];
}

struct gale_group gale_group_rest(struct gale_group g) {
	assert (!gale_group_null(g) && "car of an atom");
	while (g.len == 0) g = *g.next;
	++g.list;
	--g.len;
	return g;
}

void gale_group_prefix(struct gale_group *g,struct gale_group tail) {
	struct gale_group n;
	const struct gale_group *p;
	struct gale_fragment *list;
	n.len = 0;
	for (p = g; p->next != tail.next; p = p->next) {
		assert(NULL != p->next);
		n.len += p->len;
	}
	assert(p->len >= tail.len);
	n.len += p->len - tail.len;
	gale_create_array(list,n.len);
	n.list = list;
	for (p = g; p->next != tail.next; p = p->next) {
		memcpy(list,p->list,sizeof(*list) * p->len);
		list += p->len;
	}
	memcpy(list,p->list,sizeof(*list) * (p->len - tail.len));
	n.next = NULL;
	*g = n;
}

int gale_unpack_fragment(struct gale_data *d,struct gale_fragment *f) {
	struct gale_data fdata;
	size_t size;
	u32 num,type,len;

	if (!gale_unpack_u32(d,&type) || type > max_fragment
	||  !gale_unpack_u32(d,&len) || len > d->l)
		return 0;

	/* point of no return */

	fdata.p = d->p;
	fdata.l = len;
	d->p += len;
	d->l -= len;

	if (!gale_unpack_text(&fdata,&f->name)) goto warning;

	switch (type) {
	case fragment_text:
		f->type = frag_text;
		size = fdata.l / gale_wch_size();
		if (!gale_unpack_text_len(&fdata,size,&f->value.text))
			goto warning;
		break;
	case fragment_data:
		f->type = frag_data;
		f->value.data = gale_data_copy(fdata);
		fdata = null_data;
		break;
	case fragment_time:
		f->type = frag_time;
		if (!gale_unpack_time(&fdata,&f->value.time))
			goto warning;
		break;
	case fragment_number:
		f->type = frag_number;
		if (!gale_unpack_u32(&fdata,&num)) 
			goto warning;
		f->value.number = (s32) num;
		break;
	case fragment_group:
		f->type = frag_group;
		if (!gale_unpack_group(&fdata,&f->value.group)) 
			goto warning;
		break;
	default:
		assert(0); /* checked above */
	}

	if (0 != fdata.l) {
	warning:
		gale_alert(GALE_WARNING,G_("invalid fragment"),0);
		f->name = G_("error");
		f->type = frag_data;
		f->value.data = gale_data_copy(fdata);
	}

	return 1;
}

size_t gale_fragment_size(struct gale_fragment frag) {
	size_t size = 0;
	size += gale_u32_size() * 2; /* type, length */
	size += gale_text_size(frag.name);
	switch (frag.type) {
	case frag_text:
		size += gale_text_len_size(frag.value.text);
		break;
	case frag_data:
		size += frag.value.data.l;
		break;
	case frag_time:
		size += gale_time_size();
		break;
	case frag_number:
		size += gale_u32_size();
		break;
	case frag_group:
		size += gale_group_size(frag.value.group);
		break;
	default:
		assert(0);
	}

	return size;
}

void gale_pack_fragment(struct gale_data *data,struct gale_fragment frag) {
	size_t len = gale_text_size(frag.name);

	switch (frag.type) {
	case frag_text:
		gale_pack_u32(data,fragment_text);
		gale_pack_u32(data,len + 
			      gale_text_len_size(frag.value.text));
		gale_pack_text(data,frag.name);
		gale_pack_text_len(data,frag.value.text);
		break;
	case frag_data:
		gale_pack_u32(data,fragment_data);
		gale_pack_u32(data,len + frag.value.data.l);
		gale_pack_text(data,frag.name);
		gale_pack_copy(data,
			       frag.value.data.p,frag.value.data.l);
		break;
	case frag_time:
		gale_pack_u32(data,fragment_time);
		gale_pack_u32(data,len + gale_time_size());
		gale_pack_text(data,frag.name);
		gale_pack_time(data,frag.value.time);
		break;
	case frag_number:
		gale_pack_u32(data,fragment_number);
		gale_pack_u32(data,len + gale_u32_size());
		gale_pack_text(data,frag.name);
		gale_pack_u32(data,(u32) frag.value.number);
		break;
	case frag_group:
		gale_pack_u32(data,fragment_group);
		gale_pack_u32(data,len + 
			      gale_group_size(frag.value.group));
		gale_pack_text(data,frag.name);
		gale_pack_group(data,frag.value.group);
		break;
	default:
		assert(0);
	}
}

int gale_unpack_group(struct gale_data *data,struct gale_group *group) {
	struct gale_fragment list[100],*copy;
	struct gale_group *next;
	int count = 0;

	while (gale_unpack_fragment(data,&list[count])) {
		if (++count == sizeof(list)/sizeof(list[0])) {
			gale_create_array(copy,group->len = count);
			memcpy(copy,list,sizeof(list[0]) * count);
			gale_create(next);
			group->list = copy;
			group->next = next;
			group = next;
			count = 0;
		}
	}

	gale_create_array(copy,group->len = count);
	memcpy(copy,list,sizeof(list[0]) * count);
	group->list = copy;
	group->next = NULL;
	return 1;
}

size_t gale_group_size(struct gale_group group) {
	size_t size = 0;
	while (!gale_group_null(group)) {
		size += gale_fragment_size(gale_group_first(group));
		group = gale_group_rest(group);
	}

	return size;
}

void gale_pack_group(struct gale_data *data,struct gale_group group) {
	while (!gale_group_null(group)) {
		gale_pack_fragment(data,gale_group_first(group));
		group = gale_group_rest(group);
	}
}

struct gale_text gale_print_fragment(struct gale_fragment frag,int indent) {
	struct gale_time t;
	struct gale_data d;
	switch (frag.type) {
	case frag_text:
		return gale_text_concat(3,G_("\""),frag.value.text,G_("\""));

	case frag_time:
		t = frag.value.time;
		if (0 == gale_time_compare(gale_time_zero(),t))
			return G_("(long ago)");
		else if (0 == gale_time_compare(gale_time_forever(),t))
			return G_("(never)");
		else
		{
			struct timeval tv;
			time_t sec;
			char buf[30];
			gale_time_to(&tv,t);
			sec = tv.tv_sec;
			strftime(buf,sizeof(buf),"%Y-%m-%d %H:%M",
				 localtime(&sec));
			return gale_text_from(NULL,buf,-1);
		}

	case frag_number:
		return gale_text_from_number(frag.value.number,10,0);

	case frag_data:
		if (3*frag.value.data.l < 82 - indent)
		{
			int i;
			struct gale_text r = G_("[");
			for (i = 0; i < frag.value.data.l; ++i)
				r = gale_text_concat(3,r,
					i ? G_(" ") : G_(""),
					gale_text_from_number(frag.value.data.p[i],16,-2));
			return gale_text_concat(2,r,G_("]"));
		}

		frag.value.data = gale_crypto_hash((d = frag.value.data));
		if (frag.value.data.l > 8) frag.value.data.l = 8;
		return gale_text_concat(3,
			gale_text_from_number(d.l,10,0),
			G_(" bytes, hash = "),
			gale_print_fragment(frag,0));

	case frag_group:
		return gale_print_group(frag.value.group,indent);

	default:
		return G_("(unknown type)");
	}
}

struct gale_text gale_print_group(struct gale_group grp,int indent) {
	struct gale_text i = null_text,t = null_text;

	while (!gale_group_null(grp))
	{
		struct gale_fragment frag = gale_group_first(grp);
		t = gale_text_concat(5,
			t,i,frag.name,G_(": "),
			gale_print_fragment(frag,
				i.l + indent + frag.name.l + 2));
		grp = gale_group_rest(grp);
		if (indent >= 0) {
			wch *ch;
			gale_create_array(ch,1 + indent);
			i.p = ch;
			i.l = 1 + indent;
			while (indent--) ch[1 + indent] = ' ';
			ch[0] = '\n';
		}
	}
	return t;
}

int gale_fragment_compare(struct gale_fragment a,struct gale_fragment b) {
	if (a.type < b.type) return -1;
	if (a.type > b.type) return 1;
	switch (a.type) {
	case frag_text:
		return gale_text_compare(a.value.text,b.value.text);
	case frag_number:
		if (a.value.number < b.value.number) return -1;
		if (a.value.number > b.value.number) return 1;
		return 0;
	case frag_time:
		return gale_time_compare(a.value.time,b.value.time);
	case frag_data:
		return gale_data_compare(a.value.data,b.value.data);
	case frag_group:
		return gale_group_compare(a.value.group,b.value.group);
	default:
		assert(0);
		return 0;
	}
}

int gale_group_compare(struct gale_group a,struct gale_group b) {
	int i;
	if (gale_group_null(a) && gale_group_null(b)) return 0;
	if (gale_group_null(a)) return -1;
	if (gale_group_null(b)) return 1;
	i = gale_fragment_compare(gale_group_first(a),gale_group_first(b));
	if (0 != i) return i;
	return gale_group_compare(gale_group_rest(a),gale_group_rest(b));
}
