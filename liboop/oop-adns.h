/* oop-adns.h, liboop, copyright 1999 Dan Egnor

   This is free software; you can redistribute it and/or modify it under the
   terms of the GNU Lesser General Public License, version 2.1 or later.
   See the file COPYING for details. */

#ifndef OOP_ADNS_H
#define OOP_ADNS_H

#ifndef ADNS_H_INCLUDED
#error You must include "adns.h" before "oop-adns.h"!
#endif

#include "oop.h"

typedef struct oop_adapter_adns oop_adapter_adns;
typedef struct oop_adns_query oop_adns_query;
typedef void *oop_adns_call(oop_adapter_adns *,adns_answer *,void *);

/* A liboop adns adapter creates an adns instance tied to a liboop source. 
   oop_adns_new() returns NULL on failure.*/
oop_adapter_adns *oop_adns_new(oop_source *,adns_initflags,FILE *diag);
void oop_adns_delete(oop_adapter_adns *);

/* Submit an asynchronous DNS query.  Returns NULL on system failure. 
   The returned pointer is valid until the callback occurs or the
   query is cancelled (see below). */
oop_adns_query *oop_adns_submit(
	oop_adapter_adns *,int *errcode,
	const char *owner,adns_rrtype type,adns_queryflags flags,
	oop_adns_call *,void *);

oop_adns_query *oop_adns_submit_reverse(
	oop_adapter_adns *,int *errcode,
	const struct sockaddr *addr,adns_rrtype type,adns_queryflags flags,
	oop_adns_call *,void *);

/* Cancel a running query. */
void oop_adns_cancel(oop_adns_query *);

#endif
