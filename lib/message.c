#include "location.h"

#include "gale/auth.h"
#include "gale/misc.h"
#include "gale/client.h"

#include <assert.h>

struct pack {
	oop_source *oop;
	struct gale_message *msg;
	gale_call_packet *func;
	void *user;
	struct gale_map *map;
	int out;
};

static void *on_pack(struct gale_text name,
	struct gale_location *loc,void *user) {
	struct pack *pack = (struct pack *) user;
	if (NULL != loc 
	&&  NULL != pack->map
	&&  NULL == gale_map_find(pack->map,gale_text_as_data(name))) {
		struct gale_group data = gale_location_public_data(loc);
		gale_map_add(pack->map,gale_text_as_data(name),loc);
		
		data = gale_group_find(data,G_("key.member"));
		while (!gale_group_null(data)) {
			struct gale_fragment frag = gale_group_first(data);
			if (frag_text != frag.type)
				gale_alert(GALE_WARNING,
				G_("invalid key.member fragment"),0);
			else if (0 == frag.value.text.l)
				pack->map = NULL;
			else {
				++(pack->out);
				gale_find_exact_location(pack->oop,
					frag.value.text,
					on_pack,pack);
			}
				
			data = gale_group_rest(data);
			data = gale_group_find(data,G_("key.member"));
		}
	}

	if (0 == --(pack->out)) {
		struct gale_packet *packet;
		struct gale_text *array;
		struct auth_id **crypt;
		struct gale_group data;
		struct gale_data key = null_data;
		int i,num;
		void *loc;

		if (NULL == pack->msg) return pack->func(NULL,pack->user);
		if (NULL == pack->msg->to || NULL == pack->msg->to[0]) {
			gale_alert(GALE_WARNING,
				G_("no recipients for message!"),0);
			return pack->func(NULL,pack->user);
		}

		data = pack->msg->data;
		if (NULL != pack->msg->from && NULL != pack->msg->from[0]) {
			auth_sign(&data,pack->msg->from[0]->key,AUTH_SIGN_NORMAL);
			if (NULL != pack->msg->from[1])
				gale_alert(GALE_WARNING,
				G_("multiple signatures not supported"),0);
		}

		num = 0;
		while (NULL != pack->map
		   &&  gale_map_walk(pack->map,&key,&key,NULL)) ++num;
		gale_create_array(crypt,num);

		i = 0;
		key = null_data;
		while (NULL != pack->map
		   &&  gale_map_walk(pack->map,&key,&key,&loc)) {
			crypt[i] = ((struct gale_location *) loc)->key;
			++i;
		}
		assert(num == i);

		if (0 != num && !auth_encrypt(&data,num,crypt))
			return pack->func(NULL,pack->user);

		for (num = 0; NULL != pack->msg->to[num]; ++num) ;
		assert(0 != num);

		gale_create_array(array,2*num - 1);
		for (i = 0; i < num; ++i) {
			if (i > 0) array[2*i - 1] = G_(":");
			array[2*i] = pack->msg->to[i]->routing;
		}

		gale_create(packet);
		packet->routing = gale_text_concat_array(2*num - 1,array);
		packet->content.p = gale_malloc(gale_group_size(data));
		packet->content.l = 0;
		gale_pack_group(&packet->content,data);
		return pack->func(packet,pack->user);
	}

	return OOP_CONTINUE;
}

void gale_pack_message(oop_source *oop,
	struct gale_message *msg,
	gale_call_packet *func,void *user) 
{
	struct gale_location **ptr;
	struct pack *pack;
	gale_create(pack);
	pack->oop = oop;
	pack->msg = msg;
	pack->func = func;
	pack->user = user;
	pack->map = gale_make_map(0);
	pack->out = 0;

	/* TODO what if there are none */
	for (ptr = msg->to; NULL != ptr && NULL != *ptr; ++ptr) {
		++(pack->out);
		gale_find_exact_location(oop,
			gale_location_name(*ptr),
			on_pack,pack);
	}
}

