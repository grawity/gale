#include "key_i.h"
#include "gale/key.h"
#include "gale/crypto.h"
#include "gale/misc.h"

#include <assert.h>

static struct gale_key_assertion *create(int is_trusted) {
	struct gale_key_assertion *output;
	gale_create(output);
	output->ref_count = 1;
	output->trust_count = is_trusted ? 1 : 0;
	output->key = NULL;
	output->bundled = NULL;
	output->source = null_data;
	output->group = gale_group_empty();
	output->signer = NULL;
	return output;
}

static int public_good(struct gale_key_assertion *assert) {
	if (NULL == assert || NULL == assert->key) return 0;

	if (NULL == assert->key->signer
	||  NULL == assert->key->signer->public
	|| !public_good(assert->key->signer->public)) 
		return assert->trust_count > 0;

	if (assert->signer == assert->key->signer->public) return 1;

	/* Do this even if is_trusted to set assert->signer properly. */
	if (key_i_verify(
		assert->source,
		assert->key->signer->public->group)) 
	{
		assert->signer = assert->key->signer->public;
		return 1;
	}

	return assert->trust_count > 0;
}

static int not_expired(struct gale_key *key,struct gale_time now) {
	struct gale_fragment f;
	if (key->public->trust_count > 0) return 1;
	if (gale_group_lookup(key->public->group,G_("key.expires"),frag_time,&f)
	&& !gale_time_compare(now,f.value.time)) return 0;
	return not_expired(key->signer,now);
}

/** Report the assertion currently active for a particular public key, if any.
 *  \param key The key handle from gale_key_handle().
 *  \param time The time to use for time-dependent validity checks.  
 *         Usually gale_time_now().
 *  \return The valid assertion handle, or NULL if none exist. 
 *  \warning Active assertions for a particular key may change as time
 *           passes, keys expire and new data is integrated.  You should
 *           generally keep around only ::gale_key references, not 
 *           ::gale_key_assertion references.
 *  \sa gale_key_assert(), gale_key_private() */
const struct gale_key_assertion *gale_key_public(
	struct gale_key *key,
	struct gale_time time) 
{
	if (NULL == key
	|| !public_good(key->public) 
	|| !not_expired(key,time)) return NULL;
	return key->public;
}

/** Report the assertion currently active for a particular private key, if any.
 *  \param key The key handle from gale_key_handle().
 *  \return The valid assertion handle, or NULL if none exist. 
 *  \warning Active assertions for a particular key may change as time
 *           passes and new data is integrated.  You should
 *           generally keep around only ::gale_key references, not 
 *           ::gale_key_assertion references.
 *  \sa gale_key_assert(), gale_key_public() */
const struct gale_key_assertion *gale_key_private(struct gale_key *key) {
	struct gale_group group;

	/* TODO: how do we report this failure without spewing spoo? */

	if (NULL == key 
	||  NULL == key->private 
	||  NULL == key->public)
		return NULL;

	/* We bar untrusted private key assertions at the door. */
	assert(key->private->trust_count > 0);

	group = key->public->group;
	while (!gale_group_null(group)) {
		struct gale_fragment check,frag = gale_group_first(group);
		group = gale_group_rest(group);

		if (gale_text_compare(G_("rsa."),gale_text_left(frag.name,4)))
			continue;
		if (!gale_group_lookup(
			key->private->group,
			frag.name,frag.type,&check)
		||   gale_fragment_compare(frag,check))
			return NULL;
	}

	return key->private;
}

static void assert_trust(struct gale_key_assertion *ass) {
	if (0 == ass->trust_count++) {
		struct gale_key_assertion **bundled = ass->bundled;
		while (NULL != bundled && NULL != *bundled)
			assert_trust(*bundled++);
	}
}

static void retract_trust(struct gale_key_assertion *ass) {
	if (0 == --(ass->trust_count)) {
		struct gale_key_assertion **bundled = ass->bundled;
		while (NULL != bundled && NULL != *bundled)
			retract_trust(*bundled++);
	}
}

static int beats(
	struct gale_key_assertion *challenger,
	struct gale_key_assertion *incumbent)
{
	struct gale_fragment c,i;

	/* An unsigned key always loses. */
	if (!public_good(challenger)) return 0;
	if (!public_good(incumbent)) return 1;

	/* A trusted key always beats a signed key. */
	if (incumbent->trust_count && !challenger->trust_count) return 0;
	if (challenger->trust_count && !incumbent->trust_count) return 1;

	/* The key that was signed most recently wins. */
	if (!gale_group_lookup(challenger->group,G_("key.signed"),frag_time,&c))
		c.value.time = gale_time_zero();
	if (!gale_group_lookup(incumbent->group,G_("key.signed"),frag_time,&i))
		i.value.time = gale_time_zero();

	return gale_time_compare(i.value.time,c.value.time) <= 0;
}

/** Supply some raw key data to the system. 
 *  \param source Raw key bits.
 *  \param is_trusted Nonzero iff the key comes from a trusted source and
 *         doesn't require external validation.
 *  \return Assertion handle. 
 *  \sa gale_key_assert_group(), gale_key_retract() */
struct gale_key_assertion *gale_key_assert(
	struct gale_data source,int is_trusted) 
{
	struct gale_text name;
	struct gale_key *key;
	struct gale_key_assertion *output;

	name = key_i_name(source);
	if (0 == name.l) {
		gale_alert(GALE_WARNING,G_("ignoring invalid key"),0);
		return create(is_trusted); /* not relevant to us */
	}

	if (key_i_stub(source)) return create(is_trusted);

