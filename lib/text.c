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

struct gale_text _gale_text_literal(const wchar_t *sz) {
	struct gale_text text;
	assert(sizeof(wchar_t) == sizeof(wch));
	text.p = (wch *) sz;
	for (text.l = 0; text.p[text.l] != '\0'; ++text.l) ;
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
	if (!token->p) {
		token->p = string.p - 1;
		token->l = 0;
	} else if (token->p + token->l == string.p + string.l) {
		*token = null_text;
		return 0;
	}
	token->p += token->l + 1;
	token->l = 0; 
	while (token->p + token->l != string.p + string.l
	&&     token->p[token->l] != sep)
		++token->l;
	return 1;
}

int gale_text_compare(struct gale_text a,struct gale_text b) {
	size_t l = a.l;
	int c;
	if (b.l < l) l = b.l;
	c = memcmp(a.p,b.p,l * sizeof(wch));
	if (!c) {
		if (a.l < b.l) return -1;
		if (b.l < a.l) return 1;
	}
	return c;
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
	return atoi(gale_text_to_latin1(text));
}

struct gale_text gale_text_from_latin1(const char *pch,int len) {
	struct gale_text text;
	size_t i;

	if (!pch) {
		text.p = NULL;
		text.l = 0;
	} else {
		wch *buf;
		if (len < 0) len = strlen(pch);
		buf = gale_malloc(sizeof(wch) * (text.l = len));
		for (i = 0; i < text.l; ++i) buf[i] = (unsigned char) pch[i];
		text.p = buf;
	}

	return text;
}

char *gale_text_to_latin1(struct gale_text text) {
	char *pch;
	size_t i;

	pch = gale_malloc(text.l + 1);
	for (i = 0; i < text.l; ++i) 
		if (text.p[i] < 256)
			pch[i] = text.p[i];
		else
			pch[i] = '$';

	pch[i] = '\0';
	return pch;
}

struct gale_text gale_text_from_local(const char *pch,int len) {
	return gale_text_from_latin1(pch,len);
}

char *gale_text_to_local(struct gale_text text) {
	return gale_text_to_latin1(text);
}
