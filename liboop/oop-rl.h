/* oop-rl.h, liboop, copyright 2000 Dan Egnor

   This is free software; you can redistribute it and/or modify it under the
   terms of the GNU Lesser General Public License, version 2.1 or later.
   See the file COPYING for details. */

#ifndef OOP_READLINE_H
#define OOP_READLINE_H

#include "oop.h"

/* Use a liboop event source to call rl_callback_read_char().
   It is up to you to call rl_callback_handler_install().
   Note well that readline uses malloc(), not oop_malloc(). */
void oop_readline_register(oop_source *);

/* Stop notifying readline of input characters. */
void oop_readline_cancel(oop_source *);

#endif