	key = gale_key_handle(name);

	if (key_i_private(source)) {
		if (!is_trusted) {
			gale_alert(GALE_WARNING,gale_text_concat(3,
				G_("\""),name,
				G_("\": ignoring untrusted private key")),0);
			return create(is_trusted);
		}

		if (NULL != key->private
		&& !gale_data_compare(source,key->private->source)) {
			++(key->private->ref_count);
			++(key->private->trust_count);
			return key->private;
		}

		output = create(is_trusted);
		output->source = source;
		output->group = key_i_group(output->source);
		output->key = key;

		if (NULL != key->private) {
			assert(gale_data_compare(source,key->private->source));
			gale_alert(GALE_WARNING,gale_text_concat(3,G_("\""),
				name,G_("\": replacing private key")),0);
			key->private->key = NULL;
		}

		key->private = output;
		assert(key->private->key == key);
		return output;
	}

	if (NULL != key->public 
	&& !gale_data_compare(key->public->source,source)) {
		/* We're the same as the existing key. */
		output = key->public;
		++(output->ref_count);
		if (is_trusted) assert_trust(output);
	} else {
		/* We're in contention for the key. */
		const struct gale_data * const bundled = key_i_bundled(source);
		int i,count;

		output = create(is_trusted);
		output->key = key;
		output->source = source;
		output->group = key_i_group(output->source);

		for (count = 0; bundled[count].l > 0; ++count) ;
		gale_create_array(output->bundled,1 + count);
		for (i = 0; i < count; ++i)
			output->bundled[i] = gale_key_assert(bundled[i],is_trusted);
		output->bundled[i] = NULL;

		/* It's impossible for a key to contain a copy of itself, so 
		   while who knows what gale_key_assert() calls did, at least 
		   we know we're still in contention for the key. */
		assert(NULL == key->public 
		    || gale_data_compare(key->public->source,source));
	}

	if (output != key->public) {
		/* Fight for the key! */
		if (beats(output,key->public)) {
			if (NULL != key->public) {
				gale_alert(GALE_WARNING,gale_text_concat(3,
					G_("\""),name,
					G_("\": replacing obsolete key")),0);
				assert(key->public->key == key);
				key->public->key = NULL;
			}
			key->public = output;
			assert(key->public->key == key);
		} else {
			if (NULL == key->public)
				gale_alert(GALE_WARNING,gale_text_concat(3,
					G_("\""),name,
					G_("\": ignoring lame key")),0);
			else
				gale_alert(GALE_WARNING,gale_text_concat(3,
					G_("\""),name,
					G_("\": ignoring obsolete key")),0);
			output->key = NULL;
		}
	}

	return output;
}

/** Supply some slightly cooked key data.
 *  \param source Key data.
 *  \param is_trusted Nonzero iff the key comes from a trusted source and
 *         doesn't require external validation.
 *  \return Assertion handle. 
 *  \sa gale_key_assert(), gale_key_retract() */
struct gale_key_assertion *gale_key_assert_group(
	struct gale_group source,
	int is_trusted)
{
	return gale_key_assert(key_i_create(source),is_trusted);
}

/** Retract a previous assertion.
 *  \param assert Assertion handle from gale_key_assert().
 *  \param is_trusted The same value passed to gale_key_assert().
 *  \sa gale_key_assert() */
void gale_key_retract(struct gale_key_assertion *ass,int is_trusted) {
	if (NULL == ass) return;
	if (is_trusted) retract_trust(ass);

	assert(0 != ass->ref_count);
	if (0 != --(ass->ref_count)) return;

	while (NULL != ass->bundled && NULL != *ass->bundled)
		gale_key_retract(*ass->bundled++,0);

	if (NULL != ass->key) {
		if (ass->key->public == ass)
			ass->key->public = NULL;
		else if (ass->key->private == ass)
			ass->key->private = NULL;
		ass->key = NULL;
	}
}

/** Is an assertion implicitly trusted?
 *  \param assert Assertion handle from gale_key_public(). 
 *  \return Nonzero iff the assertion was entered with is_trusted == 1. */
int gale_key_trusted(const struct gale_key_assertion *assert) {
	return NULL != assert && assert->trust_count > 0;
}

/** What is the key associated with an assertion?
 *  \param assert Assertion handle from gale_key_assert().
 *  \return The key associated with the assertion, or NULL if none. */
struct gale_key *gale_key_owner(const struct gale_key_assertion *assert) {
	return (NULL == assert) ? NULL : assert->key;
}

/** Is a public key signed?
 *  \param assert Assertion handle from gale_key_public(). 
 *  \return The assertion which signed this one, or NULL if none. */
const struct gale_key_assertion *gale_key_signed(
	const struct gale_key_assertion *assert) 
{
	/* HACK ... mostly harmless */
	if (public_good((struct gale_key_assertion *) assert)
	&&  NULL != assert->key
	&&  NULL != assert->key->signer
	&&  assert->signer == assert->key->signer->public)
		return assert->signer;
	return NULL;
}

/** Get a key's data.
 *  \param assert Assertion handle from gale_key_public() 
 *         or gale_key_private(). */
struct gale_group gale_key_data(const struct gale_key_assertion *assert) {
	if (NULL == assert) return gale_group_empty();
	return gale_crypto_original(assert->group);
}

/** Get the original, raw data associated with a key.
 *  \param assert Assertion handle from gale_key_public() 
 *         or gale_key_private(). */
struct gale_data gale_key_raw(const struct gale_key_assertion *assert) {
	if (NULL == assert) return null_data;
	return assert->source;
}
