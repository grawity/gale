/* oop-read.h, liboop, copyright 2000 Ian Jackson

   This is free software; you can redistribute it and/or modify it under the
   terms of the GNU Lesser General Public License, version 2.1 or later.
   See the file COPYING for details. */

#ifndef OOP_READ_H
#define OOP_READ_H

#include "oop.h"

/* ------------------------------------------------------------------------- */
/* Generic interface for readable bytestreams */

typedef struct oop_readable oop_readable;

typedef void *oop_readable_call(oop_source*, oop_readable*, void*);

struct oop_readable {
  int (*on_readable)(struct oop_readable*, oop_readable_call*, void*);
   /* Calls back as soon as any data available.  Only one on_read can
    * be set for any oop_readable. */
  void (*on_cancel)(struct oop_readable*);
  ssize_t (*try_read)(struct oop_readable*, void *buffer, size_t length);
   /* Just like read(2), but never gives EINTR, but may give EAGAIN.
    * Never cancels, never blocks. */
  int (*delete_tidy)(struct oop_readable*); /* resets any things done by _new */
  void (*delete_kill)(struct oop_readable*); /* just frees etc.; use eg after fork */
};

/* ------------------------------------------------------------------------- */
/* Interpret an fd as a readable bytestream           */
/* simple wrapper around fcntl, oop->on_fd and read() */

oop_readable *oop_readable_fd(oop_source*, int fd);
/* side-effect on fd is to make it nonblocking.
 * delete_tidy resets blocking. */

int oop_fd_nonblock(int fd, int nonblock);
/* Utility function.  Returns 0 if OK, errno value if it fails. */


/* ------------------------------------------------------------------------- */
/* Interpret a block of memory as a readable bytestream */
/* Is always ready for reading, of course.              */

oop_readable *oop_readable_mem(oop_source*, const void *data, size_t length);
/* Stores a pointer to data, rather than copying it. */


/* ------------------------------------------------------------------------- */
/* Record-structured `cooked' reading from any readable bytestream */

/*
 * Input stream is treated as series of records.
 *
 * If no delimiter is specified (_DELIM_NONE) then the records are
 * of fixed size (sz arg to oop_rd_read); otherwise file is sequence of
 * pairs {record data, delimiter string}.
 *
 * Records may end early under some circumstances:
 *  - with _SHORTREC_SOONEST, record boundary always `interpreted'
 *    whereever input would block.  Note that streams don't usually
 *    guarantee position of blocking boundaries.  Use this with
 *    _DELIM_NONE only if record boundaries are not important.
 *  - with _SHORTREC_EOFONLY or _BUFFULL, at end of file a partial
 *    record is always OK, so missing delimiter at EOF, or short last
 *    fixed-length record, is fine;
 *  - with _SHORTREC_BUFFULL, if the sz is exceeded by the record
 *    length - in this case the record is split in two (or more), the
 *    first (strictly: all but last) of which will be passed to ifok
 *    with no delimiter and event type _RD_BUFFULL, the last with the
 *    delimiter attached (if _DELIM_KEEP) and event _RD_OK.
 *
 * If, with delimited records, the delimiter doesn't appear within the
 * sz, and _BUFFULL or _SOONEST are not specified, then iferr is
 * called with _RD_BUFFULL.  Likewise, if the final record is too
 * short (for fixed-size records) or missing its delimiter (for
 * delimited ones) then without _SHORTREC_BUFFULL or
 * _SHORTREC_EOFONLY, iferr is called with _RD_PARTREC.
 *
 * Reading will continue until EOF or an error.  ifok may be called
 * any number of times with data!=0, and then there will be either one
 * further call to ifok with data==0, or alternatively one call to
 * iferr.
 *
 * You can call _rd_cancel at any point (eg in a callback) to prevent
 * further calls to your callbacks.
 *
 * An oop_read may read ahead as much as it likes in the stream any
 * time after the first call to _rd_read.  This can be prevented by
 * calling _rd_bufcontrol with a non-0 debuf argument; if called
 * before the first _rd_read then debuf will not be called, and the
 * oop_read will not read ahead `unnecessarily' (see below).  If
 * called afterwards, then any buffered data will be presented to the
 * debuf callback, once, and then matters are as above.  If
 * _rd_bufcontrol is called with 0 for debuf then buffering is
 * (re)-enabled.
 *
 * `unnecessary' readahead: with no delimiter, the readahead will
 * always be less than the record size (sz argument to _rd_read); with
 * a delimiter, it will be less than the maximum record size if any
 * except that we won't read past the end of a read(2) return if the
 * delimiter is in the returned data.  If styles and record sizes are
 * mixed then the readahead point will of course not go backwards, but
 * apart from that the most recent style and record size will apply.
 *
 * Calling _rd_delete will discard any read ahead data !
 *
 * ifok and iferr may be the same function; the sets of arguments
 * passed to it then will be unambiguous.
 *
 * With _NUL_DISCARD, any null bytes in the input still count against
 * the maximum record size, even though they are not included in the
 * record size returned.
 */
 
typedef struct oop_read oop_read;

typedef enum { /* If you change these, also change eventstrings in read.c */
  OOP_RD_OK,
  OOP_RD_EOF,     /* EOF; data==0                                            */
  OOP_RD_PARTREC, /* partial record at EOF; data!=0                          */
  OOP_RD_LONG,    /* too much data before delimiter; data==0 if error        */
  OOP_RD_NUL,     /* nul byte in data, with _NUL_FORBID; data==0             */
  OOP_RD_SYSTEM   /* system error, look in errnoval, data may be !=0         */
} oop_rd_event;

