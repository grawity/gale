#ifndef GALE_COMPAT_H
#define GALE_COMPAT_H

#ifdef hpux
/* make up for the lack in <sys/types.h> */
#include <model.h>
typedef u_int32 u_int32_t;
typedef u_int16 u_int16_t;
typedef u_int8 u_int8_t;
#endif

#ifdef hpux
void syslog(int priority, const char *message, ...);
void openlog(char *ident, int option, int facility);
char *strerror(int);
#define HPINT (int*)
#else
#define HPINT
#endif

#if defined(__sun__) && defined(__svr4__)
#define SUNSUCK (char*)
#else
#define SUNSUCK
#endif

#endif
