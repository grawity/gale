#include "msg_old.h"

#include "gale/client.h"
#include "gale/misc.h"
#include "gale/auth.h"

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>

struct gale_fragment **unpack_old_message(struct gale_data msgdata) {
	int count = 0,alloc = 10;
	struct gale_fragment **frag = gale_malloc(alloc * sizeof(*frag));

	while (msgdata.l > 0) {
		struct gale_data line,key,data;
		struct gale_fragment *create;

		line.p = msgdata.p;
		line.l = 0;
		while (line.l < msgdata.l && line.p[line.l] != '\r') ++line.l;
		msgdata.l -= line.l;
		msgdata.p += line.l;

		if (msgdata.l > 0 && *msgdata.p == '\r') {
			--msgdata.l;
			++msgdata.p;
		}
		if (msgdata.l > 0 && *msgdata.p == '\n') {
			--msgdata.l;
			++msgdata.p;
		}

		if (line.l == 0) break;
		if (msgdata.l == 0) {
			gale_alert(GALE_WARNING,"invalid header",0);
			continue;
		}

		key.p = line.p;
		key.l = 0;
		while (key.l < line.l && key.p[key.l] != ':') ++key.l;
		if (key.l == line.l) {
			gale_alert(GALE_WARNING,"malformed header",0);
			continue;
		}

		data.p = line.p + key.l + 1;
		data.l = line.l - key.l - 1;
		if (data.l > 0 && data.p[0] == ' ') {
			++data.p;
			--data.l;
		}

		/* hack alert */
		assert(key.p[key.l] == ':');
		key.p[key.l] = '\0';
		assert(data.p[data.l] == '\r');
		data.p[data.l] = '\0';
		gale_create(create);

		if (!strcasecmp(key.p,"encryption") 
		&& !strncmp(data.p,"g2",2)) {
			create->name = G_("security/encryption");
			create->type = frag_data;
			create->value.data = msgdata;
			msgdata.l = 0;
		} else if (!strcasecmp(key.p,"signature") 
		       &&  !strncmp(data.p,"g2/",3)) {
			size_t tlen = strlen(data.p + 3);
			size_t slen = dearmor_len(tlen);
			size_t size = slen + gale_u32_size() + msgdata.l;
			create->name = G_("security/signature");
			create->type = frag_data;
			create->value.data.l = 0;
			create->value.data.p = gale_malloc_atomic(size);
			gale_pack_u32(&create->value.data,slen);
			dearmor(data.p + 3,tlen,
				create->value.data.p + create->value.data.l);
			create->value.data.l += slen;
			gale_pack_copy(&create->value.data,msgdata.p,msgdata.l);
			msgdata.l = 0;
		} else if (!strcasecmp(key.p,"receipt-to")) {
                        create->name = G_("question/receipt");
                        create->type = frag_text;
                        create->value.text = gale_text_from_latin1(data.p,-1);
		} else if (!strcasecmp(key.p,"subject")) {
			create->name = G_("message/subject");
			create->type = frag_text;
			create->value.text = gale_text_from_latin1(data.p,-1);
		} else if (!strcasecmp(key.p,"from")) {
			create->name = G_("message/sender");
			create->type = frag_text;
			create->value.text = gale_text_from_latin1(data.p,-1);
		} else if (!strcasecmp(key.p,"to")) {
			create->name = G_("message/recipient");
			create->type = frag_text;
			create->value.text = gale_text_from_latin1(data.p,-1);
		} else if (!strcasecmp(key.p,"time")) {
			struct timeval tv;
			tv.tv_sec = atoi(data.p);
			tv.tv_usec = 0;
			create->name = G_("id/time");
			create->type = frag_time;
			gale_time_from(&create->value.time,&tv);
		} else if (!strcasecmp(key.p,"agent")) {
			create->name = G_("id/instance");
			create->type = frag_text;
			create->value.text = gale_text_from_latin1(data.p,-1);
		} else if (!strcasecmp(key.p,"receipt-to")) {
			create->name = G_("question/receipt");
			create->type = frag_text;
			create->value.text = gale_text_from_latin1(data.p,-1);
		} else if (!strcasecmp(key.p,"request-key")) {
			create->name = G_("question/key");
			create->type = frag_text;
			create->value.text = gale_text_from_latin1(data.p,-1);
		} else {
			create->name = gale_text_concat(2,
				G_("legacy/"),
				gale_text_from_latin1(key.p,-1));
			create->value.text = gale_text_from_latin1(data.p,-1);
		}

		frag[count++] = create;
		while (alloc < count + 2)
			frag = gale_realloc(frag,(alloc *= 2) * sizeof(*frag));

		key.p[key.l] = ':';
		data.p[data.l] = '\r';
	}

	if (msgdata.l > 0) {
		gale_create(frag[count]);
		frag[count]->name = G_("message/body");
		frag[count]->type = frag_text;
		frag[count]->value.text = 
			gale_text_from_latin1(msgdata.p,msgdata.l);
		++count;
	}

