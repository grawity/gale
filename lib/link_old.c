#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "gale/misc.h"
#include "gale/core.h"
#include "link_old.h"

struct gale_link_old {
	size_t out_ptr,out_size,out_len,in_ptr,in_size,in_len;
	char *out_buf,*in_buf;
	char *out_sub,*in_sub;
	int in_buffer_mode,out_buffer_mode;
	int in_will_mode;
	struct gale_message *in_msg,*out_msg,*in_will,*out_will;
	int queue_size,queue_head,queue_tail;
	struct gale_message **queue;
	size_t queue_mem,queue_max;
};

struct gale_link_old *new_link_old(void) {
	struct gale_link_old *l;
	gale_create(l);
	l->out_size = l->in_size = l->out_len = l->in_len = 0;
	l->in_ptr = l->out_ptr = 0;
	l->out_buf = l->in_buf = l->out_sub = l->in_sub = NULL;
	l->in_buffer_mode = l->out_buffer_mode = 0;
	l->in_will_mode = 0;
	l->in_msg = l->out_msg = l->in_will = l->out_will = NULL;
	l->queue_size = l->queue_head = l->queue_tail = 0;
	l->queue = NULL;
	l->queue_mem = l->queue_max = 0;
	link_limits_old(l,32,262144);
	return l;
}

void free_link_old(struct gale_link_old *l) {
	if (l->out_buf) gale_free(l->out_buf);
	if (l->in_buf) gale_free(l->in_buf);
	if (l->out_sub) gale_free(l->out_sub);
	if (l->in_sub) gale_free(l->in_sub);
	while (l->queue_tail != l->queue_head) {
		l->queue[l->queue_tail] = NULL;
		l->queue_tail = (l->queue_tail + 1) % l->queue_size;
	}
	if (l->queue) gale_free(l->queue);
}

void link_limits_old(struct gale_link_old *l,int num,int mem) {
	struct gale_message **queue;
	int head,tail,size;

	++num;

	queue = l->queue;
	head = l->queue_head;
	tail = l->queue_tail;
	size = l->queue_size;

	gale_create_array(l->queue,num);
	l->queue_head = 0;
	l->queue_tail = 0;
	l->queue_size = num;
	l->queue_mem = 0;
	l->queue_max = mem;

	while (tail != head) {
		link_put_old(l,queue[tail]);
		queue[tail] = NULL;
		tail = (tail + 1) % size;
	}

	gale_free(queue);
}

void reset_link_old(struct gale_link_old *l) {
	l->out_ptr = l->out_len = 0;
	if (l->out_buffer_mode) {
		l->out_buffer_mode = 0;
		l->out_msg = NULL;
	}
	l->in_will_mode = 0;
	l->in_ptr = l->in_len = 0;
	if (l->in_buffer_mode) {
		l->in_buffer_mode = 0;
		l->in_msg = NULL;
	}
}

int link_receive_q_old(struct gale_link_old *l) {
	return (l->in_msg == NULL || l->in_buffer_mode);
}

static void in_msg(struct gale_link_old *l,char *cmd) {
	struct gale_message *msg = new_message();
	char *cp = strtok(cmd," ");
	if (msg == NULL) return;
	msg->data.l = atoi(cp ? cp : "");
	if (cp) cp = strtok(NULL,"");
	msg->cat = gale_text_from_latin1(cp ? cp : "",-1);

	if (msg->data.l > l->queue_max)
		msg->data.p = NULL;
	else
		msg->data.p = gale_malloc(msg->data.l);
	l->in_msg = msg;
}

static void in_msg_done(struct gale_link_old *l) {
	if (!l->in_msg->data.p) l->in_msg = NULL;
	if (l->in_will_mode) {
		l->in_will_mode = 0;
		if (l->in_msg) {
			l->in_will = l->in_msg;
			l->in_msg = NULL;
		}
	}
}

static void process(struct gale_link_old *l,char *cmd) {
	if (strncmp(cmd,"puff ",5) == 0)
		in_msg(l,cmd + 5);
	else if (strncmp(cmd,"will ",5) == 0) {
		l->in_will_mode = 1;
		in_msg(l,cmd + 5);
	} else if (strncmp(cmd,"gimme ",6) == 0) {
		char *s = gale_strdup(cmd + 6);
		if (l->in_sub) gale_free(l->in_sub);
		l->in_sub = s;
	} 
}

static void incoming(struct gale_link_old *l,size_t min) {
	char *cr;
	size_t len;

	if (min < l->in_ptr) min = l->in_ptr;
	do {
		cr = memchr(l->in_buf + min,'\n',l->in_len - min);
		if (cr == NULL) return;
		*cr = '\0';
		if (cr > l->in_buf && cr[-1] == '\r') cr[-1] = '\0';
		process(l,l->in_buf + l->in_ptr);
		min = l->in_ptr = (int)(cr - l->in_buf) + 1;
	} while (l->in_msg == NULL);

	len = l->in_len - l->in_ptr;
	if (len > l->in_msg->data.l) len = l->in_msg->data.l;
	if (l->in_msg->data.p) memcpy(l->in_msg->data.p,l->in_buf + l->in_ptr,len);
	l->in_ptr += len;
	if (len < l->in_msg->data.l) {
		l->in_len = len;
		l->in_ptr = 0;
		l->in_buffer_mode = 1;
	} else
		in_msg_done(l);
}

