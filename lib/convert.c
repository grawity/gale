#include "gale/globals.h"
#include "gale/misc.h"

#include <assert.h>
#include <netinet/in.h>

struct gale_encoding {
#ifdef HAVE_ICONV
	iconv_t from;
	iconv_t to;
#else
	int dummy;
#endif
};

#ifdef HAVE_ICONV
static iconv_t get_iconv(struct gale_text to,struct gale_text from) {
	const char *tocode = gale_text_to(NULL,to.l ? to : G_("ASCII"));
	const char *fromcode = gale_text_to(NULL,from.l ? from : G_("ASCII"));
	iconv_t ret = iconv_open(tocode,fromcode);
	return ret;
}
#endif

struct gale_encoding *gale_make_encoding(struct gale_text name) {
	struct gale_text ienc = (4 == sizeof(wch)) ? G_("UCS-4") : G_("UCS-2");
	struct gale_encoding *enc = NULL;

#ifdef HAVE_ICONV
	if (0 != name.l) {
		gale_create(enc);
		enc->from = get_iconv(ienc,name);
		enc->to = get_iconv(name,ienc);

		if ((iconv_t) -1 == enc->from
		||  (iconv_t) -1 == enc->to) {
			gale_alert(GALE_WARNING,gale_text_to(NULL,name),errno);
			enc = NULL;
		}
	}
#endif

	return enc;
}

static struct gale_text gale_text_from_ascii(const char *pch,int len) {
	struct gale_text text;
	size_t i;

	if (!pch) {
		text.p = NULL;
		text.l = 0;
	} else {
		wch *buf;
		buf = gale_malloc(sizeof(wch) * (text.l = len));
		for (i = 0; i < text.l; ++i) buf[i] = (unsigned char) pch[i];
		text.p = buf;
	}

	return text;
}

static char *gale_text_to_ascii(struct gale_text text) {
	char *pch;
	size_t i;

	pch = gale_malloc(text.l + 1);
	for (i = 0; i < text.l; ++i) 
		if (text.p[i] < 128)
			pch[i] = text.p[i];
		else
			pch[i] = '?';

	pch[i] = '\0';
	return pch;
}

#ifdef HAVE_ICONV
static void to_ucs(wch *ch) {
	if (4 == sizeof(wch))
		*ch = htonl(*ch);
	else {
		assert(2 == sizeof(wch));
		*ch = htons(*ch);
	}
}

static void from_ucs(wch *ch) {
	if (4 == sizeof(wch))
		*ch = ntohl(*ch);
	else {
		assert(2 == sizeof(wch));
		*ch = ntohs(*ch);
	}
}
#endif

struct gale_text gale_text_from(struct gale_encoding *e,const char *p,int l) {
	wch *buf;
	const char *inbuf;
	char *outbuf;
	size_t inbytes,outbytes,ret;
	struct gale_text out;
	struct gale_encoding *save = gale_global->enc_console;

	if (l < 0) l = (NULL == p) ? 0 : strlen(p);
	if (NULL == e) return gale_text_from_ascii(p,l);

	gale_global->enc_console = NULL;
	gale_create_array(buf,l);

	inbuf = p;
	inbytes = l;
	outbuf = (char *) buf;
	outbytes = sizeof(wch) * l;

	ret = iconv(e->from,&inbuf,&inbytes,&outbuf,&outbytes);
	while ((size_t) -1 == ret) {
		switch (errno)
		{
		case EILSEQ:
			++inbuf;
			--inbytes;
			break;
		case E2BIG:
			assert(0); /* >1 character per byte?! */
		default:
			gale_alert(GALE_WARNING,"conversion error",errno);
		case EINVAL:
			inbuf += inbytes;
			inbytes = 0;
			break;
		}

		ret = iconv(e->from,&inbuf,&inbytes,&outbuf,&outbytes);
	}

	out.p = buf;
	out.l = (wch *) outbuf - out.p;
	while (buf - out.p < out.l) from_ucs(buf++);
	gale_global->enc_console = save;
	return out;
}

char *gale_text_to(struct gale_encoding *e,struct gale_text t) {
	wch *copy;
	char *buf;
	size_t alloc;
	const char *inbuf;
	char *outbuf;
	size_t inbytes,outbytes;
	struct gale_encoding *save = gale_global->enc_console;

	if (NULL == e) return gale_text_to_ascii(t);

	gale_global->enc_console = NULL;
	gale_create_array(copy,t.l);
	gale_create_array(buf,(alloc = t.l));

	for (inbytes = 0; inbytes < t.l; ++inbytes) {
		copy[inbytes] = t.p[inbytes];
		to_ucs(&copy[inbytes]);
	}

	inbuf = (const char *) copy;
	inbytes = sizeof(wch) * t.l;
	outbuf = buf;
	outbytes = alloc;

	for (;;) {
		char *newbuf;

		size_t ret = iconv(e->to,&inbuf,&inbytes,&outbuf,&outbytes);
		if ((size_t) -1 != ret) {
			if (inbuf != NULL) 
				inbuf = NULL;
			else if (outbuf != NULL) {
				*outbuf++ = '\0'; 
				outbuf = NULL;
			}
			else {
				gale_global->enc_console = save;
				return buf;
			}
			continue;
		}

		switch (errno) {
		case EINVAL:
		case EILSEQ:
			assert(0); /* invalid UCS-2/UCS-4?  impossible!! */
		default:
			gale_alert(GALE_WARNING,"conversion error",errno);
			inbuf += inbytes;
			inbytes = 0;
			break;

		case E2BIG:
			gale_create_array(newbuf,(alloc *= 2));
			memcpy(newbuf,buf,outbuf - buf);
			outbytes = alloc - (outbuf - buf);
			outbuf = newbuf + (outbuf - buf);
			buf = newbuf;
			break;
		}
	}
}
