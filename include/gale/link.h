#ifndef LINK_H
#define LINK_H

struct gale_link;
struct gale_message;

struct gale_link *new_link(void);
void free_link(struct gale_link *);
void link_limits(struct gale_link *,int num,int mem);
void reset_link(struct gale_link *);

int link_receive_q(struct gale_link *);
int link_receive(struct gale_link *,int fd);
int link_transmit_q(struct gale_link *);
int link_transmit(struct gale_link *,int fd);

void link_subscribe(struct gale_link *,const char *spec);
void link_put(struct gale_link *,struct gale_message *);
void link_will(struct gale_link *,struct gale_message *);
void link_enq(struct gale_link *,int cookie);

int link_queue(struct gale_link *);
int link_lossage(struct gale_link *);
struct gale_message *link_get(struct gale_link *);
struct gale_message *link_willed(struct gale_link *);
char *link_subscribed(struct gale_link *);
int link_ack(struct gale_link *);

#endif
