/* readline.c, liboop, copyright 1999 Dan Egnor

   This is free software; you can redistribute it and/or modify it under the
   terms of the GNU Lesser General Public License, version 2.1 or later.
   See the file COPYING for details. */

#ifdef HAVE_READLINE

#include <stdio.h> /* readline needs this! */
#include "readline/readline.h"
#include "oop-rl.h"

static void *on_input(oop_source *oop,int fd,oop_event evt,void *x) {
	rl_callback_read_char();
	return OOP_CONTINUE;
}

void oop_readline_register(oop_source *oop) {
	oop->on_fd(oop,0,OOP_READ,on_input,NULL);
}

void oop_readline_cancel(oop_source *oop) {
	oop->cancel_fd(oop,0,OOP_READ);
}

#endif
