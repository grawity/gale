#include "gale/misc.h"
#include "gale/globals.h"

#include <stdio.h>
#include <stdlib.h>

#if defined(HAVE_READLINE_READLINE_H) && defined(HAVE_LIBREADLINE)
#define HAVE_READLINE 1
#include "readline/readline.h"
#endif

#include <unistd.h>

/** Read a line of text from a file descriptor, using readline if appropriate.
 *  \param fp File to read from (often stdin).
 *  \return A line of text, including the newline; or null_text at EOF. */
struct gale_text gale_read_line(FILE *fp) {
	/* TODO: This function should be asynchronous? */
	struct gale_text_accumulator accum;
	struct gale_encoding *encoding = NULL == gale_global ? NULL
		: (stdin == fp) 
		? gale_global->enc_console
		: gale_global->enc_filesys;
	char buf[4096];
	int len;

#ifdef HAVE_READLINE
	if (stdin == fp && isatty(0)) {
		static int do_init = 1;
		struct gale_text ret;
		char *line;
		if (do_init) {
			rl_initialize();
			rl_bind_key('\t',rl_insert);
			rl_bind_key(
				'R' - '@',
				rl_named_function("redraw-current-line"));
			do_init = 0;
		}

		line = readline("");
		if (NULL == line) return null_text;

		ret = gale_text_from(encoding,line,-1);
		free(line);
		return gale_text_concat(2,ret,G_("\n"));
	}
#endif

	len = 1;
	buf[len - 1] = 'x';
	accum = null_accumulator;
	while (buf[len - 1] != '\n' && NULL != fgets(buf,sizeof(buf),fp)) {
		len = strlen(buf);
		gale_text_accumulate(&accum,gale_text_from(encoding,buf,len));
	}

	return gale_text_collect(&accum);
}
