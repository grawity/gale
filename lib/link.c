#include "gale/misc.h"
#include "gale/core.h"
#include "buffer.h"

#include <assert.h>
#include <unistd.h>
#include <errno.h>

#define opcode_puff 0
#define opcode_will 1
#define opcode_gimme 2

#define opcode_publish 3
#define opcode_watch 4
#define opcode_forget 5
#define opcode_complete 6
#define opcode_assert 7
#define opcode_retract 8
#define opcode_fetch 9
#define opcode_miss 10
#define opcode_supply 11

#define SIZE_LIMIT 262144
#define PROTOCOL_VERSION 1
#define CID_LENGTH 20

struct link {
	struct gale_message *msg;
	struct link *next;
};

struct pair {
	struct gale_data cid;
	struct gale_text cat;
};

struct gale_link {
	struct oop_source *source;
	int fd;

	/* event handlers */

	void *(*on_error)(struct gale_link *,int,void *);
	void *on_error_data;

	void *(*on_empty)(struct gale_link *,void *);
	void *on_empty_data;

	void *(*on_message)(struct gale_link *,struct gale_message *,void *);
	void *on_message_data;

	void *(*on_will)(struct gale_link *,struct gale_message *,void *);
	void *on_will_data;

	void *(*on_subscribe)(struct gale_link *,struct gale_text,void *);
	void *on_subscribe_data;

	/* input stuff */

	struct input_buffer *input;                     /* version 0 */
	u32 in_opcode,in_length;
	struct gale_message *in_msg,*in_puff,*in_will;
	struct gale_text in_gimme,*in_text;
	int in_version;

	struct gale_text in_publish;                    /* version 1 */
	struct gale_text in_watch,in_forget,in_complete;
	struct pair in_assert,in_retract;
	struct gale_data in_fetch_cid,in_miss_cid,in_supply_cid,*in_cid;
	struct gale_data in_supply_data;

	/* output stuff */

	struct output_buffer *output;                   /* version 0 */
	struct gale_message *out_msg,*out_will;
	struct gale_text out_text,out_gimme;
	struct link *out_queue;
	int queue_num;
	size_t queue_mem;

	struct gale_text out_publish;			/* version 1 */
	struct gale_wt *out_watch,*out_complete,*out_assert;
	struct gale_wt *out_fetch,*out_supply;
	struct gale_data out_cid,out_data;
};

static void * const st_yes = (void *) 0x1;
static void * const st_no = (void *) 0x2;

static size_t message_size(struct gale_message *m) {
	return gale_u32_size() + gale_group_size(m->data) 
	     + m->cat.l * gale_wch_size();
}

static struct gale_message *dequeue(struct gale_link *l) {
	struct gale_message *m = NULL;
	if (NULL != l->out_queue) {
		struct link *link = l->out_queue->next;
		if (l->out_queue == link)
			l->out_queue = NULL;
		else
			l->out_queue->next = link->next;
		--l->queue_num;
		l->queue_mem -= message_size(link->msg);
		m = link->msg;
		gale_free(link);
		gale_dprintf(7,"<- dequeueing message [%p]\n",m);
	}
	return m;
}

/* -- input state machine --------------------------------------------------- */

typedef void istate(struct input_state *inp);
static istate ist_version,ist_idle,ist_message,ist_text,ist_cid,ist_unknown;

static void ifn_version(struct input_state *inp) {
	struct gale_link *l = (struct gale_link *) inp->private;
	u32 version;
	gale_unpack_u32(&inp->data,&version);
	assert(0 == inp->data.l);
/* gale_dprintf(1,"[%d] got: version %d\n",l->fd,version); */
	if (version > PROTOCOL_VERSION) {
		struct gale_text error = gale_text_concat(4,
			G_("remote protocol v"),
			gale_text_from_number(version,10,0),
			G_(" > our protocol v"),
			gale_text_from_number(PROTOCOL_VERSION,10,0));
		gale_alert(GALE_WARNING,gale_text_to_local(error),0);
		l->in_version = PROTOCOL_VERSION;
	} else l->in_version = version;
	l->in_length = 0;
	ist_idle(inp);
}

