#include <assert.h>
#include "gale/misc.h"

#include "global.h"
#include "rsaref.h"

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
	struct gale_group n,*gn;
	struct gale_fragment *list;

	gale_create(list);
	list[0] = f;
	n.list = list;
	n.len = 1;

	gale_create(gn);
	*gn = *g;
	n.next = gn;

	*g = n;
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

int gale_unpack_group(struct gale_data *data,struct gale_group *group) {
	u32 type,len;

	*group = gale_group_empty();
	while (gale_unpack_u32(data,&type)) {
		struct gale_data fdata;
		struct gale_fragment frag;
		struct gale_group sub;
		size_t size;
		u32 num;

		if (!gale_unpack_u32(data,&len) || len > data->l) goto warning;
		fdata.p = data->p;
		fdata.l = len;
		data->p += len;
		data->l -= len;

		if (type > max_fragment) continue;
		if (!gale_unpack_text(&fdata,&frag.name)) goto warning;

		switch (type) {
		case fragment_text:
			frag.type = frag_text;
			size = fdata.l / gale_wch_size();
			if (!gale_unpack_text_len(&fdata,size,
			                          &frag.value.text))
				goto warning;
			break;
		case fragment_data:
			frag.type = frag_data;
			frag.value.data = gale_data_copy(fdata);
			fdata.p += fdata.l;
			fdata.l -= fdata.l;
			break;
		case fragment_time:
			frag.type = frag_time;
			if (!gale_unpack_time(&fdata,&frag.value.time))
				goto warning;
			break;
		case fragment_number:
			frag.type = frag_number;
			if (!gale_unpack_u32(&fdata,&num)) goto warning;
			frag.value.number = (s32) num;
			break;
		case fragment_group:
			frag.type = frag_group;
			if (!gale_unpack_group(&fdata,&sub)) goto warning;
			frag.value.group = sub;
			break;
		default:
			assert(0);
		}

		if (0 != fdata.l) {
		warning:
			gale_alert(GALE_WARNING,G_("invalid fragment"),0);
			frag.name = G_("error");
			frag.type = frag_data;
			frag.value.data = gale_data_copy(fdata);
		}

		gale_group_add(group,frag);
	}

	return 1;
}

void gale_pack_group(struct gale_data *data,struct gale_group group) {
	while (!gale_group_null(group)) {
		struct gale_fragment frag = gale_group_first(group);
		size_t len = gale_text_size(frag.name);
		group = gale_group_rest(group);

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
}

size_t gale_group_size(struct gale_group group) {
	size_t size = 0;

	while (!gale_group_null(group)) {
		struct gale_fragment frag = gale_group_first(group);
		group = gale_group_rest(group);
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
	}

	return size;
}

struct gale_text gale_print_fragment(struct gale_fragment frag) {
	struct gale_time t;
	struct gale_text r = null_text;
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
		if (20 >= frag.value.data.l)
		{
			int i;
			r = gale_text_concat(2,r,G_("["));
			for (i = 0; i < frag.value.data.l; ++i)
				r = gale_text_concat(3,r,
					i ? G_(" ") : G_(""),
					gale_text_from_number(frag.value.data.p[i],16,2));
			r = gale_text_concat(2,r,G_("]"));
		}
		else
		{
			int i;
			u8 hash[16];
			MD5_CTX ctx;
			MD5Init(&ctx);
			MD5Update(&ctx,frag.value.data.p,frag.value.data.l);
			MD5Final(hash,&ctx);
			for (i = 0; i < 16; ++i)
				r = gale_text_concat(3,r,
					i ? G_(" ") : G_("{"),
					gale_text_from_number(hash[i],16,2));
			r = gale_text_concat(2,r,G_("}"));
		}
		return r;

	case frag_group:
		return G_("(nested group)");

	default:
		return G_("(unknown type)");
	}
}

struct gale_text gale_print_group(struct gale_group grp,int indent) {
	struct gale_text i,t = null_text;
	wch *ch;
	gale_create_array(ch,indent);
	i.p = ch;
	i.l = indent;
	while (indent--) ch[indent] = ' ';

	while (!gale_group_null(grp))
	{
		struct gale_fragment frag = gale_group_first(grp);
		t = gale_text_concat(6,
			t,i,frag.name,G_(": "),
			gale_print_fragment(frag),
			G_("\n"));
		grp = gale_group_rest(grp);
	}
	return t;
}
