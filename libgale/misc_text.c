#include "gale/misc.h"

#include <stdarg.h>
#include <assert.h>
#include <stdlib.h>

const struct gale_text null_text = { NULL, 0 };

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

struct gale_text _gale_text_literal(const wchar_t *sz,size_t len) {
	struct gale_text text;
	assert(sizeof(wchar_t) == sizeof(wch));
	text.p = (wch *) sz;
	text.l = len;
	return text;
}

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

int gale_text_compare(struct gale_text a,struct gale_text b) {
	size_t l = (a.l < b.l) ? a.l : b.l;
	int c = (a.p == b.p) ? 0 : memcmp(a.p,b.p,l * sizeof(wch));
	if (0 != c) return c;
	return a.l - b.l;
}

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

int gale_text_to_number(struct gale_text text) {
	return atoi(gale_text_to(NULL,text));
}

struct gale_data gale_text_as_data(struct gale_text text) {
	struct gale_data data;
	data.p = (u8 *) text.p;
	data.l = text.l * sizeof(wch);
	return data;
}

struct gale_text gale_text_from_data(struct gale_data data) {
	struct gale_text text;
	text.p = (wch *) data.p;
	text.l = data.l / sizeof(wch);
	assert(0 == (data.l % sizeof(wch)));
	return text;
}
