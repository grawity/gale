#include "gale/misc.h"
#include "gale/core.h"

#include "msg_old.h"

#include <assert.h>

#define fragment_text 0
#define fragment_data 1
#define fragment_time 2
#define fragment_number 3
#define max_fragment 4

#define message_type 0

static int use_old_format(void) {
	return time(NULL) < 899449200;
}

struct gale_fragment **unpack_message(struct gale_data msgdata) {
	struct gale_fragment **frags = NULL;
	struct gale_data data;
	u32 type,len,num;
	int i,count = 0;

	data = msgdata;
	if (!gale_unpack_u32(&data,&type) || (type & 0xff000000) != 0)
		return unpack_old_message(msgdata);

	if (type != message_type) {
		gale_alert(GALE_WARNING,"unknown message type",0);
		gale_create(frags);
		frags[0] = NULL;
		return frags;
	}

	msgdata = data;
	while (gale_unpack_u32(&data,&type) && gale_unpack_skip(&data)) ++count;

	gale_create_array(frags,1 + count);
	data = msgdata;
	for (i = 0; i < count; ++i) {
		struct gale_data fdata;
		size_t size;
		gale_unpack_u32(&data,&type);

		if (type >= max_fragment) continue;

		fdata = data;
		gale_unpack_skip(&data);

		gale_unpack_u32(&fdata,&len);
		assert(len <= fdata.l);
		fdata.l = len;

		gale_create(frags[i]);
		if (!gale_unpack_text(&fdata,&frags[i]->name)) 
			goto warning;

		switch (type) {
		case fragment_text:
			frags[i]->type = frag_text;
			size = fdata.l / gale_wch_size();
			if (!gale_unpack_text_len(&fdata,size,
			                          &frags[i]->value.text))
				goto warning;
			break;
		case fragment_data:
			frags[i]->type = frag_data;
			frags[i]->value.data = fdata;
			fdata.p += fdata.l;
			fdata.l = 0;
			break;
		case fragment_time:
			frags[i]->type = frag_time;
			if (!gale_unpack_time(&fdata,&frags[i]->value.time))
				goto warning;
			break;
		case fragment_number:
			frags[i]->type = frag_number;
			if (!gale_unpack_u32(&fdata,&num))
				goto warning;
			frags[i]->value.number = (s32) num;
			break;
		default:
			assert(0);
		}

		if (0 != fdata.l) {
warning:
			gale_alert(GALE_WARNING,"invalid message fragment",0);
			frags[i]->name = G_("error");
			frags[i]->type = frag_data;
			frags[i]->value.data.p = data.p - len;
			frags[i]->value.data.l = len;
		}
	}

	frags[i] = NULL;
	return frags;
}

struct gale_data pack_message(struct gale_fragment **frags) {
	struct gale_data msgdata;
	struct gale_fragment **ptr;
	size_t size = gale_u32_size(); /* message type */

	if (use_old_format()) return pack_old_message(frags);

	for (ptr = frags; *ptr; ++ptr) {
		struct gale_fragment * const frag = *ptr;
		size += gale_u32_size() * 2; /* type, length */
		size += gale_text_size(frag->name);
		switch (frag->type) {
		case frag_text:
			size += gale_text_len_size(frag->value.text);
			break;
		case frag_data:
			size += frag->value.data.l;
			break;
		case frag_time:
			size += gale_time_size();
			break;
		case frag_number:
			size += gale_u32_size();
			break;
		default:
			assert(0);
		}
	}

	msgdata.p = NULL;
	msgdata.p = gale_malloc_atomic(size);
	msgdata.l = 0;

	gale_pack_u32(&msgdata,message_type);

	for (ptr = frags; *ptr; ++ptr) {
		struct gale_fragment * const frag = *ptr;
		size_t len = gale_text_size(frag->name);
		switch (frag->type) {
		case frag_text:
			gale_pack_u32(&msgdata,fragment_text);
			gale_pack_u32(&msgdata,len + 
			              gale_text_len_size(frag->value.text));
			gale_pack_text(&msgdata,frag->name);
			gale_pack_text_len(&msgdata,frag->value.text);
			break;
		case frag_data:
			gale_pack_u32(&msgdata,fragment_data);
			gale_pack_u32(&msgdata,len + frag->value.data.l);
			gale_pack_text(&msgdata,frag->name);
			gale_pack_copy(&msgdata,
			               frag->value.data.p,frag->value.data.l);
			break;
		case frag_time:
			gale_pack_u32(&msgdata,fragment_time);
			gale_pack_u32(&msgdata,len + gale_time_size());
			gale_pack_text(&msgdata,frag->name);
			gale_pack_time(&msgdata,frag->value.time);
			break;
		case frag_number:
			gale_pack_u32(&msgdata,fragment_number);
			gale_pack_u32(&msgdata,len + gale_u32_size());
			gale_pack_text(&msgdata,frag->name);
			gale_pack_u32(&msgdata,(u32) frag->value.number);
			break;
		default:
			assert(0);
		}
	}

	assert(msgdata.l == size);
	return msgdata;
}
