#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "gale/util.h"
#include "gale/link.h"
#include "gale/message.h"

struct gale_link {
	int out_size,out_len,out_ptr,in_size,in_len,in_ptr;
	char *out_buf,*in_buf;
	char *out_sub,*in_sub;
	int in_lossage,out_lossage;
	int out_enq,out_ack,in_ack;
	int in_buffer_mode,out_buffer_mode;
	int in_will_mode;
	struct gale_message *in_msg,*out_msg,*in_will,*out_will;
	int queue_size,queue_head,queue_tail;
	struct gale_message **queue;
	int queue_mem,queue_max;
};

struct gale_link *new_link(void) {
	struct gale_link *l = gale_malloc(sizeof(struct gale_link));
	l->out_size = l->in_size = l->out_len = l->in_len = 0;
	l->in_ptr = l->out_ptr = 0;
	l->out_buf = l->in_buf = l->out_sub = l->in_sub = NULL;
	l->in_lossage = l->out_lossage = 0;
	l->out_enq = l->out_ack = l->in_ack = 0;
	l->in_buffer_mode = l->out_buffer_mode = 0;
	l->in_will_mode = 0;
	l->in_msg = l->out_msg = l->in_will = l->out_will = NULL;
	l->queue_size = l->queue_head = l->queue_tail = 0;
	l->queue = NULL;
	l->queue_mem = l->queue_max = 0;
	link_limits(l,32,262144);
	return l;
}

void free_link(struct gale_link *l) {
	if (l->out_buf) gale_free(l->out_buf);
	if (l->in_buf) gale_free(l->in_buf);
	if (l->out_sub) gale_free(l->out_sub);
	if (l->in_sub) gale_free(l->in_sub);
	if (l->in_msg) release_message(l->in_msg);
	if (l->out_msg) release_message(l->out_msg);
	if (l->in_will) release_message(l->in_will);
	if (l->out_will) release_message(l->out_will);
	while (l->queue_tail != l->queue_head) {
		release_message(l->queue[l->queue_tail]);
		l->queue_tail = (l->queue_tail + 1) % l->queue_size;
	}
	if (l->queue) gale_free(l->queue);
}

void link_limits(struct gale_link *l,int num,int mem) {
	struct gale_message **queue;
	int head,tail,size;

	++num;

	queue = l->queue;
	head = l->queue_head;
	tail = l->queue_tail;
	size = l->queue_size;

	l->queue = gale_malloc(sizeof(struct gale_message *) * num);
	l->queue_head = 0;
	l->queue_tail = 0;
	l->queue_size = num;
	l->queue_mem = 0;
	l->queue_max = mem;

	while (tail != head) {
		link_put(l,queue[tail]);
		release_message(queue[tail]);
		tail = (tail + 1) % size;
	}

	gale_free(queue);
}

void reset_link(struct gale_link *l) {
	l->out_ptr = l->out_len = 0;
	if (l->out_buffer_mode) {
		release_message(l->out_msg);
		l->out_buffer_mode = 0;
		l->out_msg = NULL;
		++(l->out_lossage);
	}
	l->in_will_mode = 0;
	l->in_ptr = l->in_len = 0;
	if (l->in_buffer_mode) {
		release_message(l->in_msg);
		l->in_buffer_mode = 0;
		l->in_msg = NULL;
		++(l->in_lossage);
	}
}

int link_receive_q(struct gale_link *l) {
	return (l->in_msg == NULL || l->in_buffer_mode);
}

static void in_msg(struct gale_link *l,char *cmd) {
	struct gale_message *msg = new_message();
	char *cp = strtok(cmd," ");
	if (msg == NULL) return;
	msg->data_size = atoi(cp ? cp : "");
	if (cp) cp = strtok(NULL,"");
	msg->category = gale_strdup(cp ? cp : "");
	if (msg->data_size > l->queue_max)
		msg->data = NULL;
	else
		msg->data = gale_malloc(msg->data_size);
	l->in_msg = msg;
}

static void in_msg_done(struct gale_link *l) {
	if (!l->in_msg->data) {
		release_message(l->in_msg);
		l->in_msg = NULL;
	}
	if (l->in_will_mode) {
		l->in_will_mode = 0;
		if (l->in_msg) {
			if (l->in_will) release_message(l->in_will);
			l->in_will = l->in_msg;
			l->in_msg = NULL;
		}
	}
}

