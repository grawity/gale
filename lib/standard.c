#include "gale/all.h"

struct gale_fragment *gale_make_id_class(void) {
	struct gale_fragment *f;
	gale_create(f);
	f->name = G_("id/class");
	f->type = frag_text;
	f->value.text = gale_text_concat(3,
		gale_text_from_latin1(gale_error_prefix,-1),
		G_("/"),
		gale_text_from_latin1(VERSION,-1));
	return f;
}

struct gale_fragment *gale_make_id_instance(struct gale_text terminal) {
	struct gale_fragment *f;
	gale_create(f);
	f->name = G_("id/instance");
	f->type = frag_text;
	f->value.text = gale_text_concat(9,
		gale_var(G_("GALE_DOMAIN")),G_("/"),
		gale_var(G_("HOST")),G_("/"),
		gale_var(G_("LOGNAME")),G_("/"),
		terminal,G_("/"),
		gale_text_from_number(getpid(),10,0));
	return f;
}

struct gale_fragment *gale_make_id_time(void) {
	struct gale_fragment *f;
	gale_create(f);
	f->name = G_("id/time");
	f->type = frag_time;
	f->value.time = gale_time_now();
	return f;
}
