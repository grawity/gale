#ifndef GALE_ERROR_H
#define GALE_ERROR_H

enum { GALE_NOTICE, GALE_WARNING, GALE_ERROR };

typedef void gale_error_f(int severity,char *msg);

extern const char *gale_error_prefix;
extern gale_error_f *gale_error_handler;
gale_error_f gale_error_syslog,gale_error_stderr;

void gale_alert(int severity,const char *,int err);

#endif
