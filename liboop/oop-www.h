/* oop-www.h, liboop, copyright 1999 Dan Egnor

   This is free software; you can redistribute it and/or modify it under the
   terms of the GNU Lesser General Public License, version 2.1 or later.
   See the file COPYING for details. */

#ifndef OOP_WWW_H
#define OOP_WWW_H

#ifndef HTEVENT_H
#error You must include "HTEvent.h" before "oop-www.h"!
#endif

#include "oop.h"

/* Register an event manager with libwww to get events from a liboop source. 
   Because libwww's event loop is global, so is ours. */
void oop_www_register(oop_source *);

/* Release any resources associated with the event manager, and
   unregister it with libwww.  This will leave libwww with no event manager. */
void oop_www_cancel(void);

/* Use libwww's memory management for liboop.
   ** If you use this, you must do so before any other liboop function! ** */
void oop_www_memory(void);

#endif
