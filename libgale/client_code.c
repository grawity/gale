#include "gale/client.h"
#include "gale/misc.h"
#include "client_i.h"

struct gale_text client_i_encode(const struct gale_location *loc) {
	int i;
	struct gale_text_accumulator ret = null_accumulator;
	for (i = loc->at_part; i < loc->part_count; ++i)
		gale_text_accumulate(&ret,gale_text_replace(gale_text_replace(
			loc->parts[i],
			G_(":"),G_("@.")),
			G_("/"),G_("@|")));
	gale_text_accumulate(&ret,G_("/user/"));
	for (i = 0; i < loc->at_part; i += 2) {
		gale_text_accumulate(&ret,gale_text_replace(gale_text_replace(
			loc->parts[i],
			G_(":"),G_("..")),
			G_("/"),G_(".|")));
		gale_text_accumulate(&ret,G_("/"));
	}
	return gale_text_collect(&ret);
}

struct gale_text client_i_decode(struct gale_text routing) {
	struct gale_text_accumulator ret = null_accumulator;
	struct gale_text local,domain,part;
	int slash = 1;

	if (0 == routing.l || '@' != routing.p[0]) 
		return null_text;

	for (slash = 1; slash < routing.l && routing.p[slash] != '/'; ++slash) ;
	domain = gale_text_right(gale_text_left(routing,slash),-1);
	local = gale_text_right(routing,-slash);

	if (gale_text_compare(gale_text_left(local,6),G_("/user/")))
		return null_text;
	local = gale_text_right(local,-6);
	if ('/' == local.p[local.l - 1]) --local.l;

	part = null_text;
	while (gale_text_token(local,'/',&part)) {
		if (!gale_text_accumulator_empty(&ret))
			gale_text_accumulate(&ret,G_("."));
		gale_text_accumulate(&ret,
		gale_text_replace(gale_text_replace(gale_text_replace(part,
			G_(".."),G_(":")),
			G_(".|"),G_("/")),
			G_("."),G_("")));
	}

	gale_text_accumulate(&ret,G_("@"));
	gale_text_accumulate(&ret,
		gale_text_replace(gale_text_replace(gale_text_replace(domain,
			G_("@."),G_(":")),
			G_("@|"),G_("/")),
			G_("@"),G_("")));
	return gale_text_collect(&ret);
}
