/* global.h -- global variables used in Gale; protected from the GC. */

#ifndef GALE_GLOBAL_H
#define GALE_GLOBAL_H

#include "gale/types.h"
#include "gale/misc.h"
#include "gale/auth.h"

/* Any global variables in libgale which might reference heap data 
   must go in this structure. */

extern struct gale_global_data {
	/* System directories; set by gale_init. */

	struct gale_text dot_gale;	/* ~/.gale */
	struct gale_text home_dir;	/* ~ */
	struct gale_text sys_dir;	/* .../etc/gale */

	struct gale_text user_cache;	/* ~/.gale/cache */
	struct gale_text system_cache;	/* .../etc/gale/cache */

	struct gale_text dot_auth;	/* ~/.gale/auth */
	struct gale_text dot_trusted;	/* ~/.gale/auth/trusted */
	struct gale_text dot_private;	/* ~/.gale/auth/private */
	struct gale_text dot_local;	/* ~/.gale/auth/local */

	struct gale_text sys_auth;	/* .../etc/gale/auth */
	struct gale_text sys_trusted;	/* .../etc/gale/auth/trusted */
	struct gale_text sys_private;	/* .../etc/gale/auth/private */
	struct gale_text sys_local;	/* .../etc/gale/auth/local */
	struct gale_text sys_cache;	/* .../etc/gale/auth/cache */

	/* What to prefix error messages with.  Defaults to the program name. */
	const char *error_prefix;

        /* The error handler to use. */
	gale_error_f *error_handler;

	/* Debugging level for dprintf().  Starts at zero. */
	int debug_level;

	/* Hooks for the auth system to find keys with. */
	auth_hook *find_public,*find_private;

	/* The calling user's ID. */
	struct auth_id *user_id;

	/* The system environment. */
	char **environ;

	/* System internals. */
	struct gale_wt *auth_tree;
	struct gale_wt *cache_tree;
} *gale_global;

#endif
