#ifndef ID_H
#define ID_H

#include "common.h"
#include "sign.h"
#include "file.h"

struct auth_id {
	struct gale_text name;

	/* private */
	struct gale_group priv_data;		/* private data */
	struct inode priv_inode;		/* where it came from */

	/* public */
	struct gale_time pub_time;		/* time of last search */
	struct gale_group pub_data;		/* public data */
	struct gale_data pub_orig;		/* original bits */
	struct auth_id *pub_signer;		/* signing key */
	struct inode pub_inode;			/* where it came from */
	int pub_trusted;			/* nonzero if trusted source */
};

void _ga_warn_id(struct gale_text,...);

#endif
