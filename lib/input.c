#include "buffer.h"
#include "gale/misc.h"

#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <sys/uio.h>

struct input_buffer {
	struct input_state state;
	byte buffer[1024],*extra;
	size_t remnant;
};

struct input_buffer *create_input_buffer(struct input_state initial) {
	struct input_buffer *buf = gale_malloc(sizeof(*buf));
	buf->state = initial;
	buf->extra = NULL;
	buf->remnant = 0;
	return buf;
}

struct input_state release_input_buffer(struct input_buffer *buf) {
	struct input_state state = buf->state;
	if (NULL != buf->extra) gale_free(buf->extra);
	gale_free(buf);
	return state;
}

static void eat_remnant(struct input_buffer *buf) {
	size_t ptr = 0;
	size_t r = buf->remnant;

	if (buf->state.data.l <= r && buf->state.ready(&buf->state))
	{
		if (NULL != buf->state.data.p) {
			r -= buf->state.data.l;
			buf->state.next(&buf->state);
			if (NULL != buf->extra) {
				gale_free(buf->extra);
				buf->extra = NULL;
			}
		}

		assert(buf->extra == NULL);

		while (buf->state.data.l <= r && buf->state.ready(&buf->state))
		{
			if (NULL == buf->state.data.p)
				buf->state.data.p = buf->buffer + ptr;
			else
				memcpy(buf->state.data.p,buf->buffer + ptr,
				       buf->state.data.l);
			r -= buf->state.data.l;
			ptr += buf->state.data.l;
			buf->state.next(&buf->state);
		}

		buf->remnant = r;

		if (NULL != buf->state.data.p) {
			size_t l = r;
			if (l > buf->state.data.l) l = buf->state.data.l;
			memcpy(buf->state.data.p,buf->buffer + ptr,l);
			ptr += l;
			r -= l;
		}

		memmove(buf->buffer,buf->buffer + ptr,r);
	}
}

void input_buffer_more(struct input_buffer *buf) {
	eat_remnant(buf);
}

int input_buffer_read(struct input_buffer *buf,int fd) {
	if (NULL == buf->state.data.p 
	&&  buf->state.data.l > sizeof(buf->buffer)) 
	{
		buf->extra = gale_malloc(buf->state.data.l);
		buf->state.data.p = buf->extra;
		memcpy(buf->extra,buf->buffer,buf->remnant);
	}

	if (NULL != buf->state.data.p && buf->remnant < buf->state.data.l) {
		struct iovec vec[2];
		int l;
		vec[0].iov_base = buf->state.data.p + buf->remnant;
		vec[0].iov_len = buf->state.data.l - buf->remnant;
		vec[1].iov_base = buf->buffer;
		vec[1].iov_len = sizeof(buf->buffer);
		l = readv(fd,vec,2);
		if (l <= 0) return -1;
		buf->remnant += l;
	} else {
		int l,r = buf->remnant;
		if (NULL != buf->state.data.p) r -= buf->state.data.l;
		l = read(fd,buf->buffer + r,sizeof(buf->buffer) - r);
		if (l <= 0) return -1;
		buf->remnant += l;
	}

	eat_remnant(buf);
	return 0;
}

int input_buffer_ready(struct input_buffer *buf) {
	eat_remnant(buf);
	if (buf->state.data.p)
		return buf->remnant < sizeof(buf->buffer) + buf->state.data.l;
	else
		return buf->remnant < sizeof(buf->buffer);
}

int input_always_ready(struct input_state *buf) {
	(void) buf;
	return 1;
}
