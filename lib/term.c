#include "gale/misc.h"
#include "gale/config.h"

#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/ioctl.h>

#ifdef HAVE_CURSES_H
#define HAVE_CURSES
#include <curses.h>
#ifdef HAVE_TERM_H
#include <term.h>
#endif
#endif

/* This needs to be here for Solaris. */
#include <termios.h>

static FILE *out_fp = NULL;
static int term_cols = 0;

static void output(int ch) {
	fputc(ch,out_fp);
}

static void tmode(FILE *fp,char id[2]) {
#ifdef HAVE_CURSES
	static int init = 0;
	char stupid[2048];
	char *cap,*stupid_pointer;

	if (!init) {
		static char buf[1024];
		char *term = getenv("TERM");
		if (term && tgetent(buf,term) == 1) {
			init = 1;
			term_cols = tgetnum("co");
		} else
			init = -1;
	}

	stupid_pointer = stupid;
	assert(NULL == out_fp);
	out_fp = fp;
	if (init > 0 && isatty(1) && (cap = tgetstr(id,&stupid_pointer)))
		tputs(cap,1,(TPUTS_ARG_3_T) output);
	assert(fp == out_fp);
	out_fp = NULL;
#else
	(void) id;
#endif
}

static int okay(wch ch) {
	return (ch >= 32 && ch < 256) || ch == '\t';
}

int gale_column(int col,wch ch) {
	switch (ch) {
	case '\t':
		return (1 + col / 8) * 8;
	case '\n':
		return 0;
	default:
		if (okay(ch)) return col + 1;
		if (ch < 32 && ch >= 0) return col + 2;
		return col + 7;
	}
}

void gale_print(FILE *fp,int attr,struct gale_text str) {
	struct gale_text each = null_text;
	int num = 0,tty = isatty(fileno(fp));
	while (gale_text_token(str,'\n',&each)) {
		struct gale_text line = each;
		if (num++) 
			fputs(tty ? "\r\n" : "\n",fp);
		else if (tty && (attr & gale_print_clobber_left)) 
			fputc('\r',fp);
		if (attr & gale_print_bold) tmode(fp,"md");
		while (line.l > 0) {
			size_t p;
			for (p = 0; p < line.l && okay(line.p[p]); ++p) ;
			fputs(gale_text_to_local(gale_text_left(line,p)),fp);
			if (p > 0) line = gale_text_right(line,-p);
			while (line.l > 0 && !okay(line.p[0])) {
				tmode(fp,"mr");
				if (line.p[0] < 32 && line.p[0] >= 0)
					fprintf(fp,"^%c",(char) line.p[0] + 64);
				else
					fprintf(fp,"[0x%04X]",line.p[0]);
				tmode(fp,"me");
				if (attr & gale_print_bold) tmode(fp,"md");
				line = gale_text_right(line,-1);
			}
		}
		if (attr & gale_print_clobber_right) tmode(fp,"el");
		if (attr & gale_print_bold) tmode(fp,"me");
	}
}

void gale_beep(FILE *fp) {
	if (isatty(fileno(fp))) {
		fputc('\a',fp);
		fflush(fp);
	}
}

int gale_columns(FILE *fp) {
	int f = fileno(fp);
	if (!isatty(f)) return 80;
#ifdef TIOCGWINSZ
	{
		struct winsize ws;
		if (!ioctl(f,TIOCGWINSZ,&ws) && 0 < ws.ws_col)
			return ws.ws_col;
	}
#endif
	{
		char *cols = getenv("COLUMNS");
		if (cols && atoi(cols)) return atoi(cols);
	}

	if (term_cols > 0) return term_cols;
	return 80;
}