	frag[count] = NULL;
	return frag;
}

struct gale_data pack_old_message(struct gale_fragment **frags) {
	struct gale_fragment **ptr;
	struct gale_data data,temp,body;

	data.l = 2;

	for (ptr = frags; *ptr; ++ptr) {
		struct gale_fragment * const frag = *ptr;
		u32 len;

		switch (frag->type) {
		case frag_data:
		    temp = frag->value.data;
		    if (!gale_text_compare(frag->name,
		                           G_("security/encryption"))) {
			data.l = temp.l + 16;
			data.p = gale_malloc_atomic(data.l);
			strcpy(data.p,"Encryption: g2\r\n");
			memcpy(data.p + 16,temp.p,data.l - 16);
			return data;
		    } else 
		    if (!gale_text_compare(frag->name,
		                           G_("security/signature"))
		    &&  gale_unpack_u32(&temp,&len)
		    &&  len < temp.l) {
			size_t tlen = armor_len(len);
			data.l = 16 + tlen + temp.l - len;
		        data.p = gale_malloc_atomic(data.l);
		        strcpy(data.p,"Signature: g2/");
			armor(temp.p,len,data.p + 14);
			strcpy(data.p + 14 + tlen,"\r\n");
			memcpy(data.p + 16 + tlen,temp.p + len,temp.l - len);
			return data;
		    } else
		    if (!gale_text_compare(frag->name,G_("answer/key")))
			data.l += frag->value.data.l;
		    break;

		case frag_text:
		    data.l += 30 + frag->value.text.l * 2;
		    break;

		case frag_time:
		    if (!gale_text_compare(frag->name,G_("id/time")))
			data.l += 30;
		    break;

		case frag_number:
		    break;

		default:
		    assert(0);
		}
	}

	data.p = gale_malloc(data.l);
	data.l = 0;

	body.p = NULL;
	body.l = 0;

	for (ptr = frags; *ptr; ++ptr) {
		struct gale_fragment * const frag = *ptr;
		switch (frag->type) {
		case frag_data:
		    if (!gale_text_compare(frag->name,G_("answer/key")))
			body = frag->value.data;
		    break;

		case frag_text:
		    if (!gale_text_compare(frag->name,G_("message/body"))) {
			body.p = gale_text_to_latin1(frag->value.text);
			body.l = strlen(body.p);
		    } else
		    if (!gale_text_compare(frag->name,G_("message/subject"))) {
			sprintf(data.p + data.l,"Subject: %s\r\n",
			        gale_text_to_latin1(frag->value.text));
			data.l += strlen(data.p + data.l);
		    } else
		    if (!gale_text_compare(frag->name,G_("message/keywords"))) {
			sprintf(data.p + data.l,"Keywords: %s\r\n",
			        gale_text_to_latin1(frag->value.text));
			data.l += strlen(data.p + data.l);
		    } else
		    if (!gale_text_compare(frag->name,G_("message/sender"))) {
			sprintf(data.p + data.l,"From: %s\r\n",
			        gale_text_to_latin1(frag->value.text));
			data.l += strlen(data.p + data.l);
		    } else
		    if (!gale_text_compare(frag->name,G_("message/recipient")))
		    {
			sprintf(data.p + data.l,"To: %s\r\n",
			        gale_text_to_latin1(frag->value.text));
			data.l += strlen(data.p + data.l);
		    } else
		    if (!gale_text_compare(frag->name,G_("id/instance"))) {
			sprintf(data.p + data.l,"Agent: %s\r\n",
			        gale_text_to_latin1(frag->value.text));
			data.l += strlen(data.p + data.l);
		    } else
		    if (!gale_text_compare(frag->name,G_("question/receipt"))) {
			sprintf(data.p + data.l,"Receipt-To: %s\r\n",
			        gale_text_to_latin1(frag->value.text));
			data.l += strlen(data.p + data.l);
		    } else
		    if (!gale_text_compare(frag->name,G_("question/key"))) {
			sprintf(data.p + data.l,"Request-Key: %s\r\n",
			        gale_text_to_latin1(frag->value.text));
			data.l += strlen(data.p + data.l);
		    }
		    break;

		case frag_time:
		    if (!gale_text_compare(frag->name,G_("id/time"))) {
			struct timeval tv;
			gale_time_to(&tv,frag->value.time);
			sprintf(data.p + data.l,"Time: %lu\r\n",tv.tv_sec);
			data.l += strlen(data.p + data.l);
		    }
		    break;

		case frag_number:
		    break;

		default:
		    assert(0);
		}
	}

	if (0 != body.l) {
		sprintf(data.p + data.l,"\r\n");
		memcpy(data.p + data.l + 2,body.p,body.l);
		data.l += 2 + body.l;
	}

	return data;
}
