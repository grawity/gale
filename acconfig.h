/* acconfig.h
   This file is in the public domain.

   Descriptive text for the C preprocessor macros that
   the distributed Autoconf macros can define.
   No software package will use all of them; autoheader copies the ones
   your configure.in uses into your configuration header file templates.

   The entries are in sort -df order: alphabetical, case insensitive,
   ignoring punctuation (such as underscores).  Although this order
   can split up related entries, it makes it easier to check whether
   a given entry is in the file.

   Leave the following blank line there!!  Autoheader needs it.  */


/* The dynamic loader library */
#undef HAVE_LIBDL

/* The GNU readline library */
#undef HAVE_LIBREADLINE

/* The SOCKS firewall proxy */
#undef HAVE_SOCKS

/* BSD-based operating systems */
#undef OS_BSD

/* Hewlett-Packard HPUX */
#undef OS_HPUX

/* SGI IRIX */
#undef OS_IRIX

/* Sun Solaris */
#undef OS_SOLARIS

/* Package name */
#undef PACKAGE

/* Version */
#undef VERSION


/* Leave that blank line there!!  Autoheader needs it.
   If you're adding to this file, keep in mind:
   The entries are in sort -df order: alphabetical, case insensitive,
   ignoring punctuation (such as underscores).  */
