#ifndef SUBSCR_H
#define SUBSCR_H

struct connect;
struct gale_message;

void add_subscr(struct gale_text,struct gale_link *);
void remove_subscr(struct gale_text,struct gale_link *);
void subscr_transmit(struct gale_message *,struct gale_link *avoid);

#endif
