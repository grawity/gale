#include "gale/misc.h"

#include <stdarg.h>
#include <assert.h>
#include <stdlib.h>

const struct gale_text null_text = { NULL, 0 };
const struct gale_text_accumulator null_accumulator = { 0, { } };

/** Concatenate text strings.
 *  The first argument \a count is the number of strings passed.
 *  \param count The number of strings to concatenate.
 *  \return The concatenated string.
 *  \sa gale_text_concat_array()
 *  \code
 *  struct gale_text stuff = G_("hi hi");
 *  struct gale_text foobar = gale_text_concat(3,G_("foo ["),stuff,G_("] bar"));
 *  assert(0 == gale_text_compare(foobar,G_("foo [hi hi] bar")));
 *  \endcode */
struct gale_text gale_text_concat(int count,...) {
	size_t len = 0,alloc = 30;
	wch *buffer = gale_malloc(alloc * sizeof(*buffer));
	struct gale_text text;
	va_list ap;

	va_start(ap,count);
	while (count--) {
		text = va_arg(ap,struct gale_text);
		if (text.l + len > alloc) {
			alloc = (text.l + len) * 2;
			buffer = gale_realloc(buffer,alloc * sizeof(*buffer));
		}
		memcpy(buffer + len,text.p,text.l * sizeof(*buffer));
		len += text.l;
	}
	va_end(ap);

	text.p = buffer;
	text.l = len;
	return text;
}

/** Concatenate an array of text strings.
 *  \param count The number of members in \a array.
 *  \param array The strings to concatenate, in order.
 *  \return The concatenated string.
 *  \sa gale_text_concat() */
struct gale_text gale_text_concat_array(int count,struct gale_text *array) {
	struct gale_text ret;
	wch *buffer;
	int i;

	/* first, count */
	ret.l = 0;
	for (i = 0; i < count; ++i) ret.l += array[i].l;
	gale_create_array(buffer,ret.l);
	ret.p = buffer;

	/* then, copy */
	for (i = 0; i < count; ++i) {
		memcpy(buffer,array[i].p,array[i].l * sizeof(*buffer));
		buffer += array[i].l;
	}

	return ret;
}

/** Append text to an accumulator.
 *  \param accum Accumulator to append to.
 *  \param text Text to append to the accumulator. */
void gale_text_accumulate(
	struct gale_text_accumulator *accum,
	struct gale_text text) 
{
	if (accum->count == sizeof(accum->array) / sizeof(accum->array[0]))
		gale_text_collect(accum);

	assert(accum->count < sizeof(accum->array) / sizeof(accum->array[0]));
	accum->array[accum->count++] = text;
}

/** Return nonzero if an accumulator contains any text.
 *  \param accum Accumulator to test.
 *  \return Nonzero if the accumulator has any text in it. */
int gale_text_accumulator_empty(const struct gale_text_accumulator *accum) {
	int i;
	for (i = 0; i  < accum->count; ++i)
		if (0 != accum->array[i].l) return 0;
	return 1;
}

/** Return the text collected in an accumulator.
 *  \param accum Accumulator to collect text from. 
 *  \return All the text added to the accumulator. */
struct gale_text gale_text_collect(const struct gale_text_accumulator *accum) {
	struct gale_text_accumulator *mutate = 
		(struct gale_text_accumulator *) accum;
	mutate->array[0] = gale_text_concat_array(mutate->count,mutate->array);
	mutate->count = 1;
	return mutate->array[0];
}

struct gale_text _gale_text_literal(const wchar_t *sz,size_t len) {
	struct gale_text text;
	assert(sizeof(wchar_t) == sizeof(wch));
	text.p = (wch *) sz;
	text.l = len;
	return text;
}

/** Extract the leftmost substring of \a len characters.
 *  If \a len is larger than the length of the string, the entire string
 *  is returned.  If \a len is negative, all but the rightmost \a -len
 *  characters are returned.  If \a -len is larger than the length of the
 *  string, the empty string is returned.
 *  \param str The string to extract from.
 *  \param len The number of characters to extract.
 *  \return The leftmost \a len characters from \a str. */
struct gale_text gale_text_left(struct gale_text text,int i) {
	if (i < 0) {
		if ((size_t) -i > text.l)
			text.l = 0;
		else
			text.l += i;
	} else if ((size_t) i < text.l)
		text.l = i;
	return text;
}

/** Extract the rightmost substring of \a len characters.
 *  If \a len is larger than the length of the string, the entire string
 *  is returned.  If \a len is negative, all but the leftmost \a -len
 *  characters are returned.  If \a -len is larger than the length of the
 *  string, the empty string is returned.
 *  \param str The string to extract from.
 *  \param len The number of characters to extract.
 *  \return The rightmost \a len characters from \a str. */
struct gale_text gale_text_right(struct gale_text text,int i) {
	if (i < 0) {
		if ((size_t) -i > text.l) {
			text.p += text.l;
			text.l -= text.l;
		} else {
			text.p -= i;
			text.l += i;
		}
	} else if ((size_t) i < text.l) {
		text.p += text.l - i;
		text.l -= text.l - i;
	}
	return text;
}

