/** \file
 *  Low-level encryption layer. */

#ifndef GALE_CRYPTO_H
#define GALE_CRYPTO_H

#include "gale/misc.h"

struct gale_data gale_crypto_hash(struct gale_data);
struct gale_data gale_crypto_random(int len);

struct gale_group gale_crypto_generate(struct gale_text id);
struct gale_group gale_crypto_public(struct gale_group);

int gale_crypto_seal(int num,
	const struct gale_group *keys,
	struct gale_group *data);

const struct gale_text *gale_crypto_target(struct gale_group encrypted);
int gale_crypto_open(struct gale_group key,struct gale_group *data);

int gale_crypto_sign(int num,
	const struct gale_group *keys,
	struct gale_group *data);

const struct gale_text *gale_crypto_sender(struct gale_group signed_group);
const struct gale_data *gale_crypto_bundled(struct gale_group signed_group);
struct gale_group gale_crypto_original(struct gale_group signed_group);

int gale_crypto_verify(int num,
	const struct gale_group *keys,
	struct gale_group signed_group);

const struct gale_data *gale_crypto_sign_raw(int num,
	const struct gale_group *keys,
	struct gale_data data);

int gale_crypto_verify_raw(int num,
	const struct gale_group *keys,
	const struct gale_data *sigs,
	struct gale_data data);

#endif
