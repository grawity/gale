#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include "oop.h"

struct timer {
	struct timeval tv;
	int delay;
};

oop_call_time on_timer;
void *on_timer(oop_source *source,struct timeval tv,void *data) {
	struct timer *timer = (struct timer *) data;
	timer->tv = tv;
	timer->tv.tv_sec += timer->delay;
	source->on_time(source,timer->tv,on_timer,data);
	printf("This message should be output once every ");
	if (1 == timer->delay) printf("second\n");
	else printf("%d seconds\n",timer->delay);
	return OOP_CONTINUE;
}

oop_call_signal stop_timer;
void *stop_timer(oop_source *source,int sig,void *data) {
	struct timer *timer = (struct timer *) data;
	source->cancel_time(source,timer->tv,on_timer,timer);
	source->cancel_signal(source,SIGINT,stop_timer,timer);
	return OOP_CONTINUE;
}

void add_timer(oop_source *source,int interval) {
	struct timer *timer = malloc(sizeof(*timer));
	gettimeofday(&timer->tv,NULL);
	timer->delay = interval;
	source->on_signal(source,SIGINT,stop_timer,timer);
	on_timer(source,timer->tv,timer);
}

oop_call_fd on_data;
void *on_data(oop_source *source,int fd,oop_event event,void *data) {
	char buf[BUFSIZ];
	int r = read(fd,buf,sizeof(buf));
	if (r <= 0) return OOP_HALT;
	write(1,buf,r);
	return OOP_CONTINUE;
}

oop_call_signal stop_data;
void *stop_data(oop_source *source,int sig,void *data) {
	source->cancel_fd(source,0,OOP_READ,on_data,NULL);
	source->cancel_signal(source,SIGINT,stop_data,NULL);
	return OOP_CONTINUE;
}

oop_call_signal on_intr;
void *on_intr(oop_source *source,int sig,void *data) {
	puts("SIGINT (control-C) received, terminating!");
	source->cancel_signal(source,SIGINT,on_intr,NULL);
	return OOP_CONTINUE;
}

int main(void) {
	oop_source_sys *sys = oop_sys_new();
	oop_source *src = oop_sys_source(sys);
	add_timer(src,1);
	add_timer(src,2);
	add_timer(src,3);
	src->on_fd(src,0,OOP_READ,on_data,NULL);
	src->on_signal(src,SIGINT,stop_data,NULL);
	src->on_signal(src,SIGINT,on_intr,NULL);
	oop_sys_run(sys);
	oop_sys_delete(sys);
	return 0;
}
