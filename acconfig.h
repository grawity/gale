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


/** Character set translation functions are available. */
#undef HAVE_ICONV

/** The dynamic loader library is available. */
#undef HAVE_LIBDL

/** The GNU readline library is available. */
#undef HAVE_LIBREADLINE

/** Veillard's XML parser library is available. */
#undef HAVE_LIBXML

/** The SOCKS firewall proxy is available. */
#undef HAVE_SOCKS

/** The ADNS resolver library is available. */
#undef HAVE_ADNS

/** This is a BSD-based operating system. */
#undef OS_BSD

/** This is Hewlett-Packard HP-UX. */
#undef OS_HPUX

/** This is SGI IRIX. */
#undef OS_IRIX

/** This is Sun Solaris. */
#undef OS_SOLARIS


/* Leave that blank line there!!  Autoheader needs it.
   If you're adding to this file, keep in mind:
   The entries are in sort -df order: alphabetical, case insensitive,
   ignoring punctuation (such as underscores).  */
