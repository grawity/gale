/* read.c, liboop, copyright 2000 Ian jackson
   
   This is free software; you can redistribute it and/or modify it under the
   terms of the GNU Lesser General Public License, version 2.1 or later.
   See the file COPYING for details. */

#include "oop.h"
#include "oop-read.h"

#include <string.h>
#include <errno.h>
#include <assert.h>
#include <limits.h>

#undef MIN /* for systems that define it */
#define MIN(a,b) ((a)<(b) ? (a) : (b))

static void *on_time(oop_source*, struct timeval, void*);
static void *on_readable(oop_source*, oop_readable*, void*);
static void *on_process(oop_source*, oop_read*, int try_read);

static int set_time_ifbuf(oop_source *oop, oop_read *rd);
static void cancel_time(oop_source *oop, oop_read *rd);

const oop_rd_style OOP_RD_STYLE_GETLINE[]= {{
  OOP_RD_DELIM_STRIP,'\n', OOP_RD_NUL_FORBID, OOP_RD_SHORTREC_EOF,
}};
const oop_rd_style OOP_RD_STYLE_BLOCK[]= {{
  OOP_RD_DELIM_NONE, 0,    OOP_RD_NUL_PERMIT, OOP_RD_SHORTREC_EOF,
}};
const oop_rd_style OOP_RD_STYLE_IMMED[]= {{
  OOP_RD_DELIM_NONE, 0,    OOP_RD_NUL_PERMIT, OOP_RD_SHORTREC_SOONEST,
}};

struct oop_read {
  /* set at creation time: */
  oop_source *oop;
  oop_readable *ra;
  char *userbuf;
  /* persistent state */
  oop_rd_bufctl_op readahead; /* _ENABLE or _DISABLE */
  char *allocbuf;
  size_t alloc, used, discard;
  size_t neednotcheck; /* data we've already searched for delimiter */
  int displacedchar; /* >=0, first unused */
  /* arguments to oop_rd_read */
  oop_rd_style style;
  size_t maxrecsz;
  oop_rd_call *call_ok, *call_err;
  void *data_ok, *data_err;
};

/* Buffer is structured like this if displacedchar>=0 and delim found:
 *
 *              done stuff,    displaced readahead - read     unused
 *              we've called  delimiter| but not yet          buffer
 *              back for              || returned             space
 *              ddddddddddddddddddddddDOaaaaaaaaaaaaaaaaaaa____________
 *              <------- discard ----->
 *              <----------------------- used ------------>
 *              <------------------------------------- alloc --------->
 *
 * If displacedchar>=0 then the the first character of readahead has
 * been displaced by a nul byte and is stored in displacedchar.  If
 * _DELIM_STRIP and the delimiter is found then the nul overwrites the
 * delimiter.
 *
 *               Buffer when full   {this,max}                  may need
 * DELIM found?  <-recval->      recdata  buffer required       readahead
 *  NONE  n/a    ddddddddddOaaa_ recsz    recdata+1 == recsz+1  maxrecsz
 *  KEEP  Yes    dddddddddDOaaa_ recsz    recdata+1 == recsz+1  maxrecsz
 *  KEEP  No     ddddddddddOaaa_ recsz    recdata+1 == recsz+1  maxrecsz
 *  STRIP Yes    dddddddddd0aaaa recsz+1  recdata   == recsz+1  maxrecsz+1
 *  STRIP No     ddddddddddOaaaa recsz    recdata+1 == recsz+1  maxrecsz+1
 *
 * Key:  d = data to be returned
 *       D = delimiter, being returned
 *       a = readahead, not to be returned
 *       O = readahead character displaced by a nul
 *       0 = delimiter replaced by a nul
 *       _ = unused
 */

static const char *const eventstrings_nl[]= {
  "INTERNAL ERROR (_nl _OK) please report",
  "End of file",
  "Missing newline at end of file",
  "Line too long",
  "Nul byte",
  "Nul byte, in line which is also too long",
  "INTERNAL ERROR (_nl _SYSTEM) please report"
};

static const char *const eventstrings_other[]= {
  "Record read successfully",
  "End of file",
  "Incomplete record at end of file",
  "Record too long",
  "Nul byte",
  "Nul byte in record which is also too long",
  "System error"
};

