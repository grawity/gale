#include "key_i.h"
#include "gale/key.h"
#include "gale/crypto.h"
#include "gale/misc.h"

#include <assert.h>

static struct gale_key_assertion *create(void) {
	struct gale_key_assertion *output;
	gale_create(output);
	output->ref_count = 1;
	output->is_trusted = 0;
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
		return assert->is_trusted;

	if (assert->signer == assert->key->signer->public) return 1;

	if (key_i_verify(
		assert->source,
		assert->key->signer->public->group)) 
	{
		assert->signer = assert->key->signer->public;
		return 1;
	}

	return assert->is_trusted;
}

static int not_expired(struct gale_key *key,struct gale_time now) {
	struct gale_fragment f;
	if (key->public->is_trusted) return 1;
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

	/* TODO: how do we report this failure? */

	if (NULL == key 
	||  NULL == key->private 
	||  NULL == key->public) 
		return NULL;

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

static int beats(
	struct gale_key_assertion *challenger,
	struct gale_key_assertion *incumbent)
{
	struct gale_fragment fc,fi;

	if (!public_good(challenger)) return 0;
	if (!public_good(incumbent)) return 1;
	if (challenger->is_trusted > incumbent->is_trusted) return 1;

	if (!gale_group_lookup(challenger->group,G_("key.signed"),frag_time,&fc))
		fc.value.time = gale_time_zero();
	if (!gale_group_lookup(incumbent->group,G_("key.signed"),frag_time,&fi))
		fi.value.time = gale_time_zero();

	return gale_time_compare(fi.value.time,fc.value.time);
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
	const struct gale_data *bundled;
	struct gale_key_assertion *output;
	int i,count;

	name = key_i_name(source);
	if (0 == name.l) {
		gale_alert(GALE_WARNING,G_("ignoring invalid key"),0);
		return create(); /* not relevant to us */
	}

	if (key_i_stub(source)) return create();

	key = gale_key_handle(name);

	if (key_i_private(source)) {
		if (!is_trusted) {
			gale_alert(GALE_WARNING,gale_text_concat(3,
				G_("\""),name,
				G_("\": ignoring untrusted private key")),0);
			return create();
		}

		if (NULL != key->private) {
			if (!gale_data_compare(source,key->private->source)) {
				++(key->private->ref_count);
				return key->private;
			}

			gale_alert(GALE_WARNING,gale_text_concat(3,G_("\""),
				name,G_("\": ignoring extra private key")),0);
			return create();
		}

		output = create();
		output->is_trusted = is_trusted;
		output->source = source;
		output->group = key_i_group(output->source);
		output->key = key;
		key->private = output;
		key->private->key = key;
		return output;
	}

	/* If this key is the same as the active key, great! */
	if (NULL != key->public
	&&  is_trusted == key->public->is_trusted
	&& !gale_data_compare(source,key->public->source)) {
		++(key->public->ref_count);
		return key->public;
	}

	/* We're in contention for the key, which means that we get our
	   own assertion record, and must assert any bundled keys. */
	output = create();
	output->is_trusted = is_trusted;
	output->key = key;
	output->source = source;
	output->group = key_i_group(output->source);
	bundled = key_i_bundled(output->source);
	for (count = 0; bundled[count].l > 0; ++count) ;
	gale_create_array(output->bundled,1 + count);
	for (i = 0; i < count; ++i)
		output->bundled[i] = gale_key_assert(bundled[i],is_trusted);
	output->bundled[i] = NULL;

	/* It's impossible for a key to contain a complete copy of itself, so 
	   while the gale_key_assert() calls may have had untold ramifications,
	   we at least know that we're still in contention for the key. */
	assert(NULL == key->public 
	    || output->is_trusted != key->public->is_trusted
	    || gale_data_compare(output->source,key->public->source));

	if (beats(output,key->public)) {
		if (NULL != key->public) {
			gale_alert(GALE_WARNING,gale_text_concat(3,G_("\""),
				name,G_("\": replacing obsolete key")),0);
			assert(key->public->key == key);
			key->public->key = NULL;
		}
		key->public = output;
		assert(key->public->key == key);
	} else {
		/* Don't bother reporting degenerate trust overrides. */
		if (NULL == key->public)
			gale_alert(GALE_WARNING,gale_text_concat(3,G_("\""),
				name,G_("\": ignoring lame key")),0);
		else if (gale_data_compare(output->source,key->public->source))
			gale_alert(GALE_WARNING,gale_text_concat(3,G_("\""),
				name,G_("\": ignoring obsolete key")),0);
		output->key = NULL;
	}

	return output;
}

/** Inject a new key into the system. 
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
 *  \sa gale_key_assert() */
void gale_key_retract(struct gale_key_assertion *assert) {
	if (NULL == assert) return;
	assert(0 != assert->ref_count);
	if (0 != --(assert->ref_count)) return;

	while (NULL != assert->bundled && NULL != *assert->bundled)
		gale_key_retract(*assert->bundled++);

	if (NULL == assert->key) return;
	if (assert->key->public == assert)
		assert->key->public = NULL;
	else if (assert->key->private == assert)
		assert->key->private = NULL;
	assert->key = NULL;
}

/** Is an assertion implicitly trusted?
 *  \param assert Assertion handle from gale_key_public(). 
 *  \return Nonzero iff the assertion was entered with is_trusted == 1. */
int gale_key_trusted(const struct gale_key_assertion *assert) {
	return NULL != assert && assert->is_trusted;
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
