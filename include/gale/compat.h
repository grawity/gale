#ifndef GALE_COMPAT_H
#define GALE_COMPAT_H

#ifdef hpux
void syslog(int priority, const char *message, ...);
void openlog(char *ident, int option, int facility);
char *strerror(int);
#define HPINT (int*)
#else
#define HPINT
#endif

#endif