static void ist_version(struct input_state *inp) {
	inp->next = ifn_version;
	inp->ready = input_always_ready;
	inp->data.p = NULL;
	inp->data.l = gale_u32_size();
}

static void ifn_opcode(struct input_state *inp) {
	struct gale_link *l = (struct gale_link *) inp->private;
	gale_unpack_u32(&inp->data,&l->in_opcode);
	gale_unpack_u32(&inp->data,&l->in_length);
	assert(0 == inp->data.l);
/* gale_dprintf(1,"[%d] got: opcode %d, length %d\n",l->fd,l->in_opcode,l->in_length); */
	if (l->in_length > SIZE_LIMIT) {
		gale_alert(GALE_WARNING,"excessively big message dropped",0);
		ist_unknown(inp);
	} else switch (l->in_opcode) {
	case opcode_puff:
	case opcode_will:
		ist_message(inp);
		break;
	case opcode_gimme:
	case opcode_publish:
	case opcode_watch:
	case opcode_forget:
	case opcode_complete:
		ist_text(inp);
		break;
	case opcode_assert:
	case opcode_retract:
	case opcode_fetch:
	case opcode_miss:
	case opcode_supply:
		ist_cid(inp);
		break;
	default:
		ist_unknown(inp);
	}
}

static void ist_idle(struct input_state *inp) {
	assert(0 == ((struct gale_link *) inp->private)->in_length);
	inp->next = ifn_opcode;
	inp->ready = input_always_ready;
	inp->data.p = NULL;
	inp->data.l = 2 * gale_u32_size();
}

static void ifn_message_body(struct input_state *inp) {
	struct gale_link *l = (struct gale_link *) inp->private;
	u32 zero;
	l->in_length -= inp->data.l;
	assert(0 == l->in_length);
	assert(NULL != l->in_msg);

	if (!gale_unpack_u32(&inp->data,&zero) || 0 != zero 
	||  !gale_unpack_group(&inp->data,&l->in_msg->data))
		gale_alert(GALE_WARNING,"invalid message format ignored",0);
	else switch (l->in_opcode) {
	case opcode_puff:
		assert(NULL == l->in_puff);
		l->in_puff = l->in_msg;
		break;
	case opcode_will:
		l->in_will = l->in_msg;
		break;
	default:
		assert(0);
	}

	l->in_msg = NULL;
	ist_idle(inp);
}

static void ifn_message_category(struct input_state *inp) {
	struct gale_link *l = (struct gale_link *) inp->private;
	assert(inp->data.l <= l->in_length);
	l->in_length -= inp->data.l;

	l->in_msg = new_message();
	if (gale_unpack_text_len(&inp->data,
	                         inp->data.l / gale_wch_size(),
	                         &l->in_msg->cat)) 
	{
		inp->next = ifn_message_body;
		inp->data.l = l->in_length;
		inp->data.p = NULL;
		inp->ready = input_always_ready;
	} else {
		l->in_msg = NULL;
		ist_unknown(inp);
	}
}

static void ifn_category_len(struct input_state *inp) {
	struct gale_link *l = (struct gale_link *) inp->private;
	u32 u;
	assert(inp->data.l <= l->in_length);
	l->in_length -= inp->data.l;
	gale_unpack_u32(&inp->data,&u);
	assert(0 == inp->data.l);
	assert(NULL == l->in_msg);

	if (u > l->in_length) {
		gale_alert(GALE_WARNING,"ignoring malformed message",0);
		ist_unknown(inp);
		return;
	}

	inp->next = ifn_message_category;
	inp->data.l = u;
	inp->data.p = NULL;
}

static int ifn_message_ready(struct input_state *inp) {
	struct gale_link *l = (struct gale_link *) inp->private;
	return NULL == l->in_puff;
}

