#ifndef CONNECT_H
#define CONNECT_H

#include "attach.h"

#include "gale/core.h"

#include "oop.h"

struct connect *new_connect(oop_source *,struct gale_link *,struct gale_text);
void close_connect(struct connect *);

#endif
