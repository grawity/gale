#include "gale/misc.h"
#include "gale/core.h"

struct old_gale_message *gale_make_message(void) {
	struct old_gale_message *m;
	gale_create(m);
	m->cat.p = NULL;
	m->cat.l = 0;
	m->data = gale_group_empty();
	return m;
}

struct gale_packet *gale_transmit(struct old_gale_message *msg) {
	struct gale_packet *pkt;

	if (NULL == msg) return NULL;
	gale_create(pkt);
	pkt->routing = msg->cat;
	pkt->content.l = 0;
	gale_create_array(pkt->content.p,gale_group_size(msg->data));
	gale_pack_group(&pkt->content,msg->data);
	return pkt;
}

struct old_gale_message *gale_receive(struct gale_packet *pkt) {
	struct old_gale_message *msg;
	struct gale_data copy;

	if (NULL == pkt) return NULL;
	msg = gale_make_message();
	msg->cat = pkt->routing;
	copy = pkt->content;
	if (!gale_unpack_group(&copy,&msg->data)) {
		msg = NULL;
		gale_alert(GALE_WARNING,G_("could not decode message"),0);
	}
	return msg;
}
