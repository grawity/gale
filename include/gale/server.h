/* server.h -- handy routines for server processes */

#ifndef GALE_SERVER_H
#define GALE_SERVER_H

extern int gale_debug;    /* debugging level (starts out zero) */

/* Debugging printf.  Will only output if gale_debug > level. */
void gale_dprintf(int level,const char *fmt,...);
/* Daemonize (go into the background).  If keep_tty is true (1), don't detach
   from the tty (gsub does this), otherwise do (like most daemons). */
void gale_daemon(int keep_tty);
/* Look for other processes of the same type on the current tty and kill them
   (if do_kill is 1); register ourselves (with temp files) to get killed in
   a similar manner if appropriate.  The string is an identifier for the
   program type e.g. "gsub". */
void gale_kill(const char *,int do_kill);
/* Register a cleanup function.  This will get called, if at all possible, when
   the program exists, including most signals. */
void gale_cleanup(void (*)(void));
/* Perform all the cleanup functions "early". */
void gale_do_cleanup();

#endif
