/* default.c -- default gsubrc implementation. */

#include "default.h"
#include "gale/all.h"
#include "gale/gsubrc.h"

#include <ctype.h>
#include <stdlib.h> /* TODO */

/* Print a user ID, with a default string (like "everyone") for NULL. */
static void print_id(struct gale_text id,struct gale_text dfl) {
	struct gale_text tok = null_text;
	int first = 1;

	if (0 == id.l) {
		gale_print(stdout,0,G_(" *"));
		gale_print(stdout,gale_print_bold,dfl);
		gale_print(stdout,0,G_("*"));
		return;
	}

	while (gale_text_token(id,',',&tok)) {
                int at;
		if (first) first = 0; else gale_print(stdout,0,G_(","));
		gale_print(stdout,0,G_(" <"));
                for (at = 0; at < tok.l && '@' != tok.p[at]; ++at) ;
		gale_print(stdout,gale_print_bold,gale_text_left(tok,at));
		gale_print(stdout,0,gale_text_right(tok,-at));
		gale_print(stdout,0,G_(">"));
	}
}

static int id_width(struct gale_text id,struct gale_text dfl) {
	struct gale_text tok = null_text;
	int len = 0;
	if (0 == id.l) return 3 + dfl.l;
	while (gale_text_token(id,',',&tok))
		if (0 == len) len += 3 + tok.l; else len += 4 + tok.l;
	return len;
}

/* The Wacky Quoting Proposal (WQP), as implemented:
   For any line of text, the Quote is /^([ >]*|\|[^>])/.  The Quote is repeated
   at the beginning of all wrap lines.  If the Quote is longer than the screen,
   then it is no longer considered the Quote. */
