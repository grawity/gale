/* compat.h -- compatibility definitions for various architectures. */

#ifndef GALE_COMPAT_H
#define GALE_COMPAT_H

#include "gale/config.h"

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#if defined(OS_HPUX)
void syslog(int priority, const char *message, ...);
void openlog(const char *ident, int option, int facility);
char *strerror(int);
#endif

#if defined(OS_HPUX)
#define SELECT_ARG_2_T int *
#else
#define SELECT_ARG_2_T fd_set *
#endif

#if defined(OS_SOLARIS)
#define SETSOCKOPT_ARG_4_T char *
#else
#define SETSOCKOPT_ARG_4_T int *
#endif

#if defined(OS_HPUX) || defined(OS_SOLARIS)
#define TPUTS_ARG_3_T int(*)(char)
#elif defined(OS_BSD)
#define TPUTS_ARG_3_T void(*)(int)
#else
#define TPUTS_ARG_3_T int(*)(int)
#endif

#endif
