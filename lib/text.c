#include "gale/misc.h"
#include "gale/core.h"

struct gale_text new_gale_text(size_t alloc) {
	struct gale_text text;
	text.p = gale_malloc(alloc * sizeof(wch));
	text.l = 0;
	return text;
}

void free_gale_text(struct gale_text text) {
	if (text.p) gale_free(text.p);
}

void gale_text_append(struct gale_text *ptr,struct gale_text text) {
	memcpy(ptr->p + ptr->l,text.p,text.l * sizeof(wch));
	ptr->l += text.l;
}

struct gale_text gale_text_dup(struct gale_text text) {
	if (text.p) text.p = gale_memdup(text.p,sizeof(wch) * text.l);
	return text;
}

struct gale_text gale_text_left(struct gale_text text,int i) {
	if (i < 0) {
		if ((size_t) -i > text.l)
			text.l = 0;
		else
			text.l -= i;
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
		token->p = NULL;
		token->l = 0;
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

struct gale_text gale_text_from_latin1(const char *pch,int len) {
	struct gale_text text;
	size_t i;

	if (!pch) {
		text.p = NULL;
		text.l = 0;
	} else {
		if (len < 0) len = strlen(pch);
		text.p = gale_malloc(sizeof(wch) * (text.l = len));
		for (i = 0; i < text.l; ++i) text.p[i] = pch[i];
	}

	return text;
}

char *gale_text_to_latin1(struct gale_text text) {
	char *pch;
	size_t i;

	if (!text.p)
		pch = NULL;
	else {
		pch = gale_malloc(text.l + 1);
		for (i = 0; i < text.l; ++i) 
			if (text.p[i] < 256)
				pch[i] = text.p[i];
			else
				pch[i] = '$';
		pch[i] = '\0';
	}

	return pch;
}

struct gale_text gale_text_from_local(const char *pch,int len) {
	return gale_text_from_latin1(pch,len);
}

char *gale_text_to_local(struct gale_text text) {
	return gale_text_to_latin1(text);
}

char *gale_text_hack(struct gale_text text) {
	static char *pch[5];
	static int i = 0;
	i = (i + 1) % (sizeof(pch) / sizeof(pch[0]));
	if (pch[i]) gale_free(pch[i]);
	pch[i] = gale_text_to_local(text);
	return pch[i];
}
