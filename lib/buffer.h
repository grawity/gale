#ifndef BUFFER_H
#define BUFFER_H

#include "gale/types.h"

/* Internal I/O buffer management. */

struct input_buffer;

struct input_state {
	int (*ready)(struct input_state *);
	void (*next)(struct input_state *);
	struct gale_data data;
	void *private;
};

struct input_buffer *create_input_buffer(struct input_state initial);
int input_buffer_ready(struct input_buffer *);
int input_buffer_read(struct input_buffer *,int fd);
void input_buffer_more(struct input_buffer *);

int input_always_ready(struct input_state *);

struct output_buffer;
struct output_context;

struct output_state {
	int (*ready)(struct output_state *);
	void (*next)(struct output_state *,struct output_context *);
	void *private;
};

struct output_buffer *create_output_buffer(struct output_state initial);
int output_buffer_ready(struct output_buffer *);
int output_buffer_write(struct output_buffer *,int fd);

int output_always_ready(struct output_state *);
void send_data(struct output_context *,struct gale_data);
void send_space(struct output_context *,size_t,struct gale_data *);
void send_buffer(struct output_context *,struct gale_data,
                 void (*release)(struct gale_data,void *),void *);

#endif