void default_gsubrc(void) {
	char *buf, *quotebuf;
	struct gale_text timecode,text;
	struct gale_text presence = gale_var(G_("GALE_TEXT_NOTICE_PRESENCE"));
	struct gale_text answer = gale_var(G_("GALE_TEXT_ANSWER_RECEIPT"));
	struct gale_text from_name = gale_var(G_("GALE_TEXT_MESSAGE_SENDER"));
	struct gale_text subject = gale_var(G_("GALE_TEXT_MESSAGE_SUBJECT"));
	int len,buflen,bufloaded = 0,termwid = gale_columns(stdout);

	if (termwid < 2) termwid = 80; /* Don't crash */

	/* Get the time */
	if (0 == (timecode = gale_var(G_("GALE_TIME_ID_TIME"))).l) {
		char tstr[80];
		time_t when = time(NULL);
		strftime(tstr,sizeof(tstr),"%Y-%m-%d %H:%M:%S",localtime(&when));
		timecode = gale_text_from(NULL,tstr,-1);
	}

	if (0 != gale_var(G_("GALE_DATA_SECURITY_ENCRYPTION")).l) {
		gale_alert(GALE_WARNING,G_("decryption error"),0);
		return;
	}

	/* Format return receipts and presence notices specially */
	if (0 != answer.l || 0 != presence.l) {
		gale_print(stdout,
		gale_print_bold | gale_print_clobber_left,G_("* "));
		gale_print(stdout,0,timecode);

		if (answer.l) gale_print(stdout,0,G_(" received:"));
		if (presence.l) {
			gale_print(stdout,0,G_(" "));
			gale_print(stdout,0,presence);
		}
		if (subject.l) {
			gale_print(stdout,0,G_(" \""));
			gale_print(stdout,0,subject);
			gale_print(stdout,0,G_("\""));
		}

		print_id(gale_var(G_("GALE_FROM")),G_("unverified"));
		if (from_name.l) {
			gale_print(stdout,0,G_(" ("));
			gale_print(stdout,0,from_name);
			gale_print(stdout,0,G_(")"));
		}
		gale_print(stdout,gale_print_clobber_right,G_(""));
		gale_print(stdout,0,G_("\n"));
		fflush(stdout);
		return;
	}

	gale_print(stdout,gale_print_clobber_left,G_("-"));
	for (len = 0; len < termwid - 3; ++len) gale_print(stdout,0,G_("-"));
	gale_print(stdout,gale_print_clobber_right,G_("-"));
	gale_print(stdout,0,G_("\n"));

	/* Print the header */

	gale_print(stdout,0,G_("To"));
	text = gale_var(G_("GALE_TEXT_MESSAGE_RECIPIENT"));
	if (text.l) {
		gale_print(stdout,0,G_(" "));
		gale_print(stdout,0,text);
	}

	print_id(gale_var(G_("GALE_TO")),G_("unknown"));

	if (subject.l) {
		gale_print(stdout,0,G_(" re \""));
		gale_print(stdout,gale_print_bold,subject);
		gale_print(stdout,0,G_("\""));
	}

	if (gale_var(G_("GALE_TEXT_QUESTION_RECEIPT")).l) {
		gale_print(stdout,gale_print_clobber_right,G_(" [rcpt]"));
	}

	gale_print(stdout,gale_print_clobber_right,G_(":\n"));

	/* Print the message body. */
	buflen = termwid; /* can't be longer than this */
	buf = gale_malloc(buflen);
	quotebuf = gale_malloc(buflen);

	while (1) {
		int quotelen = 0, quotecol = 0;
		char curchar;
		/* Read more data in order to process a line of input: */
		bufloaded += fread(buf + bufloaded, 1, buflen - bufloaded, stdin);
		if (!bufloaded && feof(stdin)) goto done_proper;

		/* Find the Quote. */
		while (1) {
			if (quotelen == bufloaded || quotecol >= termwid) {
				/* This Quote is too long - give up and format as regular text. */
				quotelen = quotecol = 0;
				goto end_quote;
			}
			curchar = buf[quotelen];
			if (('\n' != curchar && isspace(curchar)) || '>' == curchar) {
				quotecol = gale_column(quotecol, curchar); ++quotelen;
			} else if ('|' == curchar) {
				++quotecol; ++quotelen;
				for (; quotelen < bufloaded && '>' != buf[quotelen]; ++quotelen)
					quotecol = gale_column(quotecol, buf[quotelen]);
			} else
				goto end_quote;
		}
		end_quote:

		/* Process rest of the line. */
		while (1) {
			/* Produce a line of output. */
			int pos = quotelen; /* current position */
			int col = quotecol; /* current screen column */
			int prevend = pos; /* end of previous word */
			int curstart = pos; /* start of current word */

			/* Advance past end of first word. */
			for (; pos < bufloaded && !isspace(buf[pos]); ++pos) {
				col = gale_column(col, buf[pos]);
				if (col >= termwid) {
					/* Extra long word!  Output it verbatim. */
					gale_print(stdout, gale_print_clobber_right, 
						 gale_text_from(gale_global->enc_console, buf, quotelen));
					do {
						for (; pos < bufloaded && !isspace(buf[pos]); ++pos)
							col = gale_column(col, buf[pos]);
						gale_print(stdout, 0, 
						           gale_text_from(gale_global->enc_console, buf + quotelen, pos - quotelen));

						/* Read more, if necessary. */
						if (pos == bufloaded) {
							pos = bufloaded = quotelen;
							bufloaded += fread(buf + bufloaded, 1, buflen - bufloaded, stdin);
							if (pos == bufloaded && feof(stdin)) goto done_premie;
						}
					} while (!isspace(buf[pos]));
					
					/* Skip whitespace. */
					for (; '\n' != buf[pos] && isspace(buf[pos]);)
						if (++pos == bufloaded) {
							pos = bufloaded = quotelen;
							bufloaded += fread(buf + bufloaded, 1, buflen - bufloaded, stdin);
							if (pos == bufloaded && feof(stdin)) goto done_premie;
						}
					gale_print(stdout,0,G_("\n"));
					if ('\n' == buf[pos]) {
						++pos; 
						memmove(buf, buf + pos, bufloaded - pos);
						bufloaded -= pos;
						goto end_line;
					} else {
						memmove(buf + quotelen, buf + pos, bufloaded - pos);
						bufloaded -= pos - quotelen;
						goto end_out_line;
					}
				}
			}

			/* Process remaining words.*/
			while (1) {
				prevend = pos;

				/* Have we reached premature EOF? */
				if (pos == bufloaded) {
					gale_print(stdout, 0, gale_text_from(gale_global->enc_console, buf, prevend));
					goto done_premie;
				}

				/* Advance past whitespace. */
				for (; pos < bufloaded && isspace(buf[pos]); ++pos) {
					col = gale_column(col, buf[pos]);
					if ('\n' == buf[pos] || col >= termwid) {
						/* Wrap line! */
						gale_print(stdout, 0, gale_text_from(gale_global->enc_console, buf, prevend));
						gale_print(stdout,0,G_("\n"));
						/* Skip any more whitespace. */
						for (; '\n' != buf[pos] && isspace(buf[pos]);)
							if (++pos == bufloaded) {
								pos = bufloaded = quotelen;
								bufloaded += fread(buf + bufloaded,1,buflen - bufloaded,stdin);
								if (pos == bufloaded && feof(stdin)) goto done_premie;
							}
						if ('\n' == buf[pos]) {
							++pos; 
							memmove(buf, buf + pos, bufloaded - pos);
							bufloaded -= pos;
							goto end_line;
						} else {
							memmove(buf + quotelen, buf + pos, bufloaded - pos);
							bufloaded -= pos - quotelen;
							goto end_out_line;
						}
					}
				}

				/* Have we reached premature EOF? */
				if (pos == bufloaded) {
					gale_print(stdout, 0, gale_text_from(gale_global->enc_console, buf, prevend));
					goto done_premie;
				}

				/* Process next word. */
				curstart = pos;
				while (pos < bufloaded && !isspace(buf[pos])) {
					col = gale_column(col, buf[pos++]);
					if (col >= termwid) {
						/* Wrap line! */
						gale_print(stdout, 0, gale_text_from(gale_global->enc_console, buf, prevend));
						gale_print(stdout,0,G_("\n"));
						memmove(buf + quotelen, buf + curstart, bufloaded - curstart);
						bufloaded -= curstart - quotelen;
						goto end_out_line;
					}
				}
			}

			end_out_line:
			bufloaded += fread(buf + bufloaded, 1, buflen - bufloaded, stdin);
			if (!bufloaded && feof(stdin)) goto done_proper;
		}
		end_line: ;
	}
	gale_print(stdout, gale_print_clobber_right, 
	           gale_text_from(gale_global->enc_console, buf, buflen));
	done_premie:
	/* We must have got here via premature EOF, so add a newline. */
	gale_print(stdout,0,G_("\n"));
	done_proper:

	/* Print the signature information. */
	{
		struct gale_text from_id = gale_var(G_("GALE_FROM"));
		struct gale_text from_name = 
			gale_var(G_("GALE_TEXT_MESSAGE_SENDER"));
		int len = 0;

		if (0 == from_name.l)
			len += id_width(from_id,G_("anonymous"));
		else
			len += id_width(from_id,G_("unverified")) + from_name.l + 1;
		while (len++ < termwid - 24) gale_print(stdout,0,G_(" "));

		gale_print(stdout,0,G_("--"));
		if (0 == from_name.l)
			print_id(from_id,G_("anonymous"));
		else {
			gale_print(stdout,0,G_(" "));
			gale_print(stdout,0,from_name);
			print_id(from_id,G_("unverified"));
		}

		gale_print(stdout,0,G_(" at "));
		gale_print(stdout,gale_print_clobber_right,
			gale_text_right(timecode,-5));
		gale_print(stdout,0,G_(" --"));
		gale_print(stdout,gale_print_clobber_right,G_("\n"));
	}

	fflush(stdout);
}