oop_read *oop_rd_new(oop_source *oop, oop_readable *ra, char *buf, size_t bufsz) {
  oop_read *rd= 0;

  assert(buf ? bufsz>=2 : !bufsz);

  rd= oop_malloc(sizeof(*rd));  if (!rd) goto x_fail;
  rd->oop= oop;
  rd->ra= ra;
  rd->userbuf= buf;
  rd->readahead= OOP_RD_BUFCTL_ENABLE;
  rd->allocbuf= 0;
  rd->used= 0;
  rd->alloc= buf ? bufsz : 0;
  rd->neednotcheck= 0;
  rd->displacedchar= -1;
  rd->style= *OOP_RD_STYLE_IMMED;

  return rd;

x_fail:
  oop_free(rd);
  return 0;
}

static int set_time_ifbuf(oop_source *oop, oop_read *rd) {
  if (rd->used > rd->discard)
    return oop->on_time(oop,OOP_TIME_NOW,on_time,rd), 0; /* fixme */
  return 0;
}
static void cancel_time(oop_source *oop, oop_read *rd) {
  oop->cancel_time(oop,OOP_TIME_NOW,on_time,rd);
}
static int set_read(oop_source *oop, oop_read *rd) {
  return rd->ra->on_readable(rd->ra,on_readable,rd), 0; /* fixme */
}
static void cancel_read(oop_source *oop, oop_read *rd) {
  rd->ra->on_cancel(rd->ra);
}

int oop_rd_read(oop_read *rd, const oop_rd_style *style, size_t maxrecsz,
		oop_rd_call *ifok, void *data_ok,
		oop_rd_call *iferr, void *data_err) {
  oop_source *oop= rd->oop;
  int er;

  cancel_time(oop,rd);
  cancel_read(oop,rd);

  if (style->delim_mode == OOP_RD_DELIM_NONE ||
      rd->style.delim_mode == OOP_RD_DELIM_NONE ||
      style->delim != rd->style.delim)
    rd->neednotcheck= 0;

  rd->style= *style;
  rd->maxrecsz= maxrecsz;
  rd->call_ok= ifok; rd->data_ok= data_ok;
  rd->call_err= iferr; rd->data_err= data_err;

  er= set_read(oop,rd);        if (er) return er;
  er= set_time_ifbuf(oop,rd);  if (er) return er;
  return 0;
}

void oop_rd_delete(oop_read *rd) {
  rd->ra->on_cancel(rd->ra);
  oop_free(rd->allocbuf);
  oop_free(rd);
}

void oop_rd_cancel(oop_read *rd) {
  cancel_time(rd->oop,rd);
  cancel_read(rd->oop,rd);
}

const char *oop_rd_errmsg(oop_read *rd, oop_rd_event event, int errnoval,
			  const oop_rd_style *style) {
  if (event == OOP_RD_SYSTEM)
    return strerror(errnoval);
  else if (style && style->delim_mode != OOP_RD_DELIM_NONE
	   && style->delim == '\n')
    return eventstrings_nl[event];
  else
    return eventstrings_other[event];
}

static void *on_readable(oop_source *oop, oop_readable *ra, void *rd_void) {
  oop_read *rd= rd_void;

  assert(oop == rd->oop);
  assert(ra == rd->ra);
  return on_process(oop,rd,1);
}

static void *on_time(oop_source *oop, struct timeval when, void *rd_void) {
  oop_read *rd= rd_void;

  assert(oop == rd->oop);
  return on_process(oop,rd,0);
}

static size_t calc_dataspace(oop_read *rd) {
  if (rd->style.delim_mode == OOP_RD_DELIM_STRIP) {
    return rd->alloc;
  } else {
    return rd->alloc ? rd->alloc-1 : 0;
  }
}

