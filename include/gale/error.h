/* error.h -- gale error management */

#ifndef GALE_ERROR_H
#define GALE_ERROR_H

/* Types of error severity. */
enum { GALE_NOTICE, GALE_WARNING, GALE_ERROR };

/* Error handler function. */
typedef void gale_error_f(int severity,char *msg);

/* The prefix to prepend to error names -- usually the program name, set up
   by gale_init (see util.h) */
extern const char *gale_error_prefix;
/* The error handler to use -- set up to a default by gale_init, but you can
   change this for custom error processing. */
extern gale_error_f *gale_error_handler;
/* Standard error handlers. */
gale_error_f gale_error_syslog,gale_error_stderr;

/* Report an error.  If GALE_ERROR, the program will terminate, otherwise it
   will continue. */
void gale_alert(int severity,const char *,int err);

#endif
