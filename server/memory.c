#include <syslog.h>
#include <stdlib.h>

#include "gale/util.h"
#include "gale/compat.h"
#include "gale/server.h"
#include "server.h"

void *gale_malloc(size_t size) {
	void *r = malloc(size);
	if (size && !r) {
		syslog(LOG_CRIT,"Oof!  Out of memory.  Terminating!");
		gale_dprintf(0,"!!! out of memory\n");
		exit(1);
	}
	return r;
}

void gale_free(void *r) {
	free(r);
}