static void ist_message(struct input_state *inp) {
	struct gale_link *l = (struct gale_link *) inp->private;

	if (gale_u32_size() > l->in_length) {
		gale_alert(GALE_WARNING,"ignoring truncated message",0);
		ist_unknown(inp);
		return;
	}

	inp->next = ifn_category_len;
	inp->data.p = NULL;
	inp->data.l = gale_u32_size();

	if (l->in_opcode == opcode_puff) 
		inp->ready = ifn_message_ready;
	else
		inp->ready = input_always_ready;
}

static void ifn_text(struct input_state *inp) {
	struct gale_link *l = (struct gale_link *) inp->private;
	size_t len = inp->data.l / gale_wch_size();
	assert(l->in_length == inp->data.l);
	l->in_length -= inp->data.l;
	if (gale_unpack_text_len(&inp->data,len,l->in_text))
		ist_idle(inp);
	else
		ist_unknown(inp);
}

static int ifn_text_ready(struct input_state *inp) {
	struct gale_link *l = (struct gale_link *) inp->private;
	switch (l->in_opcode) {
	case opcode_gimme:
	case opcode_publish:
		/* only keep the last */
		return 1;
	default:
		/* block until they've read it */
		return (0 == l->in_text->l);
	}
}

static void ist_text(struct input_state *inp) {
	struct gale_link *l = (struct gale_link *) inp->private;

	inp->next = ifn_text;
	inp->ready = ifn_text_ready;
	inp->data.l = ((struct gale_link *) inp->private)->in_length;
	inp->data.p = NULL;

	switch (l->in_opcode) {
	case opcode_gimme: 	l->in_text = &l->in_gimme; break;
	case opcode_publish:	l->in_text = &l->in_publish; break;
	case opcode_watch:	l->in_text = &l->in_watch; break;
	case opcode_forget:	l->in_text = &l->in_forget; break;
	case opcode_complete:	l->in_text = &l->in_complete; break;
	case opcode_assert:	l->in_text = &l->in_assert.cat; break;
	case opcode_retract:	l->in_text = &l->in_retract.cat; break;
	default: assert(0);
	}
}

static void ifn_supply_data(struct input_state *inp) {
	struct gale_link *l = (struct gale_link *) inp->private;
	assert(inp->data.l == l->in_length);
	l->in_length -= inp->data.l;
	l->in_supply_data = inp->data;
	ist_idle(inp);
}

static void ifn_cid(struct input_state *inp) {
	struct gale_link *l = (struct gale_link *) inp->private;
	assert(CID_LENGTH == inp->data.l);
	l->in_length -= inp->data.l;
	*(l->in_cid) = inp->data;

	/* Figure out what comes next. */
	switch (l->in_opcode) {
	case opcode_assert:
	case opcode_retract:
		ist_text(inp);
		break;
	case opcode_fetch:
	case opcode_miss:
		ist_idle(inp);
		break;
	case opcode_supply:
		inp->next = ifn_supply_data;
		inp->data.l = l->in_length;
		inp->data.p = gale_malloc(l->in_length);
		inp->ready = input_always_ready;
		break;
	default:
		assert(0);
	}
}

static int ifn_cid_ready(struct input_state *inp) {
	struct gale_link *l = (struct gale_link *) inp->private;
	return (0 == l->in_cid->l);
}

static void ist_cid(struct input_state *inp) {
	struct gale_link *l = (struct gale_link *) inp->private;
	if (l->in_length < CID_LENGTH) {
		ist_unknown(inp);
		return;
	}

	inp->next = ifn_cid;
	inp->ready = ifn_cid_ready;
	inp->data.l = CID_LENGTH;
	inp->data.p = gale_malloc(CID_LENGTH);

	switch (l->in_opcode) {
	case opcode_assert:	l->in_cid = &l->in_assert.cid; break;
	case opcode_retract:	l->in_cid = &l->in_retract.cid; break;
	case opcode_fetch:	l->in_cid = &l->in_fetch_cid; break;
	case opcode_miss:	l->in_cid = &l->in_miss_cid; break;
	case opcode_supply:	l->in_cid = &l->in_supply_cid; break;
	default: assert(0);
	}
}

