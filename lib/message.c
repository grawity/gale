#include "gale/misc.h"
#include "gale/core.h"

struct gale_message *new_message(void) {
	struct gale_message *m;
	gale_create(m);
	m->cat.p = NULL;
	m->cat.l = 0;
	m->data = gale_group_empty();
	return m;
}
