#ifndef ATTACH_H
#define ATTACH_H

#include "connect.h"

#include "gale/core.h"
#include "gale/misc.h"

#include "oop.h"

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

struct attach;
struct attach *new_attach(
	oop_source *source,
	struct gale_text server,
	struct gale_text in,struct gale_text out);
void close_attach(struct attach *);

#endif