static void ifn_unknown(struct input_state *inp) {
	struct gale_link *l = (struct gale_link *) inp->private;
	assert(inp->data.l <= l->in_length);
	l->in_length -= inp->data.l;
	ist_unknown(inp);
}

static void ist_unknown(struct input_state *inp) {
	struct gale_link *l = (struct gale_link *) inp->private;
	if (0 == l->in_length)
		ist_idle(inp);
	else {
		inp->next = ifn_unknown;
		inp->ready = input_always_ready;
		inp->data.p = NULL;
		inp->data.l = l->in_length;
		if (inp->data.l > SIZE_LIMIT) inp->data.l = SIZE_LIMIT;
	}
}

/* -- output state machine -------------------------------------------------- */

typedef void ostate(struct output_state *out);
static ostate ost_version,ost_idle;

static void ofn_version(struct output_state *out,struct output_context *ctx) {
	struct gale_data buf;
	send_space(ctx,gale_u32_size(),&buf);
	gale_pack_u32(&buf,PROTOCOL_VERSION);
	ost_idle(out);
}

static void ost_version(struct output_state *out) {
	out->ready = output_always_ready;
	out->next = ofn_version;
}

static void ofn_msg_data(struct output_state *out,struct output_context *ctx) {
	struct gale_link *l = (struct gale_link *) out->private;
	struct gale_data data;
	send_space(ctx,gale_u32_size() + gale_group_size(l->out_msg->data),&data);
	gale_pack_u32(&data,0);
	gale_pack_group(&data,l->out_msg->data);
	l->out_msg = NULL;
	ost_idle(out);
}

static void ofn_message(struct output_state *out,struct output_context *ctx) {
	struct gale_link *l = (struct gale_link *) out->private;
	struct gale_data data;
	size_t len = gale_text_len_size(l->out_msg->cat);
	send_space(ctx,gale_u32_size() + len,&data);
	gale_pack_u32(&data,len);
	gale_pack_text_len(&data,l->out_msg->cat);
	out->next = ofn_msg_data;
}

static void ofn_text(struct output_state *out,struct output_context *ctx) {
	struct gale_link *l = (struct gale_link *) out->private;
	struct gale_data data;
	size_t len = gale_text_len_size(l->out_text);
	send_space(ctx,len,&data);
	gale_pack_text_len(&data,l->out_text);
	l->out_text = null_text;
	ost_idle(out);
}

static void ofn_data(struct output_state *out,struct output_context *ctx) {
	struct gale_link *l = (struct gale_link *) out->private;
	send_data(ctx,l->out_data);
	ost_idle(out);
}

static void ofn_cid(struct output_state *out,struct output_context *ctx) {
	struct gale_link *l = (struct gale_link *) out->private;
	send_data(ctx,l->out_cid);
	out->next = ofn_data;
}

