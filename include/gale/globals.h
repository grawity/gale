/* global.h -- global variables used in Gale; protected from the GC. */

#ifndef GALE_GLOBAL_H
#define GALE_GLOBAL_H

#include "gale/types.h"
#include "gale/misc.h"
#include "gale/auth.h"

/* Internal structures. */
struct gale_cleanup;

/* Any global variables in libgale which might reference heap data 
   must go in this structure. */

extern struct gale_global_data {
	/* System directories; set by gale_init. */

	struct gale_text dot_gale;	/* ~/.gale */
	struct gale_text home_dir;	/* ~ */
	struct gale_text sys_dir;	/* .../etc/gale */

	struct gale_text user_cache;	/* ~/.gale/cache */
	struct gale_text system_cache;	/* .../etc/gale/cache */

	/* What to prefix error messages with.  Defaults to the program name. */
	const char *error_prefix;

	/* The original arguments used to invoke the program. */
	int main_argc;
	char * const *main_argv;

	/* Default report generator (written with SIGUSR2). */
	struct gale_report *report;

	/* Debugging level for dprintf().  Starts at zero. */
	int debug_level;

	/* System internals. */
	struct gale_map *cache_tree;
	struct gale_cleanup *cleanup_list;
	struct gale_errors *error;

	/* Default character set encodings to use for various circumstances. */
	struct gale_encoding 
		*enc_ascii,*enc_console,*enc_sys,*enc_filesys,
		*enc_environ,*enc_cmdline;
} *gale_global;

#endif
