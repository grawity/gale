#ifndef INIT_H
#define INIT_H

#include "gale/misc.h"

extern struct gale_text 
	_ga_dot_private,_ga_dot_trusted,_ga_dot_local,_ga_dot_auth,
	_ga_etc_private,_ga_etc_trusted,_ga_etc_local,_ga_etc_cache;

void _ga_init();

#endif