typedef enum {
  OOP_RD_BUFCTL_QUERY,  /* return amount of read-ahead data */
  OOP_RD_BUFCTL_ENABLE, /* enable, return 0 */
  OOP_RD_BUFCTL_DISABLE,/* disable but keep any already read, return that amt */
  OOP_RD_BUFCTL_FLUSH   /* disable and discard, return amount discarded */
} oop_rd_bufctl_op;
size_t oop_rd_bufctl(oop_read*, oop_rd_bufctl_op op);

typedef enum {
  OOP_RD_DELIM_NONE,  /* there is no delimiter specified */
  OOP_RD_DELIM_STRIP, /* strip the delimiter */
  OOP_RD_DELIM_KEEP   /* keep the delimiter */
} oop_rd_delim;

typedef enum {
  OOP_RD_NUL_FORBID,  /* bad for general-purpose data files ! */
  OOP_RD_NUL_DISCARD, /* bad for general-purpose data files ! */
  OOP_RD_NUL_PERMIT
} oop_rd_nul;

typedef enum {             /* record may end early without error if:       */
  OOP_RD_SHORTREC_FORBID,  /*   never (both conditions above are an error) */
  OOP_RD_SHORTREC_EOF,     /*   EOF                                        */
  OOP_RD_SHORTREC_LONG,    /*   EOF or record too long                     */
  OOP_RD_SHORTREC_SOONEST  /*   any data read at all                       */
} oop_rd_shortrec;

typedef struct {
  oop_rd_delim delim_mode; /* if _DELIM_NONE, delim=='\0',             */
  char delim;              /*  otherwise delim must be valid           */
  oop_rd_nul nul_mode; /* applies to data content, not to any in delim */
  oop_rd_shortrec shortrec_mode;
} oop_rd_style;

typedef void *oop_rd_call(oop_source*, oop_read*,
			  oop_rd_event, const char *errmsg, int errnoval,
			  const char *data, size_t recsz, void*);
/*
 * When called as `ifok':
 *  _result indicates why the record ended early (or OK if it didn't);
 *  data is 0 iff no record was read because EOF
 *  errmsg==0, errnoval==0
 *
 * When called as `iferr':
 *  _result indicates the error (and is not _OK); if it is _SYSTEM
 *  then errmsg is strerror(errnoval), otherwise errmsg is from
 *  library and errnoval is 0.  Errors in a record do NOT cause any
 *  data to be discarded, though some may be passed to the iferr call;
 *  if data==0 then calling oop_rd_read again with the same style may
 *  produce the same error again.
 *
 * data will always be nul-terminated, may also contain nuls unless
 * _NUL_FORBID specified.  recsz does not include the trailing nul; if
 * _DELIM_STRIP then it doesn't include the (now-stripped) delimiter,
 * but if _DELIM_KEEP then if there was a delimiter it is included in
 * recsz.
 *
 * Any data allocated by oop_read, and errmsg if set, is valid only
 * during this call - you must copy it !  (Also invalidated by
 * _rd_delete, but not by _rd_cancel.)
 */

const char *oop_rd_errmsg(oop_read *rd, oop_rd_event event, int errnoval,
			  const oop_rd_style *style);
/* style is a hint; it may be NUL.  The returned value is valid only
 * until this event call finishes (as if it had come from
 * oop_call_rd).  Will never return NULL.
 */

oop_read *oop_rd_new(oop_source*, oop_readable *ra, char *buf, size_t bufsz);
/* buf may be 0, in which case a buffer will be allocated internally
 * (and should then not be touched at all while the oop_read exists).
 * bufsz is the actual size of buf, or 0 if buf==0. */
void oop_rd_delete(oop_read*);

oop_read *oop_rd_new_fd(oop_source*, int fd, char *buf, size_t bufsz);
/* Uses oop_readable_fd first. */

int oop_rd_delete_tidy(oop_read*);
void oop_rd_delete_kill(oop_read*);
/* Also call the delete_tidy or delete_kill methods of the underlying
 * readable.  Make sure to use these if you use oop_rd_new_fd. */

/* predefined styles:                               DELIM    NUL    SHORTREC */
extern const oop_rd_style OOP_RD_STYLE_GETLINE[1];/*STRIP \n FORBID ATEOF    */
extern const oop_rd_style OOP_RD_STYLE_BLOCK[1]; /* NONE     ALLOW  FIXED    */
extern const oop_rd_style OOP_RD_STYLE_IMMED[1]; /* NONE     ALLOW  SOONEST  */
/* these are all 1-element arrays so you don't have to say &... */

int oop_rd_read(oop_read*, const oop_rd_style *style, size_t maxrecsz,
		oop_rd_call *ifok, void*,
		oop_rd_call *iferr, void*);
/* The data passed to ifok is only valid for that call to ifok (also
 * invalidated by _rd_delete, but not by _rd_cancel.).  maxrecsz is
 * the maximum value of recsz which will be passed to ifok or iferr,
 * or 0 for no limit.
 *
 * NB that if a caller-supplied buffer is being used then its size
 * should be at least 1 larger than maxrecsz; otherwise the value of
 * maxrecsz actually used will be reduced.
 *
 * Errors imply _rd_cancel.
 *
 * Only one _rd_read at a time per oop_read.
 */

void oop_rd_cancel(oop_read*);

#endif
