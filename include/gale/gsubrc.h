/** \file
 * API for loadable gsub modules.
 * To use either of these interfaces, create a shared library exporting the
   correct function and place it in the same place as a standard "gsubrc", 
   but name it "gsubrc.so".  The gsub will dynamically load the library and
   call the function as appropriate. */

#ifndef GALE_GSUBRC_H
#define GALE_GSUBRC_H

/* -- v 0.14: extended interface ------------------------------------------- */
/** \name v0.14
 * Extended 'nonforking' interface.
 * To use this interface, export a function with this signature.
 * It will take precedence over the previous interface.
 *
 * This is a somewhat more sophisticated hook.  The gsub will not fork before
 * calling this function, so you can easily keep persistent state, but you must
 * also take care to avoid damaging the process or leaking resources.
 *
 * The function should not exit (since it runs in the same process as gsub). */
/*@{*/
/** Signature of the function you should write.
 *  \param env The environment that would be passed to a regular gsubrc, 
 *  as a NULL-terminated array of string pointers of the form "VAR=VALUE".
 *  This includes the system environment as well as GALE_* and HEADER_*
 *  variables.
 *  \param msg The message body text.  This is not NUL-terminated!
 *  \param len The length of the message body text. 
 *  \return Nonzero (failure) to suppress return receipts. */
typedef int (gsubrc2_t)(char * const * const env,const char *msg,int len);
/** Call your function 'gsubrc2'. */
gsubrc2_t gsubrc2;
/*@}*/

/** \name v0.13b 
 * Simple 'forking' interface.
 * To use this interface, export a function with the this signature.
 *
 * Rather than looking for an external "gsubrc", gsub will simply call the
 * following function under the same conditions.  Information is communicated
 * with environment variables and standard input, just as with gsubrc.
 *
 * To support this, gsub forks once per message.  You therefore do not need to
 * clean up after yourself in this function, but you also cannot keep track of
 * persistent state without using the filesystem or another external mechanism.
 *
 * The function can return status directly, or call exit() with status value.  
 * Nonzero status will cause gsub to suppress any return receipts. */
/*@{*/
/** Signature of the function you should write. */
typedef int (gsubrc_t)(void);
/** Call your function 'gsubrc'. */
gsubrc_t gsubrc;
/*@}*/

#endif