/** Divide a string into tokens using the separator character 'sep'.
 *  Set \a token to null_text initially, then call
 *  gale_text_token(str,sep,&token) repeatedly.  The function will return a
 *  nonzero value as long as there is an additional token, and set \a token
 *  to the contents of that token.  A string with N occurrences of the
 *  separator character contains 1+N tokens.
 *
 *  This example will output 'foo', 'bar', and 'bat':
 *  \code
 *  struct gale_text str = G_("foo bar bat"),token = null_text;
 *  while (gale_text_token(str,' ',&token)) gale_print_line(stdout,0,token);
 *  \endcode
 *  \param string The string to tokenize.
 *  \param sep The separator character to use.
 *  \param token Pointer to the variable to receive the next token.
 *  \return Nonzero if a token was returned, or zero if there are no more. */
int gale_text_token(struct gale_text string,wch sep,struct gale_text *token) {
	if (NULL == string.p) {
		assert(0 == string.l);
		string.p = (wch *) 0xdeadbabe;
	}

	if (token->p < string.p || token->p > string.p + string.l) {
		/* null_text token => start iteration. */
		assert(NULL == token->p && 0 == token->l);
		token->p = string.p - 1;
		token->l = 0;
	} else if (token->p + token->l >= string.p + string.l) {
		/* Last token => done iterating. */
		*token = null_text;
		return 0;
	}

	/* Skip the seperator and find the next token. */
	token->p += token->l + 1;
	token->l = 0; 
	while (token->p + token->l != string.p + string.l
	&&     token->p[token->l] != sep)
		++token->l;
	return 1;
}

/** Replace one substring with another, everywhere it appears.
 *  \param original The original string to process.
 *  \param find The string to search for in \a original.
 *  \param replace The string to replace occurrences of \a find with.
 *  \return The value of \a original with \a replace substituted for \a find. */
struct gale_text gale_text_replace(
	struct gale_text original,
	struct gale_text find,
	struct gale_text replace)
{
	struct gale_text_accumulator accum = null_accumulator;
	int i,j;

	if (0 == find.l) return original; /* yuck */

	for (i = 0; i + find.l <= original.l; ++i) {
		for (j = 0; j < find.l; ++j)
			if (original.p[i + j] != find.p[j]) 
				break;
		if (j != find.l) 
			continue;

		gale_text_accumulate(&accum,gale_text_left(original,i));
		gale_text_accumulate(&accum,replace);
		original = gale_text_right(original,-(i + j));
		i = -1;
	}

	gale_text_accumulate(&accum,original);
	return gale_text_collect(&accum);
}

/** Compare two strings, a la strcmp().
 *  \return Less than zero if \a a \< \a b, zero if \a a == \a b, or
 *  greater than zero if \a a \> \a b. */
int gale_text_compare(struct gale_text a,struct gale_text b) {
	size_t l = (a.l < b.l) ? a.l : b.l;
	int c = (a.p == b.p) ? 0 : memcmp(a.p,b.p,l * sizeof(wch));
	if (0 != c) return c;
	return a.l - b.l;
}

/** Create a text representation of a number.
 *  \param n The number to format.
 *  \param base The numeric base to use (0 < \a base <= 36).
 *  \param pad The minimum field width.  Zeroes will be added if necessary.
 *  \return The formatted representation of this number.
 *  \sa gale_text_to_number() */
struct gale_text gale_text_from_number(int n,int base,int pad) {
	wch *buf;
	struct gale_text text;
	int t = n,width = (t < 0) ? 1 : 0;

	do {
		t /= base;
		++width;
	} while (0 != t);
	if (pad > width) width = pad;
	if (-pad > width) width = -pad;

	buf = gale_malloc(width * sizeof(*buf));
	text.p = buf;
	text.l = width;

	t = (n < 0) ? -n : n;
	do {
		int digit = t % base;
		buf[--width] = "0123456789abcdefghijklmnopqrstuvwxyz"[digit];
		t /= base;
	} while (0 != t);

	if (pad < 0) {
		while (width > 1) buf[--width] = '0';
		if (n < 0) buf[--width] = '-';
		if (width > 0) buf[--width] = '0';
	} else {
		if (n < 0) buf[--width] = '-';
		while (width > 0) buf[--width] = ' ';
	}

	return text;
}

/** Parse a number.
 *  \param text The string to parse.
 *  \return The number in \a text, or zero if unsuccessful.
 *  \sa gale_text_from_number() */
int gale_text_to_number(struct gale_text text) {
	return atoi(gale_text_to(NULL,text));
}

/** View the contexts of a text string as opaque binary data.
 *  This is typically used with gale_map structures.
 *  \sa gale_text_from_data() */
struct gale_data gale_text_as_data(struct gale_text text) {
	struct gale_data data;
	data.p = (u8 *) text.p;
	data.l = text.l * sizeof(wch);
	return data;
}

/** Convert opaque binary data back into a text string.
 *  \sa gale_text_as_data() */
struct gale_text gale_text_from_data(struct gale_data data) {
	struct gale_text text;
	text.p = (wch *) data.p;
	text.l = data.l / sizeof(wch);
	assert(0 == (data.l % sizeof(wch)));
	return text;
}