int link_receive_old(struct gale_link_old *l,int fd) {
	ssize_t r;
	if (!link_receive_q_old(l)) return 0;
	if (l->in_buffer_mode) {
		if (l->in_msg->data.p)
			r = read(fd,l->in_msg->data.p + l->in_len,
			         l->in_msg->data.l - l->in_len);
		else {
			char throwaway[8192];
			unsigned int len = l->in_msg->data.l - l->in_len;
			if (len > sizeof(throwaway)) len = sizeof(throwaway);
			r = read(fd,throwaway,len);
		}
		if (r <= 0) return -1;
		l->in_len += (int) r;
		if (l->in_len == l->in_msg->data.l) {
			l->in_buffer_mode = 0;
			l->in_len = 0;
			in_msg_done(l);
		}
		return 0;
	}
	if (l->in_len == l->in_size) {
		if (l->in_ptr) {
			int len = l->in_len - l->in_ptr;
			memmove(l->in_buf,l->in_buf + l->in_ptr,len);
			l->in_len -= l->in_ptr;
			l->in_ptr = 0;
		} else {
			char *tmp = l->in_buf;
			l->in_size = l->in_size ? l->in_size * 2 : 256;
			l->in_buf = gale_malloc(l->in_size);
			memcpy(l->in_buf,tmp,l->in_len);
			gale_free(tmp);
		}
	}
	r = read(fd,l->in_buf + l->in_len,l->in_size - l->in_len);
	if (r <= 0) return -1;
	l->in_len += (int) r;
	incoming(l,l->in_len - (int) r);
	return 0;
}

static int outgoing(struct gale_link_old *l,size_t size) {
	if (l->out_size < size) {
		char *buf = realloc(l->out_buf,size);
		if (buf == NULL) return -1;
		l->out_size = size;
		l->out_buf = buf;
	}
	return 0;
}

static void out_msg(struct gale_link_old *l,char *s) {
	char *cat;
	l->out_buffer_mode = 1;
	cat = gale_text_to_latin1(l->out_msg->cat);
	outgoing(l,40 + strlen(cat));
	sprintf(l->out_buf,"%s %d %s\r\n",s,
		l->out_msg->data.l,
	        cat);
	l->out_len = strlen(l->out_buf);
	gale_free(cat);
}

int link_transmit_q_old(struct gale_link_old *l) {
	if (l->out_len != 0 || l->out_buffer_mode) return 1;
	if (l->out_sub) {
		outgoing(l,20 + strlen(l->out_sub));
		sprintf(l->out_buf,"gimme %s\r\n",l->out_sub);
		l->out_len = strlen(l->out_buf);
		gale_free(l->out_sub);
		l->out_sub = NULL;
		return 1;
	}
	if (l->out_will) {
		l->out_msg = l->out_will;
		l->out_will = NULL;
		out_msg(l,"will");
		return 1;
	}
	if (l->queue_tail != l->queue_head) {
		l->out_msg = l->queue[l->queue_tail];
		l->queue_tail = (l->queue_tail + 1) % l->queue_size;
		l->queue_mem -= l->out_msg->data.l;
		out_msg(l,"puff");
		return 1;
	}
	return 0;
}

int link_transmit_old(struct gale_link_old *l,int fd) {
	int r;
	if (!link_transmit_q_old(l)) return 0;
	if (l->out_len == 0 && l->out_buffer_mode) {
		if (l->out_msg->data.l == 0) {
			assert(l->out_ptr == 0);
			r = 0;
		} else {
			r = write(fd,l->out_msg->data.p + l->out_ptr,
			          l->out_msg->data.l - l->out_ptr);
			if (r <= 0) return -1;
		}
		l->out_ptr += r;
		if (l->out_ptr == l->out_msg->data.l) {
			l->out_msg = NULL;
			l->out_buffer_mode = 0;
			l->out_ptr = 0;
		}
		return 0;
	}
	r = write(fd,l->out_buf + l->out_ptr,l->out_len - l->out_ptr);
	if (r <= 0) return -1;
	l->out_ptr += r;
	if (l->out_ptr == l->out_len) {
		l->out_len = 0;
		l->out_ptr = 0;
	}
	return 0;
}

int link_queue_old(struct gale_link_old *l) {
	int q = l->out_msg ? 1 : 0;
	if (l->queue_tail <= l->queue_head) 
		return q + l->queue_head - l->queue_tail;
	return q + l->queue_size - l->queue_tail + l->queue_head;
}

void link_subscribe_old(struct gale_link_old *l,const char *spec) {
	if (l->out_sub) gale_free(l->out_sub);
	l->out_sub = gale_strdup(spec);
}

void link_put_old(struct gale_link_old *l,struct gale_message *msg) {
	l->queue[l->queue_head] = msg;
	l->queue_mem += msg->data.l;
	l->queue_head = (l->queue_head + 1) % l->queue_size;
	if (l->queue_head == l->queue_tail || l->queue_mem > l->queue_max) do {
		l->queue_mem -= l->queue[l->queue_tail]->data.l;
		l->queue[l->queue_tail] = NULL;
		l->queue_tail = (l->queue_tail + 1) % l->queue_size;
	} while (l->queue_mem > l->queue_max);
}

void link_will_old(struct gale_link_old *l,struct gale_message *msg) {
	l->out_will = msg;
}

struct gale_message *link_get_old(struct gale_link_old *l) {
	struct gale_message *r = l->in_msg;
	if (l->in_buffer_mode) return NULL;
	if (r != NULL) {
		l->in_msg = NULL;
		incoming(l,0);
	}
	return r;
}

struct gale_message *link_willed_old(struct gale_link_old *l) {
	struct gale_message *r = l->in_will;
	l->in_will = NULL;
	return r;
}

char *link_subscribed_old(struct gale_link_old *l) {
	char *r = l->in_sub;
	l->in_sub = NULL;
	return r;
}
