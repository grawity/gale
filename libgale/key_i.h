#ifndef KEY_I_H
#define KEY_I_H

#include "gale/misc.h"
#include "gale/key.h"

struct gale_key;

struct gale_key_assertion {
	int ref_count;
	int trust_count;
	struct gale_key *key;
	struct gale_key_assertion **bundled;
	struct gale_data source;
	struct gale_group group;
	struct gale_time stamp;
	struct gale_key_assertion *signer;
};

struct gale_key {
	struct gale_text name;
	struct gale_key_assertion *public,*private;
	struct gale_key *signer;
	struct gale_key_search *search;
};

/* Magic numbers for key formats. */
static const byte key_magic1[] = { 0x68, 0x13, 0x00, 0x00 };
static const byte key_magic2[] = { 0x68, 0x13, 0x00, 0x02 };
static const byte key_magic3[] = { 0x47, 0x41, 0x4C, 0x45, 0x00, 0x01 };

static const byte priv_magic1[] = { 0x68, 0x13, 0x00, 0x01 };
static const byte priv_magic2[] = { 0x68, 0x13, 0x00, 0x03 };
static const byte priv_magic3[] = { 0x47, 0x41, 0x4C, 0x45, 0x00, 0x02 };

/* Convert between old-style and new-style key names. */
struct gale_text key_i_swizzle(struct gale_text name);

/* Deconstruct key data. */
int key_i_private(struct gale_data key);
struct gale_text key_i_name(struct gale_data key);
int key_i_stub(struct gale_data key);
const struct gale_data *key_i_bundled(struct gale_data key);
struct gale_group key_i_group(struct gale_data key);
int key_i_verify(struct gale_data key,struct gale_group signer);

/* Construct key data. */
struct gale_data key_i_create(struct gale_group);

/* Add standard search hooks. */
void key_i_init_builtin(void);
void key_i_init_dirs(void);
void key_i_init_akd(void);

/* Recursively expand key relationships. */
void key_i_graph(oop_source *,struct gale_key *,int flags,struct gale_text name,
	void *(*)(oop_source *,struct gale_map *,int is_complete,int has_null,void *user),
	void *user);

#endif
