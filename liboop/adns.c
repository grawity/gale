/* adns.c, liboop, copyright 1999 Dan Egnor

   This is free software; you can redistribute it and/or modify it under the
   terms of the GNU Lesser General Public License, version 2.1 or later.
   See the file COPYING for details. */

#ifdef HAVE_ADNS

#include "oop.h"
#include "adns.h"
#include "oop-adns.h"

#include <assert.h>

struct oop_adapter_adns {
	oop_source *source;
	oop_adapter_select *select;
	adns_state state;
	int count;
};

struct oop_adns_query {
	oop_adapter_adns *a;
	adns_query query;
	oop_adns_call *call;
	void *data;
};

static oop_call_select on_select;
static oop_call_time on_process;
static void set_select(oop_adapter_adns *);

oop_adapter_adns *oop_adns_new(
	oop_source *source,
	adns_initflags flags,FILE *diag) 
{
	oop_adapter_adns *a = oop_malloc(sizeof(*a));
	if (NULL == a) return NULL;
	a->select = NULL;
	a->state = NULL;

	if (adns_init(&a->state,flags | adns_if_noautosys,diag)
	|| (NULL == (a->select = oop_select_new(source,on_select,a)))) {
		if (NULL != a->state) adns_finish(a->state);
		if (NULL != a->select) oop_select_delete(a->select);
		oop_free(a);
		return NULL;
	}

	a->source = source;
	a->count = 0;
	return a;
}

void oop_adns_delete(oop_adapter_adns *a) {
	assert(0 == a->count && 
	       "deleting oop_adapter_adns with outstanding queries");
	a->source->cancel_time(a->source,OOP_TIME_NOW,on_process,a);
	oop_select_delete(a->select);
	adns_finish(a->state);
	oop_free(a);
}

oop_adns_query *oop_adns_submit(
	oop_adapter_adns *a,
	const char *owner,adns_rrtype type,adns_queryflags flags,
	oop_adns_call *call,void *data)
{
	oop_adns_query *q = oop_malloc(sizeof(*q));
	if (NULL == q) return NULL;
	if (adns_submit(a->state,owner,type,flags,q,&q->query)) {
		oop_free(q);
		return NULL;
	}

	q->a = a;
	q->call = call;
	q->data = data;
	++q->a->count;
	set_select(a);
	return q;
}

void oop_adns_cancel(oop_adns_query *q) {
	adns_cancel(q->query);
	--q->a->count;
	set_select(q->a);
	oop_free(q);
}

static void set_select(oop_adapter_adns *a) {
	fd_set rfd,wfd,xfd;
	struct timeval buf,*out = NULL,now;
	int maxfd = 0;
	FD_ZERO(&rfd);
	FD_ZERO(&wfd);
	FD_ZERO(&xfd);
	gettimeofday(&now,NULL);
	adns_beforeselect(a->state,&maxfd,&rfd,&wfd,&xfd,&out,&buf,&now);
	oop_select_set(a->select,maxfd,&rfd,&wfd,&xfd,out);
}

static void *on_process(oop_source *source,struct timeval when,void *data) {
	oop_adapter_adns *a = (oop_adapter_adns *) data;
	adns_answer *r;
	adns_query query;
	oop_adns_query *q = NULL;
	void *adns_data;

	query = NULL;
	if (0 == adns_check(a->state,&query,&r,&adns_data)) {
		q = (oop_adns_query *) adns_data;
		assert(query == q->query);
	}

	set_select(a);

	if (NULL != q) {
		oop_adns_call *call = q->call;
		void *data = q->data;
		assert(a == q->a);
		--q->a->count;
		oop_free(q);
		source->on_time(source,when,on_process,a);
		return call(a,r,data);
	}

	return OOP_CONTINUE;
}

static void *on_select(
	oop_adapter_select *select,
	int num,fd_set *rfd,fd_set *wfd,fd_set *xfd,
	struct timeval now,void *data)
{
	oop_adapter_adns *a = (oop_adapter_adns *) data;

	adns_afterselect(a->state,num,rfd,wfd,xfd,&now);
	return on_process(a->source,OOP_TIME_NOW,a);
}

#endif
