#include "gale/all.h"

struct collector {
	oop_source *source;
	gale_call_error_message *call;
	void *data;
	struct gale_text buffer;
	int active;
};

struct gale_message *gale_error_message(struct gale_text body) {
	struct gale_message *msg = new_message();
	struct gale_fragment frag;
	msg->cat = gale_var(G_("GALE_ERRORS"));
	if (0 == msg->cat.l)
		msg->cat = dom_category(null_text,
			gale_text_concat(2,G_("server/"),
			gale_text_from_local(gale_global->error_prefix,-1)));
	frag.name = G_("message/body");
	frag.type = frag_text;
	frag.value.text = body;
	gale_group_add(&msg->data,frag);
	gale_add_id(&msg->data,G_("daemon"));
	frag.name = G_("message/sender");
	frag.type = frag_text;
	frag.value.text = gale_text_concat(6,
		gale_var(G_("HOST")),G_(" "),
		gale_text_from_local(gale_global->error_prefix,-1),
		G_(" ("),gale_var(G_("LOGNAME")),G_(")"));
	gale_group_add(&msg->data,frag);
	return msg;
}

static void *on_collect(oop_source *source,struct timeval now,void *data) {
	struct collector *c = (struct collector *) data;
	struct gale_message *msg = gale_error_message(c->buffer);
	c->active = 0;
	return c->call(c->source,msg,c->data);
}

static void *on_error(gale_error severity,struct gale_text msg,void *data) {
	struct collector *c = (struct collector *) data;
	if (!c->active) {
		c->buffer = null_text;
		c->source->on_time(c->source,OOP_TIME_NOW,on_collect,c);
		c->active = 1;
	}
	c->buffer = gale_text_concat(3,c->buffer,msg,G_("\n"));
	return OOP_CONTINUE;
}

void gale_on_error_message(
	oop_source *source,
	gale_call_error_message *call,void *data)
{
	if (NULL == call)
		gale_on_error(source,NULL,NULL);
	else {
		struct collector *c = gale_malloc(sizeof(*c));
		c->source = source;
		c->call = call;
		c->data = data;
		c->active = 0;
		gale_on_error(source,on_error,c);
	}
}

static void *send_message(struct gale_message *msg,void *data) {
	struct gale_link *l = (struct gale_link *) data;
	link_put(l,msg);
	return OOP_CONTINUE;
}

void gale_set_error_link(oop_source *source,struct gale_link *link) {
	if (NULL == link)
		gale_on_error_message(source,NULL,NULL);
	else
		gale_on_error_message(source,send_message,link);
}
