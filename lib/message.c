#include <stdlib.h>
#include <string.h>

#include "gale/util.h"
#include "gale/message.h"

struct gale_message *new_message(void) {
	struct gale_message *m = gale_malloc(sizeof(struct gale_message));
	m->category = NULL;
	m->data_size = 0;
	m->data = NULL;
	m->ref = 1;
	return m;
}

void addref_message(struct gale_message *m) {
	++(m->ref);
}

void release_message(struct gale_message *m) {
	--(m->ref);
	if (m->ref == 0) {
		if (m->category != NULL) gale_free(m->category);
		if (m->data != NULL) gale_free(m->data);
		gale_free(m);
	}
}
