#include "gale/misc.h"
#include "gale/config.h"
#include "gale/globals.h"

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
	(void) fp;
	(void) id;
#endif
}

static int okay(wch ch) {
	return (ch >= 32 || ch == '\t');
}

int gale_column(int col,wch ch) {
	struct gale_text t;
	switch (ch) {
	case '\t':
		return 8*(1 + (col / 8));
	case '\n':
	case '\r':
		col = 0;
	default:
		if (ch < 32) {
			ch += 64;
			++col;
		}
		t.p = &ch;
		t.l = 1;
		return col + gale_width(t);
	}
}

int gale_width(struct gale_text str) {
	const char *cvt = gale_text_to(gale_global->enc_console,str);
	struct gale_text out = gale_text_from(gale_global->enc_console,cvt,-1);
	const wch *ptr = out.p,*end = out.l + out.p;
	int count = 0;
	for (; end != ptr; ++ptr)
		switch (wcwidth(*ptr)) {
		case -1:
		case 0: break;
		case 2: ++count;
		case 1: ++count;
			break;
		}

	return count;
}

static void rawout(FILE *fp,struct gale_text str) {
	fputs(gale_text_to(gale_global->enc_console,str),fp);
}

void gale_print(FILE *fp,int attr,struct gale_text str) {
	struct gale_text each = null_text;
	int num = 0,tty = isatty(fileno(fp));
	while (gale_text_token(str,'\n',&each)) {
		struct gale_text line = each;
		if (num++) 
			rawout(fp,tty ? G_("\r\n") : G_("\n"));
		else if (tty && (attr & gale_print_clobber_left)) 
			rawout(fp,G_("\r"));
		if (attr & gale_print_bold) tmode(fp,"md");
		while (line.l > 0) {
			size_t p;
			for (p = 0; p < line.l && okay(line.p[p]); ++p) ;
			rawout(fp,gale_text_left(line,p));
			if (p > 0) line = gale_text_right(line,-p);
			while (line.l > 0 && !okay(line.p[0])) {
				wch escape[2];
				struct gale_text t;
				tmode(fp,"mr");
				escape[0] = '^';
				escape[1] = 64 + line.p[0];
				t.p = escape; t.l = 2;
				rawout(fp,t);
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
		rawout(fp,G_("\a"));
		fflush(fp);
	}
}

int gale_columns(FILE *fp) {
	int f = fileno(fp);

	/* $GALE_COLUMNS takes precedence. */
	{
		struct gale_text cols = gale_var(G_("GALE_COLUMNS"));
		int num = gale_text_to_number(cols);
		if (0 != num) return num;
	}

	/* Other methods rely on a terminal; no terminal => 80 columns. */
	if (!isatty(f)) return 80;

	/* The most reliable method is to use TTY window-size information. */
#ifdef TIOCGWINSZ
	{
		struct winsize ws;
		if (!ioctl(f,TIOCGWINSZ,&ws) && 0 < ws.ws_col)
			return ws.ws_col;
	}
#endif

	/* Otherwise, maybe there's an environment variable? */
	{
		struct gale_text cols = gale_var(G_("COLUMNS"));
		int num = gale_text_to_number(cols);
		if (0 != num) return num;
	}

	/* Finally, use the static information in the termcap entry, if any. */
	if (term_cols > 0) return term_cols;

	/* If all else fails, default to 80 columns. */
	return 80;
}
