#include "buffer.h"
#include "gale/misc.h"

#include <errno.h>
#include <sys/uio.h>
#include <assert.h>
#include <unistd.h>

struct segment {
	struct gale_data data;
	void *private;
	void (*release)(struct gale_data,void *);
};

struct output_buffer {
	struct output_state state;
	byte buffer[1024];
	size_t bhead,btail;
	#define NUM_SEG 16
	struct segment seg[NUM_SEG];
	int shead,stail;
	size_t remnant;
};

static void rel_queue(struct gale_data data,void *private) {
	struct output_buffer *buf = (struct output_buffer *) private;
	assert(data.p == buf->buffer + buf->btail);
	buf->btail += data.l;
	assert(buf->btail <= sizeof(buf->buffer));
	if (buf->btail == sizeof(buf->buffer)) buf->btail = 0;
}

struct output_buffer *create_output_buffer(struct output_state initial) {
	struct output_buffer *buf;
	gale_create(buf);
	buf->state = initial;
	buf->bhead = 0;
	buf->btail = sizeof(buf->buffer) - 1;
	buf->shead = 0;
	buf->stail = NUM_SEG - 1;
	buf->remnant = 0;
	return buf;
}

int output_buffer_ready(struct output_buffer *buf) {
	int sptr = buf->stail;
	if (NUM_SEG == ++sptr) sptr = 0;
	return (sptr != buf->shead || buf->state.ready(&buf->state));
}

int output_buffer_write(struct output_buffer *buf,int fd) {
	struct iovec vec[NUM_SEG];
	size_t count = 0;
	int sptr,w;

	while (buf->shead != buf->stail && buf->bhead != buf->btail
	   &&  buf->state.ready(&buf->state)) {
		int prev = buf->shead;
		buf->state.next(&buf->state,(struct output_context *) buf);
		if (prev == buf->shead) break;
	}

	sptr = buf->stail;
	if (NUM_SEG == ++sptr) sptr = 0;
	if (sptr != buf->shead) {
		vec[count].iov_base = buf->seg[sptr].data.p + buf->remnant;
		vec[count].iov_len = buf->seg[sptr].data.l - buf->remnant;
		++count;
		if (NUM_SEG == ++sptr) sptr = 0;
		while (sptr != buf->shead) {
			vec[count].iov_base = buf->seg[sptr].data.p;
			vec[count].iov_len = buf->seg[sptr].data.l;
			++count;
			if (NUM_SEG == ++sptr) sptr = 0;
		}
	}

	if (0 == count) return 0;
	w = writev(fd,vec,count);
	if (w <= 0) return -(errno != EINTR);

	w += buf->remnant;
	sptr = buf->stail;
	if (NUM_SEG == ++sptr) sptr = 0;
	while (sptr != buf->shead && buf->seg[sptr].data.l <= (size_t) w) {
		struct segment *seg = &buf->seg[sptr];
		if (seg->release) seg->release(seg->data,seg->private);
		w -= seg->data.l;
		buf->stail = sptr;
		if (NUM_SEG == ++sptr) sptr = 0;
	}

	buf->remnant = w;
	return 0;
}

void send_data(struct output_context *ctx,struct gale_data data) {
	struct output_buffer *buf = (struct output_buffer *) ctx;
	size_t ptr = 0;
	while (((buf->shead + 1) % NUM_SEG) != buf->stail 
           &&  ptr < data.l 
           &&  buf->bhead != buf->btail) 
	{
		struct gale_data copy;
		if (buf->bhead > buf->btail)
			copy.l = sizeof(buf->buffer) - buf->bhead;
		else
			copy.l = buf->btail - buf->bhead;
		if (copy.l > data.l - ptr) copy.l = data.l - ptr;
		copy.p = buf->buffer + buf->bhead;
		memcpy(copy.p,data.p + ptr,copy.l);
		send_buffer(ctx,copy,rel_queue,ctx);
		ptr += copy.l;
		buf->bhead += copy.l;
		if (buf->bhead == sizeof(buf->buffer)) buf->bhead = 0;
	}

	if (ptr < data.l) {
		struct gale_data copy;
		send_space(ctx,data.l - ptr,&copy);
		copy.l = data.l - ptr;
		memcpy(copy.p,data.p + ptr,copy.l);
	}
}

void send_space(struct output_context *ctx,size_t len,struct gale_data *data) {
	/* not efficiently implemented for now */
	data->p = gale_malloc(data->l = len);
	send_buffer(ctx,*data,NULL,NULL);
	data->l = 0;
}

void send_buffer(struct output_context *ctx,struct gale_data data,
                 void (*release)(struct gale_data,void *),void *private)
{
	struct output_buffer *buf = (struct output_buffer *) ctx;
	struct segment *seg = &buf->seg[buf->shead];
	assert(buf->shead != buf->stail);
	seg->data = data;
	seg->release = release;
	seg->private = private;
	if (NUM_SEG == ++buf->shead) buf->shead = 0;
}

int output_always_ready(struct output_state *buf) {
	(void) buf;
	return 1;
}
