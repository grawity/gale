/* global.h -- global variables used in Gale; protected from the GC. */

#ifndef GALE_GLOBAL_H
#define GALE_GLOBAL_H

#include "gale/types.h"
#include "gale/misc.h"
#include "gale/auth.h"

/* Any global variables in libgale which might reference heap data 
   must go in this structure. */

extern struct gale_global_data {
	/* Standard system directories; set by gale_init.
           home_dir => ~
           dot_gale => ~/.gale
           sys_dir => .../etc/gale */
	struct gale_text dot_gale,home_dir,sys_dir;

	/* What to prefix error messages with.  Defaults to the program name. */
	const char *error_prefix;

        /* The error handler to use. */
	gale_error_f *error_handler;

	/* Debugging level for dprintf().  Starts at zero. */
	int debug_level;

	/* Hooks for the auth system to find keys with. */
	auth_hook *find_public,*find_private;
} *gale_global;

#endif
