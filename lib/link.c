#include "gale/misc.h"
#include "gale/core.h"
#include "link_old.h"
#include "buffer.h"

#include <assert.h>

#define opcode_puff 0
#define opcode_will 1
#define opcode_gimme 2

#define SIZE_LIMIT 262144

struct link {
	struct gale_message *msg;
	struct link *next;
};

struct gale_link {
	struct gale_link_old *old;

	/* input stuff */
	struct input_buffer *input;
	u32 in_opcode,in_length;
	struct gale_message *in_msg,*in_puff,*in_will;
	struct gale_text in_gimme;

	/* output stuff */
	struct output_buffer *output;
	struct gale_message *out_msg,*out_will;
	struct gale_text out_text,out_gimme;
	struct link *out_queue;
	int queue_num;
	size_t queue_mem;
};

static size_t message_size(struct gale_message *m) {
	return m->data.l + m->cat.l * sizeof(wch);
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
static istate ist_version,ist_idle,ist_message,ist_subscribe,ist_unknown;

static void ifn_version(struct input_state *inp) {
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
	if (l->in_length > SIZE_LIMIT) {
		gale_alert(GALE_WARNING,"excessively big message dropped",0);
		ist_unknown(inp);
	} else switch (l->in_opcode) {
	case opcode_puff:
	case opcode_will:
		ist_message(inp);
		break;
	case opcode_gimme:
		ist_subscribe(inp);
		break;
	default:
		ist_unknown(inp);
	}
}

static void ist_idle(struct input_state *inp) {
	inp->next = ifn_opcode;
	inp->ready = input_always_ready;
	inp->data.p = NULL;
	inp->data.l = 2 * gale_u32_size();
}

static void ifn_message_body(struct input_state *inp) {
	struct gale_link *l = (struct gale_link *) inp->private;
	l->in_length -= inp->data.l;
	assert(0 == l->in_length);
	assert(NULL != l->in_msg);
	switch (l->in_opcode) {
	case opcode_puff:
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
	inp->next = ifn_message_body;
	inp->data.l = l->in_length;
	inp->data.p = gale_malloc(inp->data.l);
	l->in_msg->data = inp->data;
	inp->ready = input_always_ready;
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
	}

	inp->next = ifn_message_category;
	inp->data.l = u;
	inp->data.p = gale_malloc(inp->data.l);
	l->in_msg = new_message();
	l->in_msg->cat.p = (wch*) inp->data.p;
	l->in_msg->cat.l = inp->data.l / sizeof(wch);
}

static int ifn_message_ready(struct input_state *inp) {
	struct gale_link *l = (struct gale_link *) inp->private;
	switch (l->in_opcode) {
	case opcode_puff:
		return (NULL == l->in_puff);
	case opcode_will:
		return (NULL == l->in_will);
	default:
		assert(0);
	}
}

static void ist_message(struct input_state *inp) {
	struct gale_link *l = (struct gale_link *) inp->private;

	if (gale_u32_size() > l->in_length) {
		gale_alert(GALE_WARNING,"ignoring truncated message",0);
		ist_unknown(inp);
	}

	inp->next = ifn_category_len;
	inp->ready = ifn_message_ready;
	inp->data.p = NULL;
	inp->data.l = gale_u32_size();
}

static void ifn_subscribe_category(struct input_state *inp) {
	struct gale_link *l = (struct gale_link *) inp->private;
	assert(opcode_gimme == l->in_opcode);
	assert(l->in_length == inp->data.l);
	assert(NULL == l->in_gimme.p);
	l->in_gimme.p = (wch*) inp->data.p;
	l->in_gimme.l = inp->data.l / sizeof(wch);
	ist_idle(inp);
}

static int ifn_subscribe_ready(struct input_state *inp) {
	return NULL == ((struct gale_link *) inp->private)->in_gimme.p;
}

static void ist_subscribe(struct input_state *inp) {
	inp->next = ifn_subscribe_category;
	inp->ready = ifn_subscribe_ready;
	inp->data.l = ((struct gale_link *) inp->private)->in_length;
	inp->data.p = gale_malloc(inp->data.l);
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
	gale_pack_u32(&buf,0);
	ost_idle(out);
}

static void ost_version(struct output_state *out) {
	out->ready = output_always_ready;
	out->next = ofn_version;
}

static void rel_message(struct gale_data data,void *private) {
	(void) data;
	release_message((struct gale_message *) private);
}

static void ofn_msg_data(struct output_state *out,struct output_context *ctx) {
	struct gale_link *l = (struct gale_link *) out->private;
	send_buffer(ctx,l->out_msg->data,rel_message,l->out_msg);
	l->out_msg = NULL;
	ost_idle(out);
}

static void ofn_msg_cat(struct output_state *out,struct output_context *ctx) {
	struct gale_link *l = (struct gale_link *) out->private;
	struct gale_data data;
	data.p = (byte *) l->out_msg->cat.p;
	data.l = l->out_msg->cat.l * sizeof(wch);
	send_buffer(ctx,data,rel_message,l->out_msg);
	addref_message(l->out_msg);
	out->next = ofn_msg_data;
}

static void ofn_message(struct output_state *out,struct output_context *ctx) {
	struct gale_link *l = (struct gale_link *) out->private;
	struct gale_data data;
	send_space(ctx,gale_u32_size(),&data);
	gale_pack_u32(&data,l->out_msg->cat.l * sizeof(wch));
	out->next = ofn_msg_cat;
}

static void ofn_text(struct output_state *out,struct output_context *ctx) {
	struct gale_link *l = (struct gale_link *) out->private;
	struct gale_data data;
	data.p = (byte *) l->out_text.p;
	data.l = l->out_text.l * sizeof(wch);
	l->out_text.p = NULL;
	send_buffer(ctx,data,output_release_free,NULL);
	ost_idle(out);
}

static void ofn_idle(struct output_state *out,struct output_context *ctx) {
	struct gale_link *l = (struct gale_link *) out->private;
	struct gale_data data;

	send_space(ctx,gale_u32_size() * 2,&data);
	out->ready = output_always_ready;
	assert(NULL == l->out_msg);
	assert(NULL == l->out_text.p);

	if (NULL != l->out_gimme.p) {
		out->next = ofn_text;
		l->out_text = l->out_gimme;
		l->out_gimme.p = NULL;
		gale_pack_u32(&data,opcode_gimme);
		gale_pack_u32(&data,l->out_text.l * sizeof(wch));
	} else if (NULL != l->out_will) {
		out->next = ofn_message;
		l->out_msg = l->out_will;
		l->out_will = NULL;
		gale_pack_u32(&data,opcode_will);
		gale_pack_u32(&data,gale_u32_size() 
			+ l->out_msg->cat.l * sizeof(wch) 
			+ l->out_msg->data.l);
	} else if (NULL != l->out_queue) {
		out->next = ofn_message;
		l->out_msg = dequeue(l);
		gale_pack_u32(&data,opcode_puff);
		gale_pack_u32(&data,gale_u32_size() 
			+ l->out_msg->cat.l * sizeof(wch) 
			+ l->out_msg->data.l);
	} else assert(0);
}

static int ofn_idle_ready(struct output_state *out) {
	struct gale_link *l = (struct gale_link *) out->private;
	return l->out_will || l->out_gimme.p || l->out_queue;
}

static void ost_idle(struct output_state *out) {
	out->ready = ofn_idle_ready;
	out->next = ofn_idle;
}

/* -- API ------------------------------------------------------------------- */

struct gale_link *new_old_link(void) {
	struct gale_link *l = gale_malloc(sizeof(struct gale_link_old *));
	l->old = new_link_old();
	return l;
}

struct gale_link *new_link(void) {
	struct gale_link *l = gale_malloc(sizeof(*l));
	l->old = NULL;

	l->input = NULL;
	l->in_msg = l->in_puff = l->in_will = NULL;
	l->in_gimme.p = NULL;

	l->output = NULL;
	l->out_text.p = NULL;
	l->out_gimme.p = NULL;
	l->out_msg = l->out_will = NULL;
	l->out_queue = NULL;
	l->queue_num = 0;
	l->queue_mem = 0;

	return l;
}

void free_link(struct gale_link *l) {
	struct gale_message *m;

	if (l->old) {
		free_link_old(l->old);
		gale_free(l);
		return;
	}

	reset_link(l);

	if (l->in_puff) release_message(l->in_puff);
	if (l->in_will) release_message(l->in_will);
	if (l->in_gimme.p) gale_free(l->in_gimme.p);

	if (l->out_will) release_message(l->out_will);
	if (l->out_gimme.p) gale_free(l->out_gimme.p);

	while ((m = dequeue(l))) release_message(m);
	assert(l->queue_num == 0 && l->queue_mem == 0);
	gale_free(l);
}

void reset_link(struct gale_link *l) {
	if (l->old) {
		reset_link_old(l->old);
		return;
	}

	/* reset fields */
	if (l->in_msg) release_message(l->in_msg); l->in_msg = NULL;
	if (l->input) release_input_buffer(l->input); l->input = NULL;

	if (l->out_msg) release_message(l->out_msg); l->out_msg = NULL;
	if (l->out_text.p) gale_free(l->out_text.p); l->out_text.p = NULL;
	if (l->output) release_output_buffer(l->output); l->output = NULL;
}

int link_receive_q(struct gale_link *l) {
	if (l->old) return link_receive_q_old(l->old);

	if (NULL == l->input) {
		struct input_state initial;
		initial.private = l;
		ist_version(&initial);
		l->input = create_input_buffer(initial);
	}

	return input_buffer_ready(l->input);
}

int link_receive(struct gale_link *l,int fd) {
	if (l->old) return link_receive_old(l->old,fd);
	assert(NULL != l->input); /* call link_receive_q first! */
	return input_buffer_read(l->input,fd);
}

int link_transmit_q(struct gale_link *l) {
	if (l->old) return link_transmit_q_old(l->old);

	if (NULL == l->output) {
		struct output_state initial;
		initial.private = l;
		ost_version(&initial);
		l->output = create_output_buffer(initial);
	}

	return output_buffer_ready(l->output);
}

int link_transmit(struct gale_link *l,int fd) {
	if (l->old) return link_transmit_old(l->old,fd);
	return output_buffer_write(l->output,fd);
}

void link_subscribe(struct gale_link *l,struct gale_text spec) {
	if (l->old) {
		char *ch = gale_text_to_latin1(spec);
		link_subscribe_old(l->old,ch);
		gale_free(ch);
		return;
	}

	if (l->out_gimme.p) gale_free(l->out_gimme.p);
	l->out_gimme = gale_text_dup(spec);
}

void link_put(struct gale_link *l,struct gale_message *m) {
	struct link *link;

	if (l->old) {
		link_put_old(l->old,m);
		return;
	}

	link = gale_malloc(sizeof(*link));
	if (NULL == l->out_queue)
		link->next = link;
	else {
		link->next = l->out_queue->next;
		l->out_queue->next = link;
	}
	l->out_queue = link;

	++l->queue_num;
	l->queue_mem += message_size(m);
	addref_message(link->msg = m);
	gale_dprintf(7,"-> enqueueing message [%p]\n",m);
}

void link_will(struct gale_link *l,struct gale_message *m) {
	if (l->old) {
		link_put_old(l->old,m);
		return;
	}

	if (l->out_will) release_message(l->out_will);
	l->out_will = m;
	if (l->out_will) addref_message(l->out_will);
}

int link_queue_num(struct gale_link *l) {
	if (l->old) return link_queue_old(l->old);
	return l->queue_num;
}

size_t link_queue_mem(struct gale_link *l) {
	if (l->old) return 0;
	return l->queue_mem;
}

void link_queue_drop(struct gale_link *l) {
	if (l->old) return;
	if (NULL != l->out_queue) release_message(dequeue(l));
}

struct gale_message *link_get(struct gale_link *l) {
	struct gale_message *puff;
	if (l->old) return link_get_old(l->old);
	puff = l->in_puff;
	l->in_puff = NULL;
	return puff;
}

struct gale_message *link_willed(struct gale_link *l) {
	struct gale_message *will;
	if (l->old) return link_willed_old(l->old);
	will = l->in_will;
	l->in_will = NULL;
	return will;
}

struct gale_text link_subscribed(struct gale_link *l) {
	struct gale_text text = { NULL,0 };

	if (l->old) {
		char *ch = link_subscribed_old(l->old);
		text = gale_text_from_latin1(ch,-1);
		gale_free(ch);
		return text;
	}

	text = l->in_gimme;
	l->in_gimme.p = NULL;
	l->in_gimme.l = 0;
	return text;
}