struct unpack {
	oop_source *oop;
	struct gale_message *msg;
	gale_call_message *func;
	void *user;
	int lookup;
};

static void *unpack_complete(oop_source *src,struct timeval tv,void *x) {
	struct unpack *unp = (struct unpack *) x;
	if (0 != --(unp->lookup)) return OOP_CONTINUE;
	return unp->func(unp->msg,unp->user);
}

static void *on_unpack_from(struct gale_text n,struct gale_location *loc,void *x) {
	struct unpack *unp = (struct unpack *) x;
	struct gale_location **ptr = unp->msg->from;
	while (NULL != *ptr) ++ptr;
	*ptr++ = loc;
	*ptr++ = NULL;
	return unpack_complete(unp->oop,OOP_TIME_NOW,unp);
}

static void *on_unpack_to(struct gale_text n,struct gale_location *loc,void *x) {
	struct unpack *unp = (struct unpack *) x;
	struct gale_location **ptr = unp->msg->to;
	while (NULL != *ptr) ++ptr;
	*ptr++ = loc;
	*ptr++ = NULL;
	return unpack_complete(unp->oop,OOP_TIME_NOW,unp);
}

static struct gale_text unmangle(struct gale_text cat) {
	struct gale_text domain = null_text,part,local;

	if (0 == cat.l || '@' != cat.p[0]) return null_text;
	cat = gale_text_right(cat,-1);
	gale_text_token(cat,'/',&domain);

	part = domain;
	if (!gale_text_token(cat,'/',&part)
	||   gale_text_compare(part,G_("user"))) return null_text;

	local = null_text;
	while (gale_text_token(cat,'/',&part))
		local = local.l ? gale_text_concat(3,local,G_("."),part) : part;
	if (!gale_text_compare(gale_text_right(local,1),G_(".")))
		local = gale_text_left(local,-1);

	return gale_text_concat(3,local,G_("@"),domain);
}

void gale_unpack_message(oop_source *oop,
	struct gale_packet *pkt,
	gale_call_message *func,void *user)
{
	struct gale_data copy = pkt->content;
	struct gale_text cat = null_text;
	struct auth_id *signer;
	struct unpack *unp;
	int cat_count;

	gale_create(unp);
	unp->oop = oop;
	gale_create(unp->msg);
	unp->msg->from = NULL;
	unp->msg->to = NULL;
	unp->func = func;
	unp->user = user;
	unp->lookup = 0;

	if (!gale_unpack_group(&copy,&unp->msg->data)) {
		unp->msg = NULL;
		gale_alert(GALE_WARNING,G_("could not decode message"),0);
		unp->oop->on_time(unp->oop,OOP_TIME_NOW,unpack_complete,unp);
		return;
	} 

	auth_decrypt(&unp->msg->data);
	signer = auth_verify(&unp->msg->data);
	if (NULL != signer) {
		gale_create_array(unp->msg->from,2);
		unp->msg->from[0] = NULL;
		++(unp->lookup);
		gale_find_exact_location(oop,
			auth_id_name(signer),
			on_unpack_from,unp);
	}

	cat_count = 0;
	while (gale_text_token(pkt->routing,':',&cat)) {
		struct gale_text name = unmangle(cat);
		if (0 == name.l) continue;
		++(unp->lookup);
		gale_find_exact_location(oop,name,on_unpack_to,unp);
		++cat_count;
	}

	gale_create_array(unp->msg->to,1 + cat_count);
	unp->msg->to[0] = NULL;
}

struct gale_text gale_pack_subscriptions(
	struct gale_location **list,
	int *positive)
{
	struct gale_text *concat;
	int count;

	for (count = 0; NULL != list[count]; ++count) ;
	gale_create_array(concat,2*count);
	concat[0] = (!positive || positive[0]) ? G_("+") : G_("-");
	for (count = 0; NULL != list[count]; ++count) {
		if (0 != count) 
			concat[2*count] = (!positive || positive[count])
				? G_(":+") : G_(":-");
		concat[1 + 2*count] = list[count]->routing;
	}

	return gale_text_concat_array(2*count,concat);
}
