/* oop-glib.h, liboop, copyright 1999 Dan Egnor

   This is free software; you can redistribute it and/or modify it under the
   terms of the GNU Lesser General Public License, version 2.1 or later.
   See the file COPYING for details. */

#ifndef OOP_TCL_H
#define OOP_TCL_H

#include "oop.h"

/* Create an event source based on the Tcl event loop. */
oop_source *oop_tcl_new(void);

/* Delete the event source so created.  (Uses reference counting.) */
void oop_tcl_done(void);

#endif
