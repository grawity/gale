#ifndef LINK_OLD_H
#define LINK_OLD_H

#include "gale/core.h"

struct gale_link_old;

struct gale_link_old *new_link_old(void);
void free_link_old(struct gale_link_old *);
void link_limits_old(struct gale_link_old *,int num,int mem);
void reset_link_old(struct gale_link_old *);

int link_receive_q_old(struct gale_link_old *);
int link_receive_old(struct gale_link_old *,int fd);
int link_transmit_q_old(struct gale_link_old *);
int link_transmit_old(struct gale_link_old *,int fd);

void link_subscribe_old(struct gale_link_old *,const char *spec);
void link_put_old(struct gale_link_old *,struct gale_message *);
void link_will_old(struct gale_link_old *,struct gale_message *);

int link_queue_old(struct gale_link_old *);
struct gale_message *link_get_old(struct gale_link_old *);
struct gale_message *link_willed_old(struct gale_link_old *);
char *link_subscribed_old(struct gale_link_old *);

#endif
