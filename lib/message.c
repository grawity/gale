#include <stdlib.h>
#include <string.h>

#include "gale/misc.h"
#include "gale/core.h"

struct gale_message *new_message(void) {
	struct gale_message *m = gale_malloc(sizeof(struct gale_message));
	m->cat.p = NULL;
	m->cat.l = 0;
	m->data.p = NULL;
	m->data.l = 0;
	m->ref = 1;
	return m;
}

void addref_message(struct gale_message *m) {
	++(m->ref);
}

void release_message(struct gale_message *m) {
	--(m->ref);
	if (m->ref == 0) {
		if (m->cat.p != NULL) gale_free(m->cat.p);
		if (m->data.p != NULL) gale_free(m->data.p);
		gale_free(m);
	}
}
