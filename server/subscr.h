#ifndef SUBSCR_H
#define SUBSCR_H

struct connect;
struct gale_message;

void add_subscr(struct connect *);
void remove_subscr(struct connect *);
void subscr_transmit(struct gale_message *,struct connect *avoid);

#endif