static void ofn_idle(struct output_state *out,struct output_context *ctx) {
	struct gale_link *l = (struct gale_link *) out->private;
	struct gale_data data,key;
	void *ptr;

	send_space(ctx,gale_u32_size() * 2,&data);
	out->ready = output_always_ready;
	assert(NULL == l->out_msg);
	assert(0 == l->out_text.l);

	/* out_complete must come after out_assert; otherwise, tune to taste */

	/* version 1 */

	       if (gale_wt_walk(l->out_watch,NULL,&key,&ptr)) {
		out->next = ofn_text;
		l->out_text = gale_text_from_data(key);
		gale_wt_add(l->out_watch,key,NULL);
		if (ptr == st_yes) gale_pack_u32(&data,opcode_watch);
		else { 
			gale_pack_u32(&data,opcode_forget);
			assert(ptr == st_no); 
		}
		gale_pack_u32(&data,l->out_text.l * gale_wch_size());
	} else if (gale_wt_walk(l->out_fetch,NULL,&key,&ptr)) {
		assert(st_yes == ptr);
		out->next = ofn_cid;
		l->out_cid = key;
		l->out_data = null_data;
		gale_pack_u32(&data,opcode_fetch);
		gale_pack_u32(&data,l->out_cid.l + l->out_data.l);
		assert(CID_LENGTH == l->out_cid.l);
	} else if (0 != l->out_publish.l) {
		out->next = ofn_text;
		l->out_text = l->out_publish;
		l->out_publish = null_text;
		gale_pack_u32(&data,opcode_publish);
		gale_pack_u32(&data,l->out_text.l * gale_wch_size());
	} else if (gale_wt_walk(l->out_supply,NULL,&key,&ptr)) {
		out->next = ofn_cid;
		l->out_cid = key;
		assert(ptr != st_yes);
		if (st_no == ptr) {
			l->out_data = null_data;
			gale_pack_u32(&data,opcode_miss);
		} else {
			l->out_data = * (struct gale_data *) ptr;
			gale_pack_u32(&data,opcode_supply);
		}
		gale_pack_u32(&data,l->out_cid.l + l->out_data.l);
		assert(CID_LENGTH == l->out_cid.l);
	} else if (gale_wt_walk(l->out_assert,NULL,&key,&ptr)) {
		out->next = ofn_cid;
		l->out_cid = key;
		l->out_data = null_data;
		if (st_yes == ptr) gale_pack_u32(&data,opcode_assert);
		else {
			gale_pack_u32(&data,opcode_retract);
			assert(st_no == ptr);
		}
		gale_pack_u32(&data,l->out_cid.l + l->out_data.l);
	} else if (gale_wt_walk(l->out_complete,NULL,&key,&ptr)) {
		assert(ptr == st_yes);
		out->next = ofn_text;
		l->out_text = gale_text_from_data(key);
		gale_wt_add(l->out_complete,key,NULL);
		gale_pack_u32(&data,opcode_complete);
		gale_pack_u32(&data,l->out_text.l * gale_wch_size());
	} else 

	/* version 0 */

	       if (0 != l->out_gimme.l) {
		out->next = ofn_text;
		l->out_text = l->out_gimme;
		l->out_gimme = null_text;
		gale_pack_u32(&data,opcode_gimme);
		gale_pack_u32(&data,l->out_text.l * gale_wch_size());
	} else if (NULL != l->out_will) {
		out->next = ofn_message;
		l->out_msg = l->out_will;
		l->out_will = NULL;
		gale_pack_u32(&data,opcode_will);
		gale_pack_u32(&data,gale_u32_size() 
			+ l->out_msg->cat.l * gale_wch_size() 
			+ gale_u32_size() + gale_group_size(l->out_msg->data));
	} else if (NULL != l->out_queue) {
		out->next = ofn_message;
		l->out_msg = dequeue(l);
		gale_pack_u32(&data,opcode_puff);
		gale_pack_u32(&data,gale_u32_size() 
			+ l->out_msg->cat.l * gale_wch_size() 
			+ gale_u32_size() + gale_group_size(l->out_msg->data));
	} else assert(0);
}

static int ofn_idle_ready(struct output_state *out) {
	struct gale_link *l = (struct gale_link *) out->private;
	return l->out_will || l->out_gimme.l || l->out_queue || l->out_publish.l
	    || gale_wt_walk(l->out_watch,NULL,NULL,NULL)
	    || gale_wt_walk(l->out_complete,NULL,NULL,NULL)
	    || gale_wt_walk(l->out_assert,NULL,NULL,NULL)
	    || gale_wt_walk(l->out_fetch,NULL,NULL,NULL)
	    || gale_wt_walk(l->out_supply,NULL,NULL,NULL);
}

static void ost_idle(struct output_state *out) {
	out->ready = ofn_idle_ready;
	out->next = ofn_idle;
}

/* -- API: version 0 -------------------------------------------------------- */

static int get_text(struct gale_link *l,
                    struct gale_text *from,struct gale_text *to) {
	if (0 == from->l) return 0;
	*to = *from;
	*from = null_text;
	if (l->input) input_buffer_more(l->input);
	return 1;
}

