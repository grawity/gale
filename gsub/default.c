/* default.c -- default gsubrc implementation. */

#include "default.h"

#include "gale/all.h"
#include "gale/gsubrc.h"

#include <ctype.h>

/* The Wacky Quoting Proposal (WQP), as implemented:
   For any line of text, the Quote is /^([ >]*|\|[^>])/.  The Quote is repeated
   at the beginning of all wrap lines.  If the Quote is longer than the screen,
   then it is no longer considered the Quote. */
void default_gsubrc(int do_beep) {
	char *buf, *quotebuf;

	struct gale_text timecode,text,cat = gale_var(G_("GALE_CATEGORY"));
	struct gale_text presence = gale_var(G_("GALE_TEXT_NOTICE_PRESENCE"));
	struct gale_text answer = gale_var(G_("GALE_TEXT_ANSWER_RECEIPT"));
	struct gale_text from_name = gale_var(G_("GALE_TEXT_MESSAGE_SENDER"));
	struct gale_text subject = gale_var(G_("GALE_TEXT_MESSAGE_SUBJECT"));
	int len,buflen,bufloaded = 0,termwid = gale_columns(stdout);

	if (termwid < 2) termwid = 80; /* Don't crash */

	/* Ignore messages to category /ping */
	text = null_text;
	while (gale_text_token(cat,':',&text)) {
		if (!gale_text_compare(G_("/ping"),
			gale_text_right(text,5)))
			return;
	}

	/* Get the time */
	if (0 == (timecode = gale_var(G_("GALE_TIME_ID_TIME"))).l) {
		char tstr[80];
		time_t when = time(NULL);
		strftime(tstr,sizeof(tstr),"%Y-%m-%d %H:%M:%S",localtime(&when));
		timecode = gale_text_from_local(tstr,-1);
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
			gale_print(stdout,0,G_(" ["));
			gale_print(stdout,0,subject);
			gale_print(stdout,0,G_("]"));
		}
		print_id(gale_var(G_("GALE_SIGNED")),G_("unverified"));
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

	/* Print the header: category, et cetera */
	{
		struct gale_text subcat = null_text;
		gale_print(stdout,gale_print_clobber_left,G_("["));
		while (gale_text_token(cat,':',&subcat)) {
			if (subcat.p != cat.p) gale_print(stdout,0,G_(":"));
			gale_print(stdout,gale_print_bold,subcat);
		}
		gale_print(stdout,0,G_("]"));
		len += 2 + cat.l;
	}

	if (subject.l) {
		gale_print(stdout,0,G_(" ["));
		gale_print(stdout,gale_print_bold,subject);
		gale_print(stdout,0,G_("]"));
		len += 3 + subject.l;
	}

	text = gale_var(G_("GALE_TEXT_MESSAGE_SENDER"));
	if (text.l) {
		gale_print(stdout,0,G_(" from "));
		gale_print(stdout,gale_print_bold,text);
		len += G_(" from ").l + text.l;
	}

	text = gale_var(G_("GALE_TEXT_MESSAGE_RECIPIENT"));
	if (text.l) {
		gale_print(stdout,0,G_(" to "));
		gale_print(stdout,gale_print_bold,text);
		len += G_(" to ").l + text.l;
	}

	if (gale_var(G_("GALE_TEXT_QUESTION_RECEIPT")).l) {
		gale_print(stdout,gale_print_clobber_right,G_(" [rcpt]"));
		len += G_(" [rcpt]").l;
	}

	gale_print(stdout,gale_print_clobber_right,G_(""));
	gale_print(stdout,0,G_("\n"));

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
										 gale_text_from_local(buf, quotelen));
					do {
						for (; pos < bufloaded && !isspace(buf[pos]); ++pos)
							col = gale_column(col, buf[pos]);
						gale_print(stdout, 0, 
						           gale_text_from_local(buf + quotelen, pos - quotelen));

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
					gale_print(stdout, 0, gale_text_from_local(buf, prevend));
					goto done_premie;
				}

				/* Advance past whitespace. */
				for (; pos < bufloaded && isspace(buf[pos]); ++pos) {
					col = gale_column(col, buf[pos]);
					if ('\n' == buf[pos] || col >= termwid) {
						/* Wrap line! */
						gale_print(stdout, 0, gale_text_from_local(buf, prevend));
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
					gale_print(stdout, 0, gale_text_from_local(buf, prevend));
					goto done_premie;
				}

				/* Process next word. */
				curstart = pos;
				while (pos < bufloaded && !isspace(buf[pos])) {
					col = gale_column(col, buf[pos++]);
					if (col >= termwid) {
						/* Wrap line! */
						gale_print(stdout, 0, gale_text_from_local(buf, prevend));
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
		end_line:
	}
	gale_print(stdout, gale_print_clobber_right, 
	           gale_text_from_local(buf, buflen));
	done_premie:
	/* We must have got here via premature EOF, so add a newline. */
	gale_print(stdout,0,G_("\n"));
	done_proper:

	/* Print the signature information. */
	{
		struct gale_text from_id = gale_var(G_("GALE_SIGNED"));
		struct gale_text to_id = gale_var(G_("GALE_ENCRYPTED"));
		struct gale_text from_name = 
			gale_var(G_("GALE_TEXT_MESSAGE_SENDER"));
		int len = 0;

		if (from_id.l) len += from_id.l;
		else if (from_name.l) len += G_("unverified").l;
		else len += G_("anonymous").l;

		if (to_id.l) len += to_id.l;
		else len += G_("everyone").l;

		while (len++ < termwid - 34) gale_print(stdout,0,G_(" "));

		gale_print(stdout,0,G_("--"));
		if (from_name.l)
			print_id(from_id,G_("unverified"));
		else
			print_id(null_text,G_("anonymous"));

		gale_print(stdout,0,G_(" for"));
		print_id(to_id,G_("everyone"));

		gale_print(stdout,0,G_(" at "));
		gale_print(stdout,gale_print_clobber_right,
			gale_text_right(timecode,-5));
		gale_print(stdout,0,G_(" --"));
		gale_print(stdout,gale_print_clobber_right,G_("\n"));

		if (to_id.l && do_beep) gale_beep(stdout);
	}

	fflush(stdout);
}
