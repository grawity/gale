#include "gale/misc.h"
#include "gale/core.h"
#include "gale/globals.h"

/* #define CHEESY_ALLOC */
/* #define GC_DEBUG */

#include <stdlib.h>
#include <string.h>

struct gale_ptr { void *ptr; };

/* -- allocator interface --------------------------------------------------- */

#ifndef CHEESY_ALLOC

#include <gc.h>

#if defined(HAVE_GC_BACKPTR_H) && defined(GC_DEBUG)
#include <gc_backptr.h>

static struct gale_text memory_report(void *x) {
	void GC_dump(void); /* YUCK */
	GC_generate_random_backtrace();
	GC_dump();
	return null_text;
}

static inline void init() {
	static int is_init = 0;
	if (!is_init 
	&&  NULL != gale_global 
	&&  NULL != gale_global->report) {
		is_init = 1;
		gale_report_add(gale_global->report,memory_report,NULL);
	}
}
#else  /* HAVE_GC_BACKPTR_H */
static void init() { }
#endif

void *gale_malloc(size_t len) { init(); return GC_MALLOC(len); }
void *gale_malloc_atomic(size_t len) { init(); return GC_MALLOC_ATOMIC(len); }
void *gale_malloc_safe(size_t len) { init(); return GC_MALLOC_UNCOLLECTABLE(len); }
void gale_free(void *ptr) { init(); GC_FREE(ptr); }
void *gale_realloc(void *s,size_t len) { init(); return GC_REALLOC(s,len); }
void gale_check_mem(void) { init(); GC_gcollect(); }

void gale_finalizer(void *obj,void (*f)(void *,void *),void *data) {
	GC_REGISTER_FINALIZER(obj,f,data,0,0);
}

struct gale_ptr *gale_make_weak(void *ptr) {
	struct gale_ptr *wptr = NULL;

	if (ptr) {
		wptr = gale_malloc_atomic(sizeof(*wptr));
		wptr->ptr = ptr;
		GC_GENERAL_REGISTER_DISAPPEARING_LINK(&wptr->ptr,ptr);
	}

	return wptr;
}

#else /* CHEESY_ALLOC */

void *gale_malloc(size_t len) { return malloc(len); }
void *gale_malloc_atomic(size_t len) { return malloc(len); }
void *gale_malloc_safe(size_t len) { return malloc(len); }
void gale_free(void *ptr) { free(ptr); }
void *gale_realloc(void *s,size_t len) { return realloc(s,len); }
void gale_check_mem(void) { }

void gale_finalizer(void *obj,void (*f)(void *,void *),void *data) { }

struct gale_ptr *gale_make_weak(void *ptr) {
	struct gale_ptr *wptr = NULL;

	if (ptr) {
		wptr = malloc(sizeof(*wptr));
		wptr->ptr = ptr;
	}

	return wptr;
}

#endif /* CHEESY_ALLOC */

struct gale_ptr *gale_make_ptr(void *ptr) {
	struct gale_ptr *wptr;
	gale_create(wptr);
	wptr->ptr = ptr;
	return wptr;
}

void *gale_get_ptr(struct gale_ptr *ptr) {
	if (NULL == ptr) return NULL;
	return ptr->ptr;
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