static int get_data(struct gale_link *l,
                    struct gale_data *from,struct gale_data *to) {
	if (0 == from->l) return 0;
	*to = *from;
	*from = null_data;
	if (l->input) input_buffer_more(l->input);
	return 1;
}

struct gale_link *new_link(struct oop_source *source) {
	struct gale_link *l;
	gale_create(l);

	l->source = source;
	l->fd = -1;

	l->on_error = NULL;
	l->on_empty = NULL;
	l->on_message = NULL;
	l->on_will = NULL;
	l->on_subscribe = NULL;

	l->input = NULL;
	l->in_msg = l->in_puff = l->in_will = NULL;
	l->in_gimme = null_text;
	l->in_version = -1;

	l->in_publish = null_text;
	l->in_watch = l->in_forget = l->in_complete = null_text;
	l->in_assert.cat = l->in_retract.cat = null_text;
	l->in_fetch_cid = l->in_miss_cid = l->in_supply_cid = null_data;
	l->in_supply_data = null_data;

	l->output = NULL;
	l->out_text = null_text;
	l->out_gimme = null_text;
	l->out_msg = l->out_will = NULL;
	l->out_queue = NULL;
	l->queue_num = 0;
	l->queue_mem = 0;

	l->out_publish = null_text;
	l->out_watch = gale_make_wt(0);
	l->out_complete = gale_make_wt(0);
	l->out_assert = gale_make_wt(0);
	l->out_fetch = gale_make_wt(0);
	l->out_supply = gale_make_wt(0);

	return l;
}

static oop_call_fd on_read,on_write;
static oop_call_time on_process;

static void *on_process(oop_source *source,struct timeval tv,void *user) {
	struct gale_link *l = (struct gale_link *) user;
	void *ret = OOP_CONTINUE;
	assert(source == l->source);

	while (OOP_CONTINUE == ret 
	    && NULL != l->in_puff 
            && NULL != l->on_message) {
		struct gale_message *puff = l->in_puff;
		l->in_puff = NULL;
		ret = l->on_message(l,puff,l->on_message_data);
		if (NULL != l->input) input_buffer_more(l->input);
	}

	if (OOP_CONTINUE == ret && NULL != l->in_will && NULL != l->on_will) {
		struct gale_message *will = l->in_will;
		l->in_will = NULL;
		ret = l->on_will(l,will,l->on_will_data);
	}

	if (OOP_CONTINUE == ret 
	&&  0 != l->in_gimme.l && NULL != l->on_subscribe) {
		struct gale_text sub = l->in_gimme;
		l->in_gimme = null_text;
		ret = l->on_subscribe(l,sub,l->on_subscribe_data);
	}

	if (OOP_CONTINUE == ret 
	&&  l->in_version > -1
	&&  0 == link_queue_num(l) && NULL != l->on_empty) {
		ret = l->on_empty(l,l->on_empty_data);
		l->source->on_time(l->source,OOP_TIME_NOW,on_process,l);
	} else if (OOP_CONTINUE != ret)
		l->source->on_time(l->source,OOP_TIME_NOW,on_process,l);

	/* We may have freed up room for more reading. */
	l->source->cancel_fd(l->source,l->fd,OOP_READ,on_read,l);
	l->source->on_fd(l->source,l->fd,OOP_READ,on_read,l);
	return ret;
}

static void *on_read(oop_source *source,int fd,oop_event event,void *user) {
	struct gale_link *l = (struct gale_link *) user;
	void *ret = OOP_CONTINUE;
	assert(source == l->source);
	assert(fd == l->fd);

	if (NULL == l->input) {
		struct input_state initial;
		initial.private = l;
		ist_version(&initial);
		l->input = create_input_buffer(initial);
	}

	if (!input_buffer_ready(l->input))
		l->source->cancel_fd(l->source,l->fd,OOP_READ,on_read,l);
	else if (input_buffer_read(l->input,l->fd)) {
		int err = errno;
/* gale_dprintf(1,"[%d] closing\n",l->fd); */
		link_set_fd(l,-1);
		if (NULL != l->on_error) 
			ret = l->on_error(l,err,l->on_error_data);
	} else
		l->source->on_time(l->source,OOP_TIME_NOW,on_process,l);

	return ret;
}

