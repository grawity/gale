/* gsubrc.h -- API for loadable gsub modules */

#ifndef GALE_GSUBRC_H
#define GALE_GSUBRC_H

/* To use either of these interfaces, create a shared library exporting the
   respective function and place it in the same place as a standard "gsubrc", 
   but named "gsubrc.so".  The gsub will dynamically load the library and
   call the function as appropriate. */

/* -- v 0.13b: simple interface, very similar to gsubrc -------------------- */

/* To use this interface, export a function with the following signature.

   Rather than looking for an external "gsubrc", gsub will simply call the
   following function under the same conditions.  Information is communicated
   with environment variables and standard input, just as with gsubrc.

   To support this, gsub forks once per message.  You therefore do not need to
   clean up after yourself in this function, but you also cannot keep track of
   persistent state without using the filesystem or another external mechanism.

   The function can return status directly, or call exit() with status value.  
   Nonzero status will cause gsub to suppress any return receipts. */

typedef int (gsubrc_t)(void);
gsubrc_t gsubrc;

/* -- v 0.14: extended interface ------------------------------------------- */

/* To use this interface, export a function with the following signature.
   It will take precedence over the previous interface.

   This is a somewhat more sophisticated hook.  The gsub will not fork before
   calling this function, so you can easily keep persistent state, but you must
   also take care to avoid damaging the process or leaking resources.

   "env" is the environment that would be passed to a regular gsubrc, as a
   NULL-terminated array of string pointers of the form "VAR=VALUE".  This 
   includes the system environment as well as GALE_* and HEADER_* variables.

   "msg" is the message body text.  This is not NUL-terminated!
   "len" is the length of the message body text.

   The function should not exit (since it runs in the same process as gsub).
   The return value from the function is the status; nonzero values will cause
   gsub to suppress any return receipts. */

typedef int (gsubrc2_t)(char * const * const env,const char *msg,int len);
gsubrc2_t gsubrc2;

#endif
