#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

#include "gale/all.h"

struct auth_id *lookup_id(struct gale_text spec) {
	struct gale_text tok = null_text;
	struct auth_id *id;

	if (gale_text_token(spec,'@',&tok) && gale_text_token(spec,'\0',&tok))
		init_auth_id(&id,spec);
	else
		init_auth_id(&id,gale_text_concat(3,
			spec,G_("@"),gale_var(G_("GALE_DOMAIN"))));

	return id;
}

static struct gale_text decolon(struct gale_text text) {
	struct gale_text tok = null_text;
	struct gale_text ret;
	gale_text_token(text,':',&tok);
	ret = tok;
	while (gale_text_token(text,':',&tok))
		ret = gale_text_concat(3,ret,G_("_"),tok);
	return ret;
}

struct gale_text id_category(struct auth_id *id,
                             struct gale_text pfx,struct gale_text sfx)
{
	struct gale_text user,name = auth_id_name(id);
	struct gale_text tok = null_text;

	gale_text_token(name,'@',&tok);
	user = tok;
	if (gale_text_token(name,'@',&tok))
		return gale_text_concat(8,
			G_("@"),decolon(tok),G_("/"), 
			pfx,G_("/"),decolon(user),G_("/"),sfx);
	else
		return gale_text_concat(5,
			G_("@"),decolon(user),G_("/"),pfx,G_("/"));
}

struct gale_text dom_category(struct gale_text dom,struct gale_text pfx) {
	struct gale_text domain;

	if (dom.l > 0) 
		domain = dom;
	else
		domain = gale_var(G_("GALE_DOMAIN"));

	return gale_text_concat(5,G_("@"),dom,G_("/"),pfx,G_("/"));
}