static void *on_write(oop_source *source,int fd,oop_event event,void *user) {
	struct gale_link *l = (struct gale_link *) user;
	void *ret = OOP_CONTINUE;
	assert(source == l->source);
	assert(fd == l->fd);

	if (NULL == l->output) {
		struct output_state initial;
		initial.private = l;
		ost_version(&initial);
		l->output = create_output_buffer(initial);
	}

	if (!output_buffer_ready(l->output))
		l->source->cancel_fd(l->source,l->fd,OOP_WRITE,on_write,l);
	else if (output_buffer_write(l->output,l->fd)) {
		int err = errno;
/* gale_dprintf(1,"[%d] closing\n",l->fd); */
		link_set_fd(l,-1);
		if (NULL != l->on_error)
			ret = l->on_error(l,err,l->on_error_data);
	}

	if (OOP_CONTINUE == ret
	&&  l->in_version > -1
	&&  0 == link_queue_num(l) && NULL != l->on_empty)
		ret = l->on_empty(l,l->on_empty_data);

	return ret;
}

static void deactivate(struct gale_link *l) {
	if (-1 != l->fd) {
		l->source->cancel_fd(l->source,l->fd,OOP_READ,on_read,l);
		l->source->cancel_fd(l->source,l->fd,OOP_WRITE,on_write,l);
		l->source->cancel_time(l->source,OOP_TIME_NOW,on_process,l);
	}
}

static void activate(struct gale_link *l) {
	deactivate(l);
	if (-1 != l->fd) {
		l->source->on_fd(l->source,l->fd,OOP_READ,on_read,l);
		l->source->on_fd(l->source,l->fd,OOP_WRITE,on_write,l);
		l->source->on_time(l->source,OOP_TIME_NOW,on_process,l);
	}
}

void link_set_fd(struct gale_link *l,int fd) {
	if (-1 != l->fd) {
		/* reset temporary fields and protocol state machine */
		if (l->in_msg) l->in_msg = NULL;
		if (l->input) l->input = NULL;

		if (l->out_msg) l->out_msg = NULL;
		if (l->out_text.l) l->out_text = null_text;
		if (l->output) l->output = NULL;

		close(l->fd);
		deactivate(l);
	}

	l->fd = fd;
	activate(l);
}

void link_subscribe(struct gale_link *l,struct gale_text spec) {
	l->out_gimme = spec;
	activate(l);
}

void link_put(struct gale_link *l,struct gale_message *m) {
	struct link *link;

	gale_create(link);
	link->msg = m;
	if (NULL == l->out_queue)
		link->next = link;
	else {
		link->next = l->out_queue->next;
		l->out_queue->next = link;
	}
	l->out_queue = link;

	++l->queue_num;
	l->queue_mem += message_size(m);
	gale_dprintf(7,"-> enqueueing message [%p]\n",m);
	activate(l);
}

void link_will(struct gale_link *l,struct gale_message *m) {
	l->out_will = m;
	activate(l);
}

int link_queue_num(struct gale_link *l) {
	return l->queue_num;
}

size_t link_queue_mem(struct gale_link *l) {
	return l->queue_mem;
}

void link_queue_drop(struct gale_link *l) {
	if (NULL != l->out_queue) dequeue(l);
}

void link_on_error(struct gale_link *l,
     void *(*call)(struct gale_link *,int,void *),
     void *data) {
	l->on_error = call;
	l->on_error_data = data;
	activate(l);
}

void link_on_empty(struct gale_link *l,
     void *(*call)(struct gale_link *,void *),
     void *data) {
	l->on_empty = call;
	l->on_empty_data = data;
	activate(l);
}

void link_on_message(struct gale_link *l,
     void *(*call)(struct gale_link *,struct gale_message *,void *),
     void *data) {
	l->on_message = call;
	l->on_message_data = data;
	activate(l);
}

