#ifndef CONNECT_H
#define CONNECT_H

#include "gale/core.h"

#include "oop.h"

typedef struct gale_message *filter(struct gale_message *,void *);

struct connect *new_connect(oop_source *,struct gale_link *,struct gale_text);
void connect_filter(struct connect *,filter *,void *);
void send_connect(struct connect *,struct gale_message *);
void close_connect(struct connect *);

#endif
