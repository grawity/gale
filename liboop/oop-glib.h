/* oop-glib.h, liboop, copyright 1999 Dan Egnor

   This is free software; you can redistribute it and/or modify it under the
   terms of the GNU Lesser General Public License, version 2.1 or later.
   See the file COPYING for details. */

#ifndef OOP_GLIB_H
#define OOP_GLIB_H

#ifndef __G_LIB_H__
#error You must include "glib.h" before "oop-glib.h"!
#endif

#include "oop.h"

/* Create an event source based on the GLib event loop. */
oop_source *oop_glib_new(void);

/* Delete the event source so created.  (Uses reference counting.) */
void oop_glib_delete(void);

/* Get the value used to terminate the event loop (e.g. OOP_HALT). */
void *oop_glib_return(void);

#endif