void link_on_will(struct gale_link *l,
     void *(*call)(struct gale_link *,struct gale_message *,void *),
     void *data) {
	l->on_will = call;
	l->on_will_data = data;
	activate(l);
}

void link_on_subscribe(struct gale_link *l,
     void *(*call)(struct gale_link *,struct gale_text,void *),
     void *data) {
	l->on_subscribe = call;
	l->on_subscribe_data = data;
	activate(l);
}

/* -- API: version 1 -------------------------------------------------------- */

static struct gale_data combine(struct gale_text cat,struct gale_data cid) {
	struct gale_data data;
	data.p = gale_malloc(gale_text_size(cat) + cid.l);
	data.l = 0;
	gale_pack_copy(&data,cid.p,cid.l);
	gale_pack_text(&data,cat);
	assert(data.l == gale_text_size(cat) + cid.l);
	return data;
}

void ltx_publish(struct gale_link *l,struct gale_text spec) {
	assert(l->in_version > 0);
	l->out_publish = spec;
}

void ltx_watch(struct gale_link *l,struct gale_text category) {
	assert(l->in_version > 0);
	gale_wt_add(l->out_watch,gale_text_as_data(category),st_yes);
}

void ltx_forget(struct gale_link *l,struct gale_text category) {
	assert(l->in_version > 0);
	gale_wt_add(l->out_watch,gale_text_as_data(category),st_no);
}

void ltx_complete(struct gale_link *l,struct gale_text category) {
	assert(l->in_version > 0);
	gale_wt_add(l->out_complete,gale_text_as_data(category),st_yes);
}

void ltx_assert(struct gale_link *l,struct gale_text cat,struct gale_data cid) {
	assert(l->in_version > 0);
	gale_wt_add(l->out_assert,combine(cat,cid),st_yes);
}

void ltx_retract(struct gale_link *l,struct gale_text cat,struct gale_data id) {
	assert(l->in_version > 0);
	gale_wt_add(l->out_assert,combine(cat,id),st_no);
}

void ltx_fetch(struct gale_link *l,struct gale_data cid) {
	assert(l->in_version > 0);
	gale_wt_add(l->out_fetch,cid,st_yes);
}

void ltx_miss(struct gale_link *l,struct gale_data cid) {
	assert(l->in_version > 0);
	gale_wt_add(l->out_supply,cid,st_no);
}

void ltx_supply(struct gale_link *l,struct gale_data id,struct gale_data data) {
	struct gale_data *pdata;
	assert(l->in_version > 0);
	*(gale_create(pdata)) = data;
	gale_wt_add(l->out_supply,id,pdata);
}

int lrx_publish(struct gale_link *l,struct gale_text *spec) {
	return get_text(l,&l->in_publish,spec);
}

int lrx_watch(struct gale_link *l,struct gale_text *category) {
	return get_text(l,&l->in_watch,category);
}

int lrx_forget(struct gale_link *l,struct gale_text *category) {
	return get_text(l,&l->in_forget,category);
}

int lrx_complete(struct gale_link *l,struct gale_text *category) {
	return get_text(l,&l->in_complete,category);
}

int lrx_assert(struct gale_link *l,
               struct gale_text *cat,struct gale_data *cid) 
{
	*cid = l->in_assert.cid; l->in_assert.cid = null_data;
	return get_text(l,&l->in_assert.cat,cat);
}

int lrx_retract(struct gale_link *l,
                struct gale_text *cat,struct gale_data *cid) 
{
	*cid = l->in_retract.cid; l->in_retract.cid = null_data;
	return get_text(l,&l->in_retract.cat,cat);
}

int lrx_fetch(struct gale_link *l,struct gale_data *cid) {
	return get_data(l,&l->in_fetch_cid,cid);
}

int lrx_miss(struct gale_link *l,struct gale_data *cid) {
	return get_data(l,&l->in_miss_cid,cid);
}

int lrx_supply(struct gale_link *l,
               struct gale_data *cid,struct gale_data *data) 
{
	*cid = l->in_supply_cid; l->in_supply_cid = null_data;
	return get_data(l,&l->in_supply_data,data);
}
