#include "gale/misc.h"
#include "gale/core.h"

#include <stdlib.h>
#include <string.h>
#include <gc.h>

/* -- allocator interface --------------------------------------------------- */

void *gale_malloc(size_t len) {
	return GC_malloc(len);
}

void *gale_malloc_atomic(size_t len) {
	return GC_malloc_atomic(len);
}

void *gale_malloc_safe(size_t len) {
	return GC_malloc_uncollectable(len);
}

void gale_free(void *ptr) {
	GC_free(ptr);
}

void *gale_realloc(void *s,size_t len) {
	return GC_realloc(s,len);
}

void gale_finalizer(void *obj,void (*f)(void *,void *),void *data) {
	GC_register_finalizer(obj,f,data,0,0);
}

void gale_weak_ptr(void **ptr) {
	GC_general_register_disappearing_link(ptr,*ptr);
}

/* -------------------------------------------------------------------------- */

struct gale_data gale_data_copy(struct gale_data d) {
	struct gale_data r;
	r.p = gale_malloc(d.l);
	memcpy(r.p,d.p,r.l = d.l);
	return r;
}

int gale_data_compare(struct gale_data a,struct gale_data b) {
	size_t min = (a.l > b.l) ? b.l : a.l;
	int result = memcmp(a.p,b.p,min);
	if (result != 0) return result;
	if (a.l < b.l) return -1;
	if (a.l > b.l) return 1;
	return 0;
}
