#ifndef DIRECTED_H
#define DIRECTED_H

#include "gale/misc.h"

int is_directed(struct gale_text cat,int *flag,
                struct gale_text *canon,struct gale_text *host);

void sub_directed(oop_source *,struct gale_text host);
void unsub_directed(oop_source *,struct gale_text host);
void send_directed(oop_source *,struct gale_text host);

#endif