static void *on_process(oop_source *oop, oop_read *rd, int try_read) {
  oop_rd_event event;
  int evkind; /* 0=none, -1=error, 1=something */
  int errnoval, nread, cancelnow;
  oop_rd_call *call;
  char *buf, *delimp;
  const char *errmsg;
  size_t maxrecsz; /* like in arg to oop_rd_read, but 0 -> large val */
  size_t maxbufreqd; /* maximum buffer we might possibly want to alloc */
  size_t readahead; /* max amount of data we might want to readahead */
  size_t want; /* amount we want to allocate or data we want to read */
  size_t dataspace; /* amount of buffer we can usefully fill with data */
  size_t thisrecsz; /* length of the record we've found */
  size_t thisrecdata; /* length of data representing the record */
  void *call_data;

  cancel_time(oop,rd);

  if (rd->userbuf) {
    buf= rd->userbuf;
  } else {
    buf= rd->allocbuf;
  }
  
  if (rd->discard) {
    rd->used -= rd->discard;
    rd->neednotcheck -= rd->discard;
    memmove(buf, buf + rd->discard, rd->used);
    rd->discard= 0;
  }
  if (rd->displacedchar >= 0) {
    assert(rd->used > 0);
    buf[0]= rd->displacedchar;
    rd->displacedchar= -1;
  }

  maxrecsz= rd->maxrecsz ? rd->maxrecsz : INT_MAX / 5 /* allows +20 and *4 */;
  maxbufreqd= maxrecsz+1;

  if (rd->userbuf && maxbufreqd > rd->alloc) {
    maxrecsz -= (maxbufreqd - rd->alloc);
    maxbufreqd= rd->alloc;
  }

  if (rd->style.delim_mode == OOP_RD_DELIM_STRIP) {
    readahead= maxrecsz+1;
  } else {
    readahead= maxrecsz;
  }

  for (;;) {
    evkind= 0;
    event= -1;
    thisrecdata= thisrecsz= 0;
    errnoval= 0;

    assert(rd->used <= rd->alloc);
    dataspace= calc_dataspace(rd);
    
    if (/* delimiter already in buffer, within max record data ? */
	rd->style.delim_mode != OOP_RD_DELIM_NONE &&
	(delimp= memchr(buf + rd->neednotcheck, rd->style.delim,
			MIN(rd->used, readahead) - rd->neednotcheck))) {
      
      thisrecsz= (delimp - buf);
      thisrecdata= thisrecsz+1;
      if (rd->style.delim_mode == OOP_RD_DELIM_KEEP)
	thisrecsz= thisrecdata;
      event= OOP_RD_OK;
      evkind= +1;

    } else if (rd->used >= readahead) {
      
      thisrecsz= thisrecdata= maxrecsz;
      evkind= +1;

      if (rd->style.delim_mode == OOP_RD_DELIM_NONE) {
	event= OOP_RD_OK;
      } else {
	event= OOP_RD_LONG;
	if (rd->style.shortrec_mode < OOP_RD_SHORTREC_LONG) {
	  evkind= -1;
	  thisrecsz= thisrecdata= 0;
	}
      }

    } else if (/* want to return ASAP, and we have something ? */
	       rd->style.shortrec_mode == OOP_RD_SHORTREC_SOONEST &&
	       rd->used > 0 && rd->alloc >= 2) {
      
      thisrecdata= rd->used;
      if (thisrecdata == rd->alloc) thisrecdata--;
      thisrecsz= thisrecdata;
      event= OOP_RD_OK;
      evkind= +1;

    }

    want= 0;
    if (evkind && thisrecdata && thisrecsz >= rd->alloc) {
      /* Need to make space for the trailing nul */
      want= rd->alloc+1;
    } else if (!evkind && !rd->userbuf &&
	       rd->used >= dataspace && rd->alloc < maxbufreqd) {
      /* Need to make space to read more data */
      want= rd->alloc + 20;
      want <<= 2;
      want= MIN(want, maxbufreqd);
    }

    if (want) {
      assert(!rd->userbuf);
      assert(want <= maxbufreqd);

      buf= oop_realloc(rd->allocbuf,want);
      if (!buf) {
	event= OOP_RD_SYSTEM;
	evkind= -1;
	errnoval= ENOMEM;
	thisrecsz= thisrecdata= 0;
	break;
      }
      rd->allocbuf= buf;
      rd->alloc= want;
    }

    if (evkind) break; /* OK, process it then */

    if (!try_read) return OOP_CONTINUE; /* But we weren't told it was ready. */

    dataspace= calc_dataspace(rd);
    want= MIN(dataspace, readahead);
    assert(rd->used < want);

    errno= 0;
    nread= rd->ra->try_read(rd->ra, buf+rd->used, want-rd->used);
    if (errno == EAGAIN) return OOP_CONTINUE;

    if (nread > 0) {
      rd->neednotcheck= rd->used;
      rd->used += nread;
      continue;
    }

    if (nread < 0) { /* read error */

      event= OOP_RD_SYSTEM;
      evkind= -1;
      errnoval= errno;
      thisrecsz= thisrecdata= rd->used;
      break;

    } else {

      if (rd->used) {
	event= OOP_RD_PARTREC;
	evkind= (rd->style.shortrec_mode == OOP_RD_SHORTREC_FORBID) ? -1 : +1;
	thisrecsz= thisrecdata= rd->used;
      } else {
	event= OOP_RD_EOF;
	evkind= +1;
      }
      break;

    }
  }

  /* OK, we have an event of some kind */

  /* Nul byte handling */
  if (thisrecsz > 0 && rd->style.nul_mode != OOP_RD_NUL_PERMIT) {
    size_t checked;
    char *nul, *notnul;
    
    for (checked=0;
	 (nul= memchr(buf+checked,0,thisrecsz-checked));
	 ) {
      if (rd->style.nul_mode == OOP_RD_NUL_FORBID) {
	event= OOP_RD_NUL;
	evkind= -1;
	thisrecdata= thisrecsz= 0;
	break;
      }
      assert(rd->style.nul_mode == OOP_RD_NUL_DISCARD);
      for (notnul= nul+1;
	   notnul < buf+thisrecsz && notnul == '\0';
	   notnul++);
      thisrecsz-= (notnul-nul);
      checked= nul-buf;
      memmove(nul,notnul,thisrecsz-checked);
    }
  }

  /* Checks that all is well */

  assert(evkind);
  assert(thisrecsz <= thisrecdata);
  assert(!rd->maxrecsz || thisrecsz <= rd->maxrecsz);
  assert(thisrecdata <= rd->used);

  rd->discard= thisrecdata;

  cancelnow= (evkind < 0) || (event == OOP_RD_EOF);

  if (!cancelnow) {
    errnoval= set_time_ifbuf(oop,rd);
    if (errnoval) {
      event= OOP_RD_SYSTEM;
      evkind= -1;
      cancelnow= 1;
      thisrecsz= thisrecdata= 0;
      rd->discard= 0;
    }
  }

  if (evkind < 0) {
    call= rd->call_err;
    call_data= rd->data_err;
    errmsg= oop_rd_errmsg(rd,event,errnoval,&rd->style);
  } else {
    call= rd->call_ok;
    call_data= rd->data_ok;
    errmsg= 0;
  }

  if (thisrecdata) {
    /* We have to fill in a nul byte. */
    assert(thisrecsz < rd->alloc);
    if (thisrecsz == thisrecdata && thisrecsz < rd->used)
      rd->displacedchar= (unsigned char)buf[thisrecdata];
    buf[thisrecsz]= 0;
  }

  if (cancelnow)
    oop_rd_cancel(rd);

  return
    call(oop,rd, event,errmsg,errnoval,
	 (thisrecdata ? buf : 0), thisrecsz, call_data);
}

oop_read *oop_rd_new_fd(oop_source *oop, int fd, char *buf, size_t bufsz) {
  oop_readable *ra;
  oop_read *rd;

  ra= oop_readable_fd(oop,fd);
  if (!ra) return 0;

  rd= oop_rd_new(oop,ra,buf,bufsz);
  if (!rd) { ra->delete_tidy(ra); return 0; }

  return rd;
}

int oop_rd_delete_tidy(oop_read *rd) {
  oop_readable *ra= rd->ra;
  oop_rd_delete(rd);
  return ra->delete_tidy(ra);
}

void oop_rd_delete_kill(oop_read *rd) {
  oop_readable *ra= rd->ra;
  oop_rd_delete(rd);
  ra->delete_kill(ra);
}  
