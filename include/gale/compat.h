/* compat.h -- compatibility definitions for various architectures. */

#ifndef GALE_COMPAT_H
#define GALE_COMPAT_H

#ifdef hpux
/* make up for the lack in <sys/types.h> */
/* #include <sys/bitypes.h> */

typedef unsigned int u_int32_t;
typedef unsigned short u_int16_t;
typedef unsigned char u_int8_t;

void syslog(int priority, const char *message, ...);
void openlog(char *ident, int option, int facility);
char *strerror(int);
#define HPINT (int*)
#else
#define HPINT
#endif

#ifdef linux
#include <sys/bitypes.h>
#endif

#if defined(__sun) && defined(__SVR4)
#define SUNSUCK (char*)
#else
#define SUNSUCK
#endif

#if defined(hpux) || (defined(__sun) && defined(__SVR4))
#define TPUTS_CAST (int(*)(char))
#else
#define TPUTS_CAST
#endif

#endif
