#include "gale/client.h"
#include "gale/key.h"
#include "gale/crypto.h"

#include "client_i.h"

#include <assert.h>

/** Pack a Gale message into a raw "packet".
 *  Packing may require location lookups, so this function starts
 *  the process in the background, using liboop to invoke a callback
 *  when the process is complete.
 *  \param oop Liboop event source to use.
 *  \param msg Message to pack.
 *  \param func Function to call with packed message.
 *  \param user User-defined parameter to pass the function.
 *  \sa gale_unpack_message(), link_put() */
void gale_pack_message(oop_source *oop,
        struct gale_message *msg,
        gale_call_packet *call,void *user)
{
	struct gale_group data = msg->data;
	const struct gale_time now = gale_time_now();

	/* TODO: blah... private keys */
	{
		int i,num_from = 0;
		struct gale_group *keys;
		while (NULL != msg->from && NULL != msg->from[num_from]) 
			++num_from;
		gale_create_array(keys,num_from);
		for (i = 0; i < num_from; ++i) {
			keys[i] = gale_key_data(
				gale_key_private(msg->from[i]->key));
			if (msg->from[i]->at_part < 0) {
				struct gale_fragment frag;
				frag.type = frag_data;
				frag.name = G_("key.source");
				frag.value.data = gale_key_raw(
					gale_key_public(msg->from[i]->key,now));
				gale_group_replace(&keys[i],frag);
			}
		}

		if (!gale_crypto_sign(num_from,keys,&data))
			/* TODO: handle errors */;
	}

	/* TODO: check if msg->to is empty */
	{
		int i = 0,num_to = 0,is_null = 0;

		while (!is_null && NULL != msg->to && NULL != msg->to[i]) {
			struct gale_location * const loc = msg->to[i++];
			struct gale_data key = null_data;
			void *data;

			if (loc->members_null) {
				is_null = 1;
				continue;
			}

			while (gale_map_walk(loc->members,&key,&key,&data))
				++num_to;
		}

		if (!is_null && num_to > 0) {
			struct gale_group *keys;
			int j = 0;

			gale_create_array(keys,num_to);
			for (i = 0; NULL != msg->to[i]; ++i) {
				struct gale_location * const loc = msg->to[i];
				struct gale_data key = null_data;
				void *data;

				while (gale_map_walk(loc->members,&key,&key,&data))
					keys[j++] = gale_key_data(gale_key_public((struct gale_key *) data,now));
			}

			assert(j == num_to);
			if (!gale_crypto_seal(num_to,keys,&data))
				/* TODO: handle errors */;
		}
	}

	{
		struct gale_packet *pack;
		gale_create(pack);
		pack->routing = gale_pack_subscriptions(msg->to,NULL);
		pack->content.p = gale_malloc(gale_group_size(data));
		pack->content.l = 0;
		gale_pack_group(&pack->content,data);

		/* TODO: delay this */
		call(pack,user);
	}
}

/** Pack a list of locations into a subscription expression.
 *  \param list NULL-terminated array of ::location pointers.
 *  \param positive Corresponding array of subscription flags,
 *  zero for negative subscriptions and nonzero for positive
 *  subscriptions.  If \a positive is NULL, all locations are
 *  assumed to be positive.
 *  \return Raw form of subscription expression.
 *  \sa gale_find_location() */
struct gale_text gale_pack_subscriptions(
        struct gale_location **list,
        int *positive)
{
	struct gale_text_accumulator accum = null_accumulator;
	while (NULL != list && NULL != *list) {
		const int pos = (NULL == positive) || *positive++;
		const struct gale_text cat = client_i_encode(*list++);
		if (0 == cat.l) continue;

		if (!gale_text_accumulator_empty(&accum))
			gale_text_accumulate(&accum,G_(":"));
		if (!pos)
			gale_text_accumulate(&accum,G_("-"));
		gale_text_accumulate(&accum,cat);
	}

	return gale_text_collect(&accum);
}
