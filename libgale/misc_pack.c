#include "gale/misc.h"

#include <netinet/in.h>
#include <assert.h>
#include <string.h>

const struct gale_data null_data = { NULL, 0 };

void gale_pack_copy(struct gale_data *data,const void *p,size_t l) {
	memcpy(data->p + data->l,p,l);
	data->l += l;
}

int gale_unpack_copy(struct gale_data *data,void *p,size_t l) {
	if (data->l < l) return 0;
	memcpy(p,data->p,l);
	data->p += l;
	data->l -= l;
	return 1;
}

int gale_unpack_compare(struct gale_data *data,const void *p,size_t l) {
	if (data->l < l) return 0;
	if (memcmp(p,data->p,l)) return 0;
	data->p += l;
	data->l -= l;
	return 1;
}

void gale_pack_u32(struct gale_data *data,u32 u32) {
	u32 = htonl(u32);
	gale_pack_copy(data,&u32,sizeof(u32));
}

int gale_unpack_u32(struct gale_data *data,u32 *u32) {
	if (!gale_unpack_copy(data,u32,sizeof(*u32))) return 0;
	*u32 = ntohl(*u32);
	return 1;
}

void gale_pack_wch(struct gale_data *data,wch wch) {
	u16 u16 = htons(wch);
	gale_pack_copy(data,&u16,sizeof(u16));
}

int gale_unpack_wch(struct gale_data *data,wch *wch) {
	u16 u16;
	if (!gale_unpack_copy(data,&u16,sizeof(u16))) return 0;
	*wch = ntohs(u16);
	return 1;
}

void gale_pack_str(struct gale_data *data,const char *p) {
	int len = strlen(p) + 1;
	memcpy(data->p + data->l,p,len);
	data->l += len;
}

int gale_unpack_str(struct gale_data *data,const char **p) {
	const byte *nul = memchr(data->p,'\0',data->l);
	if (!nul) return 0;
	*p = (char*) data->p;
	data->l -= nul - data->p + 1;
	data->p += nul - data->p + 1;
	return 1;
}

void gale_pack_text(struct gale_data *data,struct gale_text t) {
	gale_pack_u32(data,t.l);
	gale_pack_text_len(data,t);
}

int gale_unpack_text(struct gale_data *data,struct gale_text *t) {
	u32 len;
	if (!gale_unpack_u32(data,&len)) return 0;
	if (len * gale_wch_size() > data->l) return 0;
	return gale_unpack_text_len(data,len,t);
}

void gale_pack_text_len(struct gale_data *data,struct gale_text t) {
	while (t.l) {
		gale_pack_wch(data,*t.p);
		++t.p;
		--t.l;
	}
}

int gale_unpack_text_len(struct gale_data *data,size_t len,struct gale_text *t)
{
	wch *buffer = gale_malloc(len * sizeof(*buffer));
	t->l = 0;
	while (len--)
		if (!gale_unpack_wch(data,&buffer[t->l++]))
			return 0;
	t->p = buffer;
	return 1;
}

void gale_pack_skip(struct gale_data *data,size_t len) {
	gale_pack_u32(data,len);
}

int gale_unpack_skip(struct gale_data *data) {
	u32 len;
	if (!gale_unpack_u32(data,&len)) return 0;
	if (data->l < len) return 0;
	data->l -= len;
	data->p += len;
	return 1;
}

void gale_pack_rle(struct gale_data *data,const void *p,size_t l) {
	const byte *ptr = p,*end = ptr;
	while (l) {
		int cnt = 0,rep = -1;
		assert(end == ptr);
		for (; cnt < 3 && end < ptr + l && end < ptr + 128; ++end)
			if (*end != rep) {
				cnt = 1;
				rep = *end;
			} else 
				++cnt;
		if (cnt >= 3) end -= cnt;
		if (end != ptr) {
			data->p[data->l] = (end - ptr - 1) | 0x80;
			memcpy(data->p + data->l + 1,ptr,end - ptr);
			data->l += end - ptr + 1;
			l -= end - ptr;
			ptr += end - ptr;
		}

		rep = *ptr;
		assert(end == ptr);
		for (; *end == rep && end < ptr + l && end < ptr + 128; ++end);
		if (end >= ptr + 3) {
			data->p[data->l] = end - ptr - 1;
			data->p[data->l + 1] = rep;
			data->l += 2;
			l -= end - ptr;
			ptr += end - ptr;
		} else
			end = ptr;
	}
}

int gale_unpack_rle(struct gale_data *data,void *p,size_t l) {
	byte *ptr = p;
	while (l) {
		byte ctl = data->p[0],cnt = (ctl & 0x7F) + 1;
		++data->p;
		--data->l;
		if (cnt > l) return 0;
		if (ctl & 0x80) {
			if (data->l < cnt) return 0;
			memcpy(ptr,data->p,cnt);
			data->p += cnt;
			data->l -= cnt;
		} else {
			if (data->l < 1) return 0;
			memset(ptr,data->p[0],cnt);
			++data->p;
			--data->l;
		}
		ptr += cnt;
		l -= cnt;
	}

	return 1;
}