static void process(struct gale_link *l,char *cmd) {
	int i;
	if (sscanf(cmd,"enq %d",&i) == 1)
		l->out_ack = i;
	else if (sscanf(cmd,"ack %d",&i) == 1)
		l->in_ack = i;
	else if (sscanf(cmd,"lossage %d",&i) == 1)
		l->in_lossage += i;
	else if (strncmp(cmd,"puff ",5) == 0)
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

static void incoming(struct gale_link *l,int min) {
	char *cr;
	int len;

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
	if (len > l->in_msg->data_size) len = l->in_msg->data_size;
	if (l->in_msg->data) memcpy(l->in_msg->data,l->in_buf + l->in_ptr,len);
	l->in_ptr += len;
	if (len < l->in_msg->data_size) {
		l->in_len = len;
		l->in_ptr = 0;
		l->in_buffer_mode = 1;
	} else
		in_msg_done(l);
}

int link_receive(struct gale_link *l,int fd) {
	ssize_t r;
	if (!link_receive_q(l)) return 0;
	if (l->in_buffer_mode) {
		if (l->in_msg->data)
			r = read(fd,l->in_msg->data + l->in_len,
			         l->in_msg->data_size - l->in_len);
		else {
			char throwaway[8192];
			unsigned int len = l->in_msg->data_size - l->in_len;
			if (len > sizeof(throwaway)) len = sizeof(throwaway);
			r = read(fd,throwaway,len);
		}
		if (r <= 0) return -1;
		l->in_len += (int) r;
		if (l->in_len == l->in_msg->data_size) {
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

static int outgoing(struct gale_link *l,int size) {
	if (l->out_size < size) {
		char *buf = realloc(l->out_buf,size);
		if (buf == NULL) return -1;
		l->out_size = size;
		l->out_buf = buf;
	}
	return 0;
}

static void out_msg(struct gale_link *l,char *s) {
	l->out_buffer_mode = 1;
	outgoing(l,40 + strlen(l->out_msg->category));
	sprintf(l->out_buf,"%s %d %s\r\n",s,
		l->out_msg->data_size,
	        l->out_msg->category);
	l->out_len = strlen(l->out_buf);
}

int link_transmit_q(struct gale_link *l) {
	if (l->out_len != 0 || l->out_buffer_mode) return 1;
	if (l->out_ack != 0) {
		outgoing(l,20);
		sprintf(l->out_buf,"ack %d\r\n",l->out_ack);
		l->out_len = strlen(l->out_buf);
		l->out_ack = 0;
		return 1;
	}
	if (l->out_enq != 0) {
		outgoing(l,20);
		sprintf(l->out_buf,"enq %d\r\n",l->out_enq);
		l->out_len = strlen(l->out_buf);
		l->out_enq = 0;
		return 1;
	}
	if (l->out_sub) {
		outgoing(l,20 + strlen(l->out_sub));
		sprintf(l->out_buf,"gimme %s\r\n",l->out_sub);
		l->out_len = strlen(l->out_buf);
		gale_free(l->out_sub);
		l->out_sub = NULL;
		return 1;
	}
	if (l->out_lossage) {
		outgoing(l,20);
		sprintf(l->out_buf,"lossage %d\r\n",l->out_lossage);
		l->out_len = strlen(l->out_buf);
		l->out_lossage = 0;
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
		l->queue_mem -= l->out_msg->data_size;
		out_msg(l,"puff");
		return 1;
	}
	return 0;
}

int link_transmit(struct gale_link *l,int fd) {
	int r;
	if (!link_transmit_q(l)) return 0;
	if (l->out_len == 0 && l->out_buffer_mode) {
		if (l->out_msg->data_size == 0) {
			assert(l->out_ptr == 0);
			r = 0;
		} else {
			r = write(fd,l->out_msg->data + l->out_ptr,
			          l->out_msg->data_size - l->out_ptr);
			if (r <= 0) return -1;
		}
		l->out_ptr += r;
		if (l->out_ptr == l->out_msg->data_size) {
			release_message(l->out_msg);
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

int link_queue(struct gale_link *l) {
	int q = l->out_msg ? 1 : 0;
	if (l->queue_tail <= l->queue_head) 
		return q + l->queue_head - l->queue_tail;
	return q + l->queue_size - l->queue_tail + l->queue_head;
}

void link_subscribe(struct gale_link *l,const char *spec) {
	if (l->out_sub) gale_free(l->out_sub);
	l->out_sub = gale_strdup(spec);
}

void link_put(struct gale_link *l,struct gale_message *msg) {
	addref_message(msg);
	l->queue[l->queue_head] = msg;
	l->queue_mem += msg->data_size;
	l->queue_head = (l->queue_head + 1) % l->queue_size;
	if (l->queue_head == l->queue_tail || l->queue_mem > l->queue_max) do {
		l->queue_mem -= l->queue[l->queue_tail]->data_size;
		release_message(l->queue[l->queue_tail]);
		l->queue_tail = (l->queue_tail + 1) % l->queue_size;
		++(l->out_lossage);
	} while (l->queue_mem > l->queue_max);
}

void link_will(struct gale_link *l,struct gale_message *msg) {
	if (l->out_will != NULL) release_message(l->out_will);
	addref_message(l->out_will = msg);
}

void link_enq(struct gale_link *l,int cookie) {
	l->out_enq = cookie;
}

int link_lossage(struct gale_link *l) {
	int r = l->in_lossage;
	l->in_lossage = 0;
	return r;
}

struct gale_message *link_get(struct gale_link *l) {
	struct gale_message *r = l->in_msg;
	if (l->in_buffer_mode) return NULL;
	if (r != NULL) {
		l->in_msg = NULL;
		incoming(l,0);
	}
	return r;
}

struct gale_message *link_willed(struct gale_link *l) {
	struct gale_message *r = l->in_will;
	l->in_will = NULL;
	return r;
}

char *link_subscribed(struct gale_link *l) {
	char *r = l->in_sub;
	l->in_sub = NULL;
	return r;
}

int link_ack(struct gale_link *l) {
	int r = l->in_ack;
	l->in_ack = 0;
	return r;
}
