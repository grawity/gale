/* glibconfig.h spoof -- exists only to make glib.h work */

#ifndef GLIBCONFIG_H_SPOOF
#define GLIBCONFIG_H_SPOOF

/* cheesy */
typedef unsigned char guint8;
typedef unsigned short guint16;
typedef unsigned int guint32;
typedef int gint32;

#ifdef HAVE_POLL_H
#include <poll.h>
#endif

#if defined(POLLIN) && defined(POLLOUT)
#define GLIB_SYSDEF_POLLIN =POLLIN
#define GLIB_SYSDEF_POLLOUT =POLLOUT
#define GLIB_SYSDEF_POLLPRI =POLLPRI
#define GLIB_SYSDEF_POLLERR =POLLERR
#define GLIB_SYSDEF_POLLHUP =POLLHUP
#define GLIB_SYSDEF_POLLNVAL =POLLNVAL
#else
#define GLIB_SYSDEF_POLLIN =1
#define GLIB_SYSDEF_POLLOUT =4
#define GLIB_SYSDEF_POLLPRI =2
#define GLIB_SYSDEF_POLLERR =8
#define GLIB_SYSDEF_POLLHUP =16
#define GLIB_SYSDEF_POLLNVAL =32
#endif

#endif
