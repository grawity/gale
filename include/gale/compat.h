/** \file
 *  System compatibility definitions.
 *  \sa gale/config.h */

#ifndef GALE_COMPAT_H
#define GALE_COMPAT_H

#include "gale/config.h"

#include <stdlib.h> /* OpenBSD needs this or other headers fail */
#include <unistd.h> /* MacOS X needs this or same same */

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif

#ifdef HAVE_RUNE_H
#include <rune.h>
#endif

#ifdef HAVE_WCHAR_H
#include <wchar.h>
#endif

#ifdef HAVE_ICONV
#include <iconv.h>
#endif

#if defined(OS_HPUX)
void syslog(int priority, const char *message, ...);
void openlog(const char *ident, int option, int facility);
char *strerror(int);
#endif

/** \def SELECT_ARG_2_T 
 *  The type of the second argument to select() 
 *  (usually 'fd_set *', but sometimes 'int *'). */

#if defined(OS_HPUX)
#define SELECT_ARG_2_T int *
#else
#define SELECT_ARG_2_T fd_set *
#endif

/** \def SETSOCKOPT_ARG_4_T
 *  The type of the fourth argument to setsockopt()
 *  (usually 'int *', but sometimes 'char *'). */

#if defined(OS_SOLARIS)
#define SETSOCKOPT_ARG_4_T char *
#else
#define SETSOCKOPT_ARG_4_T int *
#endif

/** \def TPUTS_ARG_3_T 
 *  The type of the third argument to tputs()
 *  (usually a function which accepts and returns 'int',
 *  but sometimes it accepts 'char' and sometimes it returns 'void'). */

#if defined(OS_HPUX) || defined(OS_SOLARIS)
#define TPUTS_ARG_3_T int(*)(char)
#elif defined(OS_BSD)
#define TPUTS_ARG_3_T void(*)(int)
#else
#define TPUTS_ARG_3_T int(*)(int)
#endif

#endif
